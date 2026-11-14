#include "onenet_service.h"
#include "onenet_config.h"
#include "onenet_ca.h"
#include "onenet_model.h"
#include "alarm_engine.h"
#include "alarm_service.h"
#include "esp_at.h"
#include "esp_mqtt.h"
#include "clock_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "$sys/" CLOCK_ONENET_PRODUCT_ID "/" CLOCK_ONENET_DEVICE_NAME "/thing/property/"
static const char topic_set[]=PREFIX "set",topic_get[]=PREFIX "get";
static const char topic_set_reply[]=PREFIX "set_reply",topic_get_reply[]=PREFIX "get_reply";
static const char topic_post[]=PREFIX "post",topic_post_reply[]=PREFIX "post/reply";
#define CACHE_COUNT 4U
#define POST_COUNT 12U
#define COMMAND_MAX_AGE_MS 2500U

typedef struct {
    onenet_set_request_t request;
    uint32_t local_id;
    unsigned code;
    const char *message;
    bool used,pending,reply_due;
} command_cache_t;
typedef struct {uint32_t id,sent_ms;uint16_t mask;bool used;} pending_post_t;

static struct {
    command_cache_t cache[CACHE_COUNT];
    pending_post_t posts[POST_COUNT];
    uint32_t next_post,last_attempt,retry_ms,last_report,last_periodic,last_revision;
    uint32_t blocked_at[13];
    uint16_t blocked;
    uint16_t reports;
    unsigned cache_cursor;
    bool initialized,online,attempted,wifi,failed,missing_logged,waiting_time_logged,expired_logged;
} cloud;
static struct {float temperature,humidity;bool valid;} indoor;
#if defined(__GNUC__) && defined(__arm__)
#define CLOUD_WORK_RAM __attribute__((section(".clock_onenet_ram"),aligned(4)))
#else
#define CLOUD_WORK_RAM
#endif
static struct {
    char output[ESP_MQTT_PAYLOAD_CAPACITY+1U];
    char partial[ESP_MQTT_PAYLOAD_CAPACITY+1U];
    char partial_topic[ESP_MQTT_TOPIC_CAPACITY+1U];
    esp_mqtt_message_t incoming;
    size_t partial_length;
    uint32_t partial_started;
} work CLOUD_WORK_RAM;

bool onenet_service_configured(void)
{
    return CLOCK_ONENET_TOKEN[0] && CLOCK_ONENET_PRODUCT_ID[0] && CLOCK_ONENET_DEVICE_NAME[0]
        && CLOCK_ONENET_MQTT_HOST[0] && CLOCK_ONENET_MQTT_PORT==8883U;
}
bool onenet_service_online(void){return cloud.online && esp_at_mqtt_connected();}
bool onenet_service_busy(void)
{
    if(!onenet_service_online())return false;
    for(unsigned i=0;i<CACHE_COUNT;++i)if(cloud.cache[i].used && (cloud.cache[i].pending || cloud.cache[i].reply_due))return true;
    return esp_at_mqtt_pending();
}
void onenet_service_init(void)
{
    // 初始化OneNET服务的状态，包括清空缓存、设置重试时间和报告标志等。
    // memset是用来将指定内存区域的内容设置为0，这里用于清空cloud和work结构体的内容。
    memset(&cloud,0,sizeof cloud);// cloud是一个结构体，包含了OneNET服务的状态信息，如缓存、报告标志、重试时间等。通过memset将其所有成员初始化为0。
    memset(&work,0,sizeof work);
    memset(&indoor,0,sizeof indoor);
    cloud.retry_ms=5000U;cloud.next_post=100U;cloud.reports=ONENET_PROPERTY_ALL;
    cloud.initialized=true;
}
void onenet_service_sensor(float temperature,float humidity)
{
    bool valid=isfinite(temperature) && isfinite(humidity) && humidity>=0 && humidity<=100;
    taskENTER_CRITICAL();
    indoor.temperature=valid?temperature:0.0f;indoor.humidity=valid?humidity:0.0f;indoor.valid=valid;
    taskEXIT_CRITICAL();
}
static void network_failed(const char *stage)
{
    esp_mqtt_diagnostic_t diagnostic=esp_at_mqtt_diagnostic();
    clock_log("[ONENET] %s failed, transport error %u; step=%s; reply=%s; esp=0x%08lX; retry pending\r\n",
              stage,(unsigned)esp_at_mqtt_last_error(),diagnostic.step,diagnostic.reply,(unsigned long)diagnostic.at_error);
    cloud.online=false;
    cloud.failed=true;cloud.attempted=true;
    work.partial_length=0U;
    cloud.last_attempt=(uint32_t)xTaskGetTickCount();
    if(cloud.retry_ms<60000U)cloud.retry_ms=cloud.retry_ms>30000U?60000U:cloud.retry_ms*2U;
    if(esp_at_is_ready() && esp_at_mqtt_connected())(void)esp_at_mqtt_disconnect();
}

void onenet_service_network(bool wifi_connected,bool esp_time_ready,uint32_t now_ms)
{
    if(!cloud.initialized || !onenet_service_configured()) {
        if(cloud.initialized && !cloud.missing_logged){clock_log_line("[ONENET] Token missing or configuration invalid; local functions continue");cloud.missing_logged=true;}
        return;
    }
    if(!wifi_connected) {
        cloud.online=false;cloud.wifi=false;
        if(!cloud.failed)cloud.attempted=false;
        work.partial_length=0U;return;
    }
    cloud.wifi=true;
    if(cloud.online && !esp_at_mqtt_connected()) {
        cloud.online=false;cloud.attempted=false;work.partial_length=0U;
        clock_log_line("[ONENET] MQTT disconnected; reconnect pending");
    }
    if(cloud.online)return;
    if(!esp_time_ready) {
        if(!cloud.waiting_time_logged){clock_log_line("[ONENET] Waiting for ESP SNTP before verified TLS");cloud.waiting_time_logged=true;}
        return;
    }
    if(!esp_at_is_ready() || (cloud.attempted && now_ms - cloud.last_attempt<cloud.retry_ms))return;
 #if CLOCK_ONENET_TOKEN_EXPIRES != 0
    alarm_service_snapshot_t snapshot;
    uint32_t seconds;
    if(alarm_service_snapshot(&snapshot) && snapshot.time_valid
       && alarm_calendar_to_seconds(&snapshot.now,&seconds)
       && (uint64_t)seconds+UINT64_C(946656000)>=CLOCK_ONENET_TOKEN_EXPIRES) {
        if(!cloud.expired_logged){clock_log_line("[ONENET] Token expired; regenerate local Token, rebuild and flash");cloud.expired_logged=true;}
        return;
    }
 #endif
    cloud.attempted=true;cloud.last_attempt=now_ms;
    /* OneNET's current dedicated selfsigned CA/server certificate has no
     * product DNS name. AT4.1 maps nonempty SNI to certificate-name matching.
     * Explicitly omit it; scheme3 still validates this CA and certificate date.
     * The generic driver retains host-name validation for NULL/default SNI. */
    const esp_mqtt_config_t config={CLOCK_ONENET_MQTT_HOST,(uint16_t)CLOCK_ONENET_MQTT_PORT,
        CLOCK_ONENET_DEVICE_NAME,CLOCK_ONENET_PRODUCT_ID,CLOCK_ONENET_TOKEN,onenet_ca_pem,onenet_ca_pem_length,""};
    clock_log("[ONENET] Connecting TLS %s:%u; product %s; device %s\r\n",config.host,(unsigned)config.port,config.username,config.client_id);
    clock_log_line("[ONENET] TLS policy: OneNET CA/date verification; no SNI or DNS-name check");
    if(!esp_at_mqtt_configure(&config)){network_failed("TLS/credential configuration");return;}
    if(!esp_at_mqtt_connect(20000U)){network_failed("MQTT connection");return;}
    if(!esp_at_mqtt_subscribe(topic_set,1U) || !esp_at_mqtt_subscribe(topic_get,1U)
       || !esp_at_mqtt_subscribe(topic_post_reply,1U)){network_failed("MQTT subscription");return;}
    cloud.online=true;cloud.failed=false;cloud.retry_ms=5000U;cloud.waiting_time_logged=false;
    cloud.reports=ONENET_PROPERTY_ALL;cloud.blocked=0U;work.partial_length=0U;
    memset(cloud.posts,0,sizeof cloud.posts);
    clock_log_line("[ONENET] MQTT connected and subscribed; property reports pending");
}

static bool publish(const char *topic,size_t length)
{
    if(!length)return false;
    if(!esp_at_mqtt_publish(topic,work.output,length,0U,false)) {
        network_failed("MQTT publish");return false;
    }
    return true;
}
static bool reply(const char *topic,const char *id,unsigned code,const char *message)
{
    return publish(topic,onenet_model_encode_reply(id,code,message,work.output,sizeof work.output));
}
static void collect_results(void)
{
    for(unsigned i=0;i<CACHE_COUNT;++i) {
        command_cache_t *entry=&cloud.cache[i];
        if(!entry->used || !entry->pending)continue;
        alarm_remote_result_t result=alarm_service_remote_result(entry->local_id);
        if(result==ALARM_REMOTE_PENDING)continue;
        entry->pending=false;entry->reply_due=true;
        entry->code=result==ALARM_REMOTE_SAVED?200U:500U;
        entry->message=result==ALARM_REMOTE_SAVED?"success":result==ALARM_REMOTE_FAILED?"save_failed":"result_unknown";
        if(result==ALARM_REMOTE_SAVED)cloud.reports|=ONENET_PROPERTY_ALARM(entry->request.index)|ONENET_PROPERTY_CONFIG_REVISION;
        clock_log("[ONENET] Alarm %u request %s %s\r\n",(unsigned)entry->request.index+1U,entry->request.id,entry->message);
    }
}
static bool send_due_reply(void)
{
    for(unsigned i=0;i<CACHE_COUNT;++i) {
        command_cache_t *entry=&cloud.cache[i];
        if(entry->used && !entry->pending && entry->reply_due) {
            if(reply(topic_set_reply,entry->request.id,entry->code,entry->message))entry->reply_due=false;
            return true;
        }
    }
    return false;
}

static void handle_set(const char *data,size_t length,uint32_t received_ms,uint32_t now_ms)
{
    alarm_service_snapshot_t snapshot;
    onenet_set_request_t request;
    if(!alarm_service_snapshot(&snapshot)) {
        alarm_config_defaults(&snapshot.config);
        (void)onenet_model_decode_set(data,length,&snapshot.config,&request);
        if(request.id[0])(void)reply(topic_set_reply,request.id,503U,"alarm_unavailable");
        return;
    }
    onenet_model_status_t status=onenet_model_decode_set(data,length,&snapshot.config,&request);
    for(unsigned i=0;i<CACHE_COUNT;++i)if(cloud.cache[i].used && strcmp(cloud.cache[i].request.id,request.id)==0) {
        command_cache_t *entry=&cloud.cache[i];
        /* Replay validation must use the original committed merge context:
         * omitted date fields may have changed since this patch succeeded. */
        alarm_config_t original=snapshot.config;
        original.entries[entry->request.index]=entry->request.entry;
        onenet_set_request_t replay;
        if(onenet_model_decode_set(data,length,&original,&replay)!=ONENET_MODEL_OK ||
           !onenet_model_same_patch(&entry->request,&replay))
            (void)reply(topic_set_reply,request.id,409U,"request_id_conflict");
        else if(!entry->pending)entry->reply_due=true;
        return;
    }
    if(status!=ONENET_MODEL_OK) {
        if(request.id[0])(void)reply(topic_set_reply,request.id,400U,"invalid_alarm_request");
        return;
    }
    if(now_ms-received_ms>=COMMAND_MAX_AGE_MS){(void)reply(topic_set_reply,request.id,408U,"request_expired");return;}
    unsigned index=CACHE_COUNT;
    for(unsigned i=0;i<CACHE_COUNT;++i) {
        unsigned candidate=(cloud.cache_cursor+i)%CACHE_COUNT;
        if(!cloud.cache[candidate].used || (!cloud.cache[candidate].pending && !cloud.cache[candidate].reply_due)){index=candidate;break;}
    }
    if(index==CACHE_COUNT){(void)reply(topic_set_reply,request.id,409U,"busy");return;}
    uint32_t id=0;
    alarm_remote_submit_t accepted=alarm_service_remote_submit_patch(request.index,&request.entry,request.fields_mask,snapshot.state_version,&id);
    if(accepted!=ALARM_REMOTE_ACCEPTED) {
        (void)reply(topic_set_reply,request.id,accepted==ALARM_REMOTE_INVALID?400U:
            accepted==ALARM_REMOTE_UNAVAILABLE?503U:409U,"busy_or_conflict");return;
    }
    command_cache_t *entry=&cloud.cache[index];memset(entry,0,sizeof *entry);
    entry->request=request;entry->local_id=id;entry->used=true;entry->pending=true;
    cloud.cache_cursor=(index+1U)%CACHE_COUNT;
}
static bool values(onenet_model_values_t *out,alarm_service_snapshot_t *snapshot)
{
    if(!alarm_service_snapshot(snapshot))return false;
    memset(out,0,sizeof *out);out->config=&snapshot->config;
    out->config_revision=snapshot->revision;out->ringing_mask=snapshot->ringing_mask;
    taskENTER_CRITICAL();
    out->temperature=indoor.temperature;out->humidity=indoor.humidity;out->sensor_valid=indoor.valid;
    taskEXIT_CRITICAL();
    return true;
}
static void handle_get(const char *data,size_t length)
{
    onenet_get_request_t request;
    if(onenet_model_decode_get(data,length,&request)!=ONENET_MODEL_OK) {
        if(request.id[0])(void)reply(topic_get_reply,request.id,400U,"invalid_get_request");
        return;
    }
    alarm_service_snapshot_t snapshot;onenet_model_values_t current;
    if(!values(&current,&snapshot)){(void)reply(topic_get_reply,request.id,503U,"alarm_unavailable");return;}
    uint16_t mask=request.property_mask;
    if(!current.sensor_valid && (mask&(ONENET_PROPERTY_TEMPERATURE|ONENET_PROPERTY_HUMIDITY))) {
        (void)reply(topic_get_reply,request.id,503U,"sensor_unavailable");return;
    }
    size_t n=onenet_model_encode_get_reply(request.id,&current,mask,work.output,sizeof work.output);
    if(n)(void)publish(topic_get_reply,n);else (void)reply(topic_get_reply,request.id,413U,"reply_too_large");
}
static void handle_post_reply(const char *data,size_t length)
{
    char id[ONENET_MODEL_ID_SIZE];unsigned code;
    if(onenet_model_decode_reply(data,length,id,&code)!=ONENET_MODEL_OK)return;
    for(unsigned i=0;i<POST_COUNT;++i)if(cloud.posts[i].used) {
        char expected[14];snprintf(expected,sizeof expected,"%lu",(unsigned long)cloud.posts[i].id);
        if(strcmp(id,expected)!=0)continue;
        cloud.posts[i].used=false;
        if(code==200U)clock_log("[ONENET] Cloud accepted report %s\r\n",id);
        else {
            clock_log("[ONENET] Cloud rejected report %s mask 0x%04X, code %u; check OneJSON property names/types\r\n",id,(unsigned)cloud.posts[i].mask,code);
            cloud.reports|=cloud.posts[i].mask;cloud.blocked|=cloud.posts[i].mask;
            for(unsigned bit=0;bit<13U;++bit)if(cloud.posts[i].mask&(1U<<bit))cloud.blocked_at[bit]=(uint32_t)xTaskGetTickCount();
        }
        return;
    }
}
/* Framing scan only finds a complete JSON envelope across ESP receive chunks;
 * the strict model parser validates every token/type before any operation. */
static int envelope(const char *data,size_t length)
{
    unsigned depth=0;bool quoted=false,escaped=false,started=false,complete=false;
    for(size_t i=0;i<length;++i) {
        unsigned char c=(unsigned char)data[i];
        if(c==0U)return -1;
        if(quoted) {if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c==' ' || c=='\r' || c=='\n' || c=='\t')continue;
        if(complete)return -1;
        if(!started){if(c!='{')return -1;started=true;}
        if(c=='"')quoted=true;
        else if(c=='{' || c=='['){if(++depth>16U)return -1;}
        else if(c=='}' || c==']'){if(!depth)return -1;if(--depth==0U)complete=true;}
    }
    return complete?1:0;
}
static bool independent_message(const esp_mqtt_message_t *message)
{
    const char *data=(const char *)message->data;
    if(envelope(data,message->length)!=1)return false;
    if(strcmp(message->topic,topic_set)==0) {
        alarm_config_t base;onenet_set_request_t request;
        alarm_config_defaults(&base);
        (void)onenet_model_decode_set(data,message->length,&base,&request);
        /* A strict JSON parse plus unambiguous top-level ID establishes a
         * fresh envelope. Validate its patch against the real snapshot later. */
        return request.id[0]!='\0';
    }
    if(strcmp(message->topic,topic_get)==0) {
        onenet_get_request_t request;
        (void)onenet_model_decode_get(data,message->length,&request);
        return request.id[0]!='\0';
    }
    char id[14];unsigned code;
    return onenet_model_decode_reply(data,message->length,id,&code)==ONENET_MODEL_OK;
}
static void consume(const esp_mqtt_message_t *message,uint32_t now_ms)
{
    if(strcmp(message->topic,topic_set)!=0 && strcmp(message->topic,topic_get)!=0 && strcmp(message->topic,topic_post_reply)!=0)return;
    if(message->length>ESP_MQTT_PAYLOAD_CAPACITY)return;
    /* A complete envelope with an unambiguous ID supersedes a broken
     * prefix on this topic. Arbitrarily interleaved partial broker messages
     * have no boundary metadata in ESP-AT and are rejected conservatively. */
    if(work.partial_length && independent_message(message))work.partial_length=0U;
    if(work.partial_length && (strcmp(work.partial_topic,message->topic)!=0 ||
       message->received_ms-work.partial_started>1000U))work.partial_length=0U;
    if(!work.partial_length) {
        strcpy(work.partial_topic,message->topic);work.partial_started=message->received_ms;
    }
    if(work.partial_length+message->length>ESP_MQTT_PAYLOAD_CAPACITY){work.partial_length=0U;return;}
    memcpy(work.partial+work.partial_length,message->data,message->length);work.partial_length+=message->length;
    int complete=envelope(work.partial,work.partial_length);
    if(complete==0)return;
    size_t length=work.partial_length;work.partial_length=0U;
    if(complete<0)return;
    if(strcmp(work.partial_topic,topic_set)==0)handle_set(work.partial,length,work.partial_started,now_ms);
    else if(strcmp(work.partial_topic,topic_get)==0)handle_get(work.partial,length);
    else handle_post_reply(work.partial,length);
}

static void report(uint32_t now_ms)
{
    alarm_service_snapshot_t snapshot;onenet_model_values_t current;
    if(!values(&current,&snapshot))return;
    if(snapshot.revision!=cloud.last_revision){cloud.last_revision=snapshot.revision;cloud.reports|=ONENET_PROPERTY_ALARMS|ONENET_PROPERTY_CONFIG_REVISION;}
    if(now_ms-cloud.last_periodic>=10000U){cloud.last_periodic=now_ms;cloud.reports|=ONENET_PROPERTY_TELEMETRY;}
    for(unsigned i=0;i<POST_COUNT;++i)if(cloud.posts[i].used && now_ms-cloud.posts[i].sent_ms>=10000U) {
        cloud.reports|=cloud.posts[i].mask;cloud.posts[i].used=false;
        clock_log_line("[ONENET] Property report not confirmed; retry pending");
    }
    for(unsigned bit=0;bit<13U;++bit)if((cloud.blocked&(1U<<bit)) && now_ms-cloud.blocked_at[bit]>=30000U)
        cloud.blocked&=(uint16_t)~(1U<<bit);
    uint16_t eligible=cloud.reports&(uint16_t)~cloud.blocked;
    if(!eligible || now_ms-cloud.last_report<250U)return;
    unsigned post=POST_COUNT;
    for(unsigned i=0;i<POST_COUNT;++i)if(!cloud.posts[i].used){post=i;break;}
    if(post==POST_COUNT)return;
    uint16_t mask=(uint16_t)(eligible & (uint16_t)(0U-eligible));
    if(!current.sensor_valid) {
        mask&=(uint16_t)~(ONENET_PROPERTY_TEMPERATURE|ONENET_PROPERTY_HUMIDITY);
        cloud.reports&=(uint16_t)~(ONENET_PROPERTY_TEMPERATURE|ONENET_PROPERTY_HUMIDITY);
    }
    if(!mask)return;
    if(++cloud.next_post==0U)cloud.next_post=1U;
    char id[14];snprintf(id,sizeof id,"%lu",(unsigned long)cloud.next_post);
    size_t n=onenet_model_encode_report(id,&current,mask,work.output,sizeof work.output);
    if(!n){clock_log_line("[ONENET] Property serialization failed");return;}
    if(publish(topic_post,n)) {
        cloud.reports&=(uint16_t)~mask;cloud.last_report=now_ms;
        cloud.posts[post]=(pending_post_t){cloud.next_post,now_ms,mask,true};
    }
}

void onenet_service_poll(uint32_t now_ms)
{
    (void)now_ms; /* Refresh the ISR timebase after every potentially slow AT call. */
    if(!cloud.initialized || !onenet_service_configured())return;
    collect_results();
    if(!onenet_service_online())return;
    if(send_due_reply())return;
    for(unsigned budget=0;budget<3U;++budget) {
        if(!esp_at_mqtt_poll(&work.incoming))break;
        consume(&work.incoming,(uint32_t)xTaskGetTickCount());
        if(!onenet_service_online())return;
        collect_results();
        if(send_due_reply())return;
    }
    for(unsigned i=0;i<CACHE_COUNT;++i)if(cloud.cache[i].used && cloud.cache[i].pending)return;
    if(!esp_at_mqtt_pending())report((uint32_t)xTaskGetTickCount());
}
