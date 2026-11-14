#ifndef CLOCK_OTA_SERVICE_H
#define CLOCK_OTA_SERVICE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define OTA_HEARTBEAT_CLOCK 0x01U
#define OTA_HEARTBEAT_UI 0x02U
#define OTA_HEARTBEAT_ALARM 0x04U
#define OTA_HEARTBEAT_ALL 0x07U

/* 初始化在调度器启动前调用；start 仅在本地 UI、闹钟与网络任务创建成功后调用。 */
void ota_service_init(void);
void ota_service_start(void);
/* 仅由对应本地任务在实际完成一轮工作后报告单个来源位，网络任务不得代报。 */
void ota_service_heartbeat(uint8_t source);
/* 只允许唯一网络任务在 HTTP 响应关闭之后调用，后台分段发送回调不得调用。 */
void ota_service_network_poll(uint32_t now_ms);
/* 只由唯一网络所有者在健康确认及 HTTP 响应关闭之后发现候选，不访问应用 Flash。 */
void ota_service_autocheck_poll(bool connected, uint32_t now_ms);
void ota_service_status(char *output, size_t capacity, uint32_t session);
/* 仅接受 session 与 token 两个不重复表单字段；返回 HTTP 状态码。 */
int ota_service_request(const char *body, uint32_t session);
#endif
