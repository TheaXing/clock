#include "clock_web.h"
#include "clock_http.h"
#include "web_page.h"
#include "alarm_service.h"
#include "alarm_engine.h"
#include "esp_at.h"
#include "clock_console.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WEB_LINKS 5U
#define WEB_RETRY_MS 30000U
#define WEB_REQUEST_MS 35000U
typedef struct {
    clock_http_parser_t parser;
    uint32_t generation;
    uint32_t started_ms;
    bool active;
} web_client_t;
/* CPU-only work buffers: UART uses polling TX, never DMA from these arrays.
 * This NOLOAD region is explicitly cleared by init; the existing startup
 * only clears normal .bss. Keep the proven 92KiB RTOS heap unchanged. */
#if defined(__GNUC__) && defined(__arm__)
#define WEB_WORK_RAM __attribute__((section(".clock_web_ram"),aligned(4)))
#else
#define WEB_WORK_RAM
#endif
static web_client_t clients[WEB_LINKS] WEB_WORK_RAM;
static char json[2304] WEB_WORK_RAM;
static uint32_t session_token,last_attempt;
static bool network_connected,attempted;
static void (*background_poll)(void);
_Static_assert(sizeof(clock_http_parser_t)<=768U,"HTTP parser RAM budget exceeded");

static bool append(size_t *used,const char *format,...)
{
    if(*used>=sizeof json)return false;
    va_list args;va_start(args,format);
    int n=vsnprintf(json+*used,sizeof json-*used,format,args);
    va_end(args);
    if(n<0 || (size_t)n>=sizeof json-*used)return false;
    *used+=(size_t)n;return true;
}

static void respond(unsigned link,const char *status,const char *type,const char *body,size_t length)
{
    web_client_t *client=&clients[link];
    char header[256];
    int n=snprintf(header,sizeof header,
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        status,type,(unsigned long)length);
    if(n>0 && (size_t)n<sizeof header &&
       esp_at_server_send((uint8_t)link,client->generation,header,(size_t)n)) {
        while(length) {
            if(background_poll)background_poll();
            size_t piece=length>512U?512U:length;
            if(!esp_at_server_send((uint8_t)link,client->generation,body,piece))break;
            body+=piece;length-=piece;
        }
        if(background_poll)background_poll();
    }
    (void)esp_at_server_close((uint8_t)link,client->generation);
    client->active=false;
}
static void error_response(unsigned link,const char *status,const char *error)
{
    int n=snprintf(json,sizeof json,"{\"error\":\"%s\"}",error);
    respond(link,status,"application/json; charset=utf-8",json,n>0?(size_t)n:0U);
}

/* The browser sends decimal form fields only. Deliberately reject percent
 * escapes, signs, unknown/duplicate fields and overflow rather than guess. */
static bool form_numbers(const char *body,const char *const *names,unsigned count,uint32_t *values)
{
    uint32_t seen=0U;
    const char *p=body;
    if(!p || !*p || count>31U)return false;
    for(;;) {
        const char *key=p;
        while(*p && *p!='=' && *p!='&')++p;
        if(*p!='=')return false;
        size_t length=(size_t)(p-key);
        unsigned index;
        for(index=0;index<count;++index)
            if(strlen(names[index])==length && memcmp(key,names[index],length)==0)break;
        if(index==count || (seen&(1UL<<index)))return false;
        ++p;
        if(*p<'0' || *p>'9')return false;
        uint32_t value=0U;
        do {
            uint32_t digit=(uint32_t)(*p-'0');
            if(value>(UINT32_MAX-digit)/10U)return false;
            value=value*10U+digit;++p;
        }while(*p>='0' && *p<='9');
        values[index]=value;seen|=1UL<<index;
        if(!*p)break;
        if(*p++!='&' || !*p)return false;
    }
    return seen==((1UL<<count)-1UL);
}

static void get_alarms(unsigned link)
{
    alarm_service_snapshot_t data;
    if(!alarm_service_snapshot(&data)){error_response(link,"503 Service Unavailable","alarm_unavailable");return;}
    size_t used=0;
    bool ok=append(&used,"{\"session\":%lu,\"version\":%lu,\"revision\":%lu,\"time_valid\":%s,"
        "\"time\":\"%04u-%02u-%02u %02u:%02u:%02u\",\"saving\":%s,\"storage_ok\":%s,"
        "\"ringing_mask\":%u,\"snoozed_mask\":%u,\"local_editing\":%s,\"alarms\":[",
        (unsigned long)session_token,(unsigned long)data.state_version,(unsigned long)data.revision,
        data.time_valid?"true":"false",(unsigned)data.now.year,(unsigned)data.now.month,(unsigned)data.now.day,
        (unsigned)data.now.hour,(unsigned)data.now.minute,(unsigned)data.now.second,
        data.saving?"true":"false",data.storage_ok?"true":"false",(unsigned)data.ringing_mask,
        (unsigned)data.snoozed_mask,data.local_editing?"true":"false");
    for(unsigned i=0;i<ALARM_COUNT && ok;++i) {
        const alarm_entry_t *e=&data.config.entries[i];
        ok=append(&used,"%s{\"id\":%u,\"enabled\":%s,\"hour\":%u,\"minute\":%u,\"repeat_mask\":%u,"
            "\"year\":%u,\"month\":%u,\"day\":%u,\"ring_seconds\":%u,\"snooze_seconds\":%u}",
            i?",":"",i+1U,e->enabled?"true":"false",(unsigned)e->hour,(unsigned)e->minute,
            (unsigned)e->repeat_mask,(unsigned)e->year,(unsigned)e->month,(unsigned)e->day,
            (unsigned)e->ring_seconds,(unsigned)e->snooze_seconds);
    }
    if(!ok || !append(&used,"]}")){error_response(link,"500 Internal Server Error","response_overflow");return;}
    respond(link,"200 OK","application/json; charset=utf-8",json,used);
}

static void get_result(unsigned link,const char *query)
{
    static const char *const names[]={"id","session"};
    uint32_t values[2];
    if(!form_numbers(query,names,2,values) || values[0]==0U){error_response(link,"400 Bad Request","invalid_query");return;}
    if(values[1]!=session_token){error_response(link,"409 Conflict","session_changed");return;}
    const char *state;
    switch(alarm_service_remote_result(values[0])) {
    case ALARM_REMOTE_PENDING:state="pending";break;
    case ALARM_REMOTE_SAVED:state="saved";break;
    case ALARM_REMOTE_FAILED:state="failed";break;
    default:state="unknown";break;
    }
    int n=snprintf(json,sizeof json,"{\"session\":%lu,\"state\":\"%s\"}",(unsigned long)session_token,state);
    respond(link,"200 OK","application/json; charset=utf-8",json,(size_t)n);
}

static void post_alarm(unsigned link,unsigned index,const clock_http_parser_t *request)
{
    static const char *const names[]={"session","version","enabled","hour","minute","repeat_mask",
        "year","month","day","ring_seconds","snooze_seconds"};
    uint32_t v[11],id=0;
    if(!request->has_clock_request){error_response(link,"403 Forbidden","request_header_required");return;}
    if(!request->form_content_type){error_response(link,"415 Unsupported Media Type","form_required");return;}
    if(!form_numbers(request->body,names,11,v) || v[2]>1U || v[3]>23U || v[4]>59U || v[5]>127U ||
       v[6]<2020U || v[6]>2099U || v[7]<1U || v[7]>12U || v[8]<1U || v[8]>31U ||
       v[9]<5U || v[9]>600U || v[10]<60U || v[10]>1800U || v[10]%60U!=0U) {
        error_response(link,"400 Bad Request","invalid_fields");return;
    }
    if(v[0]!=session_token){error_response(link,"409 Conflict","session_changed");return;}
    alarm_entry_t entry={(uint16_t)v[6],(uint16_t)v[9],(uint16_t)v[10],
        (uint8_t)v[7],(uint8_t)v[8],(uint8_t)v[3],(uint8_t)v[4],(uint8_t)v[5],(uint8_t)v[2]};
    if(!alarm_entry_valid(&entry)){error_response(link,"400 Bad Request","invalid_date");return;}
    alarm_service_snapshot_t data;
    if(!alarm_service_snapshot(&data)){error_response(link,"503 Service Unavailable","alarm_unavailable");return;}
    if(v[1]!=data.state_version){error_response(link,"409 Conflict","version_changed");return;}
    alarm_remote_submit_t accepted=alarm_service_remote_submit((uint8_t)index,&entry,v[1],&id);
    switch(accepted) {
    case ALARM_REMOTE_ACCEPTED: {
        int n=snprintf(json,sizeof json,"{\"id\":%lu,\"state\":\"pending\"}",(unsigned long)id);
        respond(link,"202 Accepted","application/json; charset=utf-8",json,(size_t)n);break;
    }
    case ALARM_REMOTE_BUSY:error_response(link,"409 Conflict","busy_or_local_editing");break;
    case ALARM_REMOTE_CONFLICT:error_response(link,"409 Conflict","version_changed");break;
    case ALARM_REMOTE_INVALID:error_response(link,"400 Bad Request","invalid_fields");break;
    default:error_response(link,"503 Service Unavailable","alarm_unavailable");break;
    }
}

static void dispatch(unsigned link)
{
    clock_http_parser_t *request=&clients[link].parser;
    if(request->method==CLOCK_HTTP_GET) {
        if(strcmp(request->target,"/")==0 || strcmp(request->target,"/index.html")==0)
            respond(link,"200 OK","text/html; charset=utf-8",clock_web_page,clock_web_page_length);
        else if(strcmp(request->target,"/api/alarms")==0)get_alarms(link);
        else if(strncmp(request->target,"/api/result?",12)==0)get_result(link,request->target+12);
        else if(strcmp(request->target,"/favicon.ico")==0)respond(link,"204 No Content","image/x-icon","",0);
        else error_response(link,"404 Not Found","not_found");
    }else if(strncmp(request->target,"/api/alarms/",12)==0 && request->target[12]>='1' &&
             request->target[12]<='8' && request->target[13]=='\0')
        post_alarm(link,(unsigned)(request->target[12]-'1'),request);
    else error_response(link,"404 Not Found","not_found");
}

void clock_web_init(uint32_t session)
{
    memset(clients,0,sizeof clients);
    memset(json,0,sizeof json);
    session_token=session;
    network_connected=false;attempted=false;last_attempt=0U;
    background_poll=NULL;
}
void clock_web_set_background_poll(void (*poll)(void)){background_poll=poll;}

void clock_web_network(bool connected,uint32_t now_ms)
{
    if(session_token==0U)return;
    if(!connected) {
        memset(clients,0,sizeof clients);
        network_connected=false;attempted=false;return;
    }
    bool newly_connected=!network_connected;
    network_connected=true;
    if((newly_connected || !esp_at_server_ready()) && esp_at_is_ready() &&
       (!attempted || now_ms-last_attempt>=WEB_RETRY_MS || newly_connected)) {
        char ip[16];last_attempt=now_ms;attempted=true;
        memset(clients,0,sizeof clients);
        if(esp_at_server_start(80U,ip))clock_log("[WEB] Open http://%s/ on the same WiFi\r\n",ip);
        else clock_log_line("[WEB] Server unavailable; retry in 30 seconds (check ESP-AT TCP server support)");
    }
}

void clock_web_poll(uint32_t now_ms)
{
    if(!network_connected || !esp_at_server_ready())return;
    for(unsigned i=0;i<WEB_LINKS;++i)if(clients[i].active && now_ms-clients[i].started_ms>=WEB_REQUEST_MS) {
        (void)esp_at_server_close((uint8_t)i,clients[i].generation);clients[i].active=false;
    }
    for(unsigned budget=0;budget<4U;++budget) {
        esp_server_chunk_t chunk;
        if(!esp_at_server_poll(&chunk))break;
        if(chunk.link>=WEB_LINKS)continue;
        web_client_t *client=&clients[chunk.link];
        if(chunk.event==ESP_SERVER_EVENT_CLOSED) {
            if(client->generation==chunk.generation)client->active=false;
            continue;
        }
        if(chunk.event==ESP_SERVER_EVENT_CONNECTED || client->generation!=chunk.generation) {
            clock_http_init(&client->parser);client->generation=chunk.generation;
            client->started_ms=now_ms;client->active=true;
        }
        if(chunk.event!=ESP_SERVER_EVENT_DATA || !client->active)continue;
        clock_http_status_t status=clock_http_feed(&client->parser,chunk.data,chunk.length);
        if(status==CLOCK_HTTP_READY){dispatch(chunk.link);return;}
        else if(status==CLOCK_HTTP_TOO_LARGE)error_response(chunk.link,"413 Content Too Large","request_too_large");
        else if(status==CLOCK_HTTP_METHOD_NOT_ALLOWED)error_response(chunk.link,"405 Method Not Allowed","method_not_allowed");
        else if(status!=CLOCK_HTTP_NEED_MORE)error_response(chunk.link,"400 Bad Request","malformed_http");
    }
}
