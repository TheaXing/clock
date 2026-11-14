#ifndef CLOCK_WIFI_H
#define CLOCK_WIFI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "clock_config.h"
#define WIFI_SSID CLOCK_WIFI_SSID
#define WIFI_PASSWD CLOCK_WIFI_PASSWORD
bool wifi_init(void);
bool wifi_wait_connect(void);
bool wifi_ready(void);
void wifi_mark_unready(void);
typedef enum {
    WIFI_SETUP_WAITING,
    WIFI_SETUP_CONNECTING,
    WIFI_SETUP_VERIFYING,
    WIFI_SETUP_SAVING,
    WIFI_SETUP_SAVED,
    WIFI_SETUP_FAILED
} wifi_setup_state_t;
/** 提供独立的屏幕快照，临时热点密码仅供本机显示。 */
typedef struct {
    bool active;              // 配网或模块错误页面是否可见
    bool ready;               // 热点及网页是否均可用
    bool client;              // 是否已有手机获得地址
    wifi_setup_state_t state; // 当前配网阶段
    uint32_t generation;      // 显示内容变化代次
    char ap_ssid[33];         // 临时热点名称
    char ap_password[17];     // 临时热点密码
    char error[16];           // 固定错误名称
} wifi_view_t;
/* 除快照读取外，均由唯一网络任务调用；提交仅排队，轮询才执行联网。 */
void wifi_poll(uint32_t now_ms);
bool wifi_setup_active(void);
bool wifi_setup_available(void);
void wifi_get_view(wifi_view_t *view);
int wifi_setup_submit(const char *body, uint32_t session);
void wifi_setup_status(char *output, size_t capacity, uint32_t session);
#endif
