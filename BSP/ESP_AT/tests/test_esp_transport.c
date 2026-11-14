#include "FreeRTOS.h"
#include "esp_at.h"
#include "esp_mqtt.h"
#include "semphr.h"
#include "task.h"
#include "usart.h"
#include "weather.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UART_HandleTypeDef huart2;
/** 保存主机信号量令牌计数。 */
struct test_semaphore {
    int tokens; // 可获取的令牌数
};
static TickType_t tick;
static uint8_t *rx_target;
static unsigned arm_count;
static const char *expected_tx[32];
static const char *rx_reply[32];
static const char *rx_wait_reply[32];
static size_t expected_length[32], reply_length[32], wait_reply_length[32];
static size_t transmitted_length[32];
static HAL_StatusTypeDef transmit_result[32];
static const char *pending_reply;
static size_t pending_reply_length;
static const char *delay_reply;
static const char *critical_exit_reply;
static unsigned critical_exit_skips;
static unsigned script_length, script_index;
static bool inject_uart_error;
static bool model_data_entry;
static unsigned data_entry_remaining;
static unsigned scheduler_suspend_depth;
static SemaphoreHandle_t command_lock;
static bool refuse_command_lock;
static void receive_text(const char *text);
static void receive_bytes(const void *bytes, size_t length);

/** 功能：离开临界区时注入预定的异步应答；参数：无；返回：无。 */
void test_exit_critical(void) {
    if (critical_exit_reply) {
        if (critical_exit_skips != 0U) {
            --critical_exit_skips;
        } else {
            const char *reply = critical_exit_reply;
            critical_exit_reply = NULL;
            receive_text(reply);
        }
    }
}

/** 功能：读取模拟任务时钟；参数：无；返回：对应模拟结果或测试退出码。 */
TickType_t xTaskGetTickCount(void) {
    return tick;
}
/** 功能：读取模拟中断时钟；参数：无；返回：对应模拟结果或测试退出码。 */
TickType_t xTaskGetTickCountFromISR(void) {
    return tick;
}
/** 功能：推进模拟时钟并注入延迟应答；参数：ticks，延迟节拍数；返回：无。 */
void vTaskDelay(TickType_t ticks) {
    tick += ticks;
    if (delay_reply) {
        const char *reply = delay_reply;
        delay_reply = NULL;
        receive_text(reply);
    }
}
/** 功能：返回正在运行的模拟调度器状态；参数：无；返回：对应模拟结果或测试退出码。 */
BaseType_t xTaskGetSchedulerState(void) {
    return taskSCHEDULER_RUNNING;
}
/** 功能：记录调度器暂停深度；参数：无；返回：无。 */
void vTaskSuspendAll(void) {
    ++scheduler_suspend_depth;
}
/** 功能：恢复模拟调度器并验证配对；参数：无；返回：对应模拟结果或测试退出码。 */
BaseType_t xTaskResumeAll(void) {
    assert(scheduler_suspend_depth != 0U);
    --scheduler_suspend_depth;
    return pdFALSE;
}
/** 功能：创建有初始令牌的模拟互斥锁；参数：无；返回：对应模拟结果或测试退出码。 */
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    SemaphoreHandle_t s = calloc(1, sizeof *s);
    assert(s);
    s->tokens = 1;
    command_lock = s;
    return s;
}
/** 功能：创建空模拟二值信号量；参数：无；返回：对应模拟结果或测试退出码。 */
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    SemaphoreHandle_t s = calloc(1, sizeof *s);
    assert(s);
    return s;
}
/** 功能：获取模拟信号量并在等待期注入应答；参数：s，模拟信号量；wait，等待节拍数；返回：对应模拟结果或测试退出码。 */
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) {
    if (s == command_lock && wait != 0U && refuse_command_lock) {
        refuse_command_lock = false;
        return pdFALSE;
    }
    if (!s->tokens && wait && pending_reply) {
        const char *reply = pending_reply;
        pending_reply = NULL;
        receive_bytes(reply, pending_reply_length); /* 应答在阻塞发送返回后到达。 */
    }
    if (s->tokens) {
        --s->tokens;
        return pdTRUE;
    }
    tick += wait;
    return pdFALSE;
}
/** 功能：释放模拟信号量；参数：s，模拟信号量；返回：对应模拟结果或测试退出码。 */
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    s->tokens = 1;
    return pdTRUE;
}
/** 功能：模拟中断释放信号量；参数：s，模拟信号量；woken，高优先级任务唤醒标记；返回：对应模拟结果或测试退出码。 */
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken) {
    assert(*woken == pdFALSE);
    s->tokens = 1;
    return pdTRUE;
}
/** 功能：注册逐字节接收目标；参数：u，模拟串口句柄；data，发送或接收缓冲区；length，字节长度；返回：对应模拟结果或测试退出码。 */
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u, uint8_t *data, uint16_t length) {
    assert(u == &huart2 && length == 1);
    rx_target = data;
    ++arm_count;
    return HAL_OK;
}
/** 功能：取消模拟串口接收；参数：u，模拟串口句柄；返回：对应模拟结果或测试退出码。 */
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u) {
    assert(u == &huart2);
    rx_target = NULL;
    return HAL_OK;
}
/** 功能：按字符串长度注入串口应答；参数：text，文本输入；返回：无。 */
static void receive_text(const char *text) {
    receive_bytes(text, strlen(text));
}
/** 功能：逐字节执行真实接收中断回调；参数：bytes，二进制输入；length，字节长度；返回：无。 */
static void receive_bytes(const void *bytes, size_t length) {
    const uint8_t *text = bytes;
    for (size_t n = 0; n < length; ++n) {
        assert(rx_target);
        *rx_target = text[n];
        HAL_UART_RxCpltCallback(&huart2);
    }
}
/** 功能：校验真实驱动发送并模拟 ESP
 * 数据输入和应答；参数：u，模拟串口句柄；data，发送或接收缓冲区；length，字节长度；timeout，发送超时毫秒数；返回：对应模拟结果或测试退出码。
 */
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *u, uint8_t *data, uint16_t length,
                                    uint32_t timeout) {
    assert(u == &huart2 && timeout > 0 && timeout < 10000);
    if (scheduler_suspend_depth != 0U) {
        assert(timeout <= 20U);
    }
    assert(script_index < script_length);
    assert(expected_length[script_index] == length);
    assert(memcmp(expected_tx[script_index], data, length) == 0);
    if (model_data_entry) {
        if (data_entry_remaining != 0U) {
            /* ESP 收满声明的载荷长度前始终处于数据模式，不能依赖 AT+RST 退出。 */
            assert(!(length == 8U && memcmp(data, "AT+RST\r\n", 8U) == 0));
            size_t sent = transmitted_length[script_index];
            data_entry_remaining =
                sent >= data_entry_remaining ? 0U : data_entry_remaining - (unsigned)sent;
        } else if (length >= 11U && memcmp(data, "AT+CIPSEND=", 11U) == 0) {
            char *end;
            unsigned long id = strtoul((const char *)data + 11U, &end, 10);
            assert(id <= 4U && *end == ',');
            data_entry_remaining = (unsigned)strtoul(end + 1U, NULL, 10);
        } else if (length >= 19U && memcmp(data, "AT+MQTTLONGPASSWORD=", 19U) == 0) {
            const char *comma = strchr((const char *)data + 19U, ',');
            assert(comma);
            data_entry_remaining = (unsigned)strtoul(comma + 1U, NULL, 10);
        } else if (length >= 14U && memcmp(data, "AT+MQTTPUBRAW=", 14U) == 0) {
            const char *quote = strchr((const char *)data + 14U, '"');
            assert(quote);
            quote = strchr(quote + 1U, '"');
            assert(quote && quote[1] == ',');
            data_entry_remaining = (unsigned)strtoul(quote + 2U, NULL, 10);
        } else if (length >= 12U && memcmp(data, "AT+SYSMFG=2,", 12U) == 0) {
            const char *comma = strrchr((const char *)data, ',');
            assert(comma);
            data_entry_remaining = (unsigned)strtoul(comma + 1U, NULL, 10);
        }
    }
    const char *reply = rx_reply[script_index];
    HAL_StatusTypeDef result = transmit_result[script_index];
    size_t reply_size = reply_length[script_index];
    pending_reply_length = wait_reply_length[script_index];
    pending_reply = rx_wait_reply[script_index++];
    if (inject_uart_error) {
        inject_uart_error = false;
        HAL_UART_ErrorCallback(u);
    }
    /* 完整应答可能在阻塞发送返回前到达，用于检查发送后错误重置接收状态或清空信号量。 */
    if (reply) {
        receive_bytes(reply, reply_size);
    }
    return result;
}
/** 功能：追加一个带应答的期望命令；参数：tx，期望发送内容；reply，模拟应答；返回：无。 */
static void script(const char *tx, const char *reply) {
    assert(script_length < 32);
    expected_tx[script_length] = tx;
    rx_reply[script_length] = reply;
    expected_length[script_length] = strlen(tx);
    transmitted_length[script_length] = strlen(tx);
    transmit_result[script_length] = HAL_OK;
    reply_length[script_length] = reply ? strlen(reply) : 0U;
    wait_reply_length[script_length] = 0U;
    rx_wait_reply[script_length++] = NULL;
}
/** 功能：追加分两阶段到达的命令应答；参数：tx，期望发送内容；early，提前应答；late，等待期应答；返回：无。 */
static void script_delayed(const char *tx, const char *early, const char *late) {
    script(tx, early);
    rx_wait_reply[script_length - 1] = late;
    wait_reply_length[script_length - 1] = late ? strlen(late) : 0U;
}
/** 功能：追加含任意二进制内容的事务；参数：tx，期望发送内容；tx_size，发送长度；reply，模拟应答；reply_size，应答长度；返回：无。 */
static void script_binary(const void *tx, size_t tx_size, const void *reply, size_t reply_size) {
    script("", NULL);
    expected_tx[script_length - 1U] = tx;
    expected_length[script_length - 1U] = tx_size;
    transmitted_length[script_length - 1U] = tx_size;
    rx_reply[script_length - 1U] = reply;
    reply_length[script_length - 1U] = reply_size;
}
/** 功能：验证所有期望事务均已完成并清空脚本；参数：无；返回：无。 */
static void script_done(void) {
    assert(script_index == script_length && pending_reply == NULL);
    script_index = script_length = 0;
}
/** 功能：通过真实软重启握手恢复传输同步；参数：无；返回：无。 */
static void initialize(void) {
    /* 首个 OK 只确认重启命令，迟到的第二个 OK 可能属于旧操作，均不得替代 ready 握手。 */
    script_delayed("AT+RST\r\n", "OK\r\n", "OK\r\nready\r\n");
    script("ATE0\r\n", "OK\r\n");
    assert(esp_at_init());
    script_done();
}
/** 功能：配置终端初始化的持久化与自动连接应答；参数：无；返回：无。 */
static void wifi_init_prefix(void) {
    script("AT+SYSSTORE=0\r\n", "OK\r\n");
    script("AT+CWAUTOCONN?\r\n", "+CWAUTOCONN:0\r\nOK\r\n");
}
/** 功能：配置终端模式后的断开与 DHCP 应答；参数：无；返回：无。 */
static void wifi_init_suffix(void) {
    script("AT+CWRECONNCFG=0,0\r\n", "OK\r\n");
    script("AT+CWQAP\r\n", "OK\r\n");
    script("AT+CWDHCP=1,1\r\n", "OK\r\n");
}
/** 功能：验证固件查询的容量和传输故障边界；参数：无；返回：无。 */
static void firmware_diagnostics(void) {
    static const char version[] = "AT version:2.4.0.0(ESP8266 compatible firmware build with a "
                                  "version line longer than sixty-four bytes)\r\n"
                                  "SDK version:3.0.5\r\ncompile time:Nov 13 2026 12:00:00\r\n"
                                  "Bin version:2.4.0(ESP8266)\r\n\r\nOK\r\n";
    char output[512];
    script_delayed("AT+GMR\r\n", "\r\n", version);
    assert(esp_at_get_firmware_info(output, sizeof output));
    script_done();
    assert(strcmp(output, "\r\nAT version:2.4.0.0(ESP8266 compatible firmware build with a version "
                          "line longer than sixty-four bytes)\r\n"
                          "SDK version:3.0.5\r\ncompile time:Nov 13 2026 12:00:00\r\n"
                          "Bin version:2.4.0(ESP8266)\r\n\r\nOK\r\n") == 0);
    assert(esp_at_is_ready());

    /* 无效输出参数不得发送命令或改写保护字节。 */
    char guard = 'g';
    assert(!esp_at_get_firmware_info(NULL, sizeof output));
    assert(!esp_at_get_firmware_info(&guard, 0U) && guard == 'g');
    script_done();

    /** 保存输出缓冲区及两侧越界保护。 */
    struct {
        char before;   // 前置保护字节
        char text[16]; // 固件信息输出缓冲区
        char after;    // 后置保护字节
    } small;
    memset(&small, 'g', sizeof small);
    script("AT+GMR\r\n", version);
    assert(!esp_at_get_firmware_info(small.text, sizeof small.text));
    script_done();
    assert(small.text[0] == '\0' && small.before == 'g' && small.after == 'g');
    assert(esp_at_is_ready());
    char exact[sizeof "AT version:test\r\nOK\r\n"];
    script("AT+GMR\r\n", "AT version:test\r\nOK\r\n");
    assert(esp_at_get_firmware_info(exact, sizeof exact));
    script_done();
    assert(strcmp(exact, "AT version:test\r\nOK\r\n") == 0);
    script("AT+GMR\r\n", "AT version:test\r\nOK\r\n");
    assert(!esp_at_get_firmware_info(exact, sizeof exact - 1U));
    script_done();
    assert(exact[0] == '\0' && esp_at_is_ready());
    memcpy(output, "previous version", sizeof "previous version");
    script("AT+GMR\r\n", "ERROR\r\n");
    assert(!esp_at_get_firmware_info(output, sizeof output));
    script_done();
    assert(output[0] == '\0' && esp_at_is_ready());

    script("AT+GMR\r\n", NULL);
    TickType_t before = tick;
    assert(!esp_at_get_firmware_info(output, sizeof output));
    script_done();
    assert(output[0] == '\0' && !esp_at_is_ready());
    assert(tick - before >= 1000U && tick - before <= 1100U);
    memcpy(output, "previous version", sizeof "previous version");
    assert(!esp_at_get_firmware_info(output, sizeof output) && output[0] == '\0');
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_TRANSPORT_ERROR);
    script_done();
    initialize();

    static char oversized[2100];
    memset(oversized, 'v', sizeof oversized - 1U);
    script("AT+GMR\r\n", oversized);
    unsigned before_arms = arm_count;
    assert(!esp_at_get_firmware_info(output, sizeof output));
    script_done();
    assert(output[0] == '\0' && !esp_at_is_ready());
    assert(arm_count - before_arms == sizeof oversized - 1U);
    initialize();
}
/** 功能：验证 MQTT 支持探测和失败隔离；参数：无；返回：无。 */
static void mqtt_diagnostics(void) {
    script("AT+MQTTCONN?\r\n", "+MQTTCONN:0,0,0,\"\",0,\"\",0\r\nOK\r\n");
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_SUPPORTED);
    script_done();
    assert(esp_at_is_ready());
    script("AT+MQTTCONN?\r\n", "ERROR\r\n");
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_REJECTED);
    script_done();
    assert(esp_at_is_ready());
    script("AT+CIPSNTPCFG=1,8\r\n", "OK\r\n");
    assert(esp_at_sntp_init());
    script_done();
    script("AT+MQTTCONN?\r\n", "busy p...\r\n");
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_REJECTED);
    script_done();
    assert(esp_at_is_ready());

    script("AT+MQTTCONN?\r\n", NULL);
    TickType_t before = tick;
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_TRANSPORT_ERROR);
    script_done();
    assert(!esp_at_is_ready() && tick - before >= 1000U && tick - before <= 2100U);
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_TRANSPORT_ERROR);
    assert(!esp_at_sntp_init());
    script_done();
    initialize();
    script("AT+MQTTCONN?\r\n", NULL);
    inject_uart_error = true;
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_TRANSPORT_ERROR);
    script_done();
    assert(!esp_at_is_ready());
    initialize();
    script("AT+MQTTCONN?\r\n", NULL);
    transmit_result[script_length - 1U] = HAL_ERROR;
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_TRANSPORT_ERROR);
    script_done();
    assert(!esp_at_is_ready());
    initialize();
}
/** 功能：配置服务器启动应答；参数：fallback，是否使用新版本命令回退；ip，模拟输入；返回：无。 */
static void server_start_script(bool fallback, const char *ip) {
    script("AT+CIPMUX=1\r\n", "OK\r\n");
    script("AT+CIPDINFO=0\r\n", "OK\r\n");
    script("AT+CIPRECVMODE=1\r\n", fallback ? "ERROR\r\n" : "OK\r\n");
    if (fallback) {
        script("AT+CIPRECVTYPE=5,1\r\n", "OK\r\n");
    }
    script("AT+CIPSERVER=1,80\r\n", "OK\r\n");
    script("AT+CIPSTO=60\r\n", "OK\r\n");
    script("AT+CIFSR\r\n", ip);
}
/** 功能：验证服务器数据发送、接收和断线事件；参数：无；返回：无。 */
static void server_transport(void) {
    char ip[16];
    esp_server_chunk_t chunk;
    assert(esp_at_is_ready() && !esp_at_server_ready());
    server_start_script(false, "+CIFSR:STAIP,\"192.168.1.55\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    assert(strcmp(ip, "192.168.1.55") == 0 && esp_at_server_ready());
    script_done();
    script("AT+MQTTCONN?\r\n", "ERROR\r\n");
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_REJECTED);
    script_done();
    assert(esp_at_is_ready() && esp_at_server_ready());
    assert(!esp_at_server_poll(&chunk));
    /* 没有命令执行时，通知仍逐字节进入串口中断。 */
    receive_text("2,CON");
    receive_text("NECT\r\n+IPD,2,");
    receive_text("10\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CONNECTED &&
           chunk.link == 2U);
    uint32_t generation = chunk.generation;
    assert(generation != 0U);
    script("AT+CIPRECVDATA=2,10\r\n", "+CIPRECVDATA:10,\r\nOK\r\nERR!\r\nOK\r\n");
    assert(esp_at_server_poll(&chunk));
    assert(chunk.event == ESP_SERVER_EVENT_DATA && chunk.link == 2U &&
           chunk.generation == generation);
    assert(chunk.length == 10U && memcmp(chunk.data, "\r\nOK\r\nERR!", 10U) == 0);
    script_done();
    assert(!esp_at_server_poll(&chunk));
    /* 提前到达的 OK 不能替代提示符或 SEND OK。 */
    script_delayed("AT+CIPSEND=2,5\r\n", "OK\r\n", ">");
    script_delayed("hello", "OK\r\n", "Recv 5 bytes\r\nSEND OK\r\n");
    assert(esp_at_server_send(2U, generation, "hello", 5U));
    script_done();
    /* 混入 HTTPCLIENT 的主动通知仍应交给服务器任务。 */
    script("AT+HTTPCLIENT=2,1,\"https://example.com\",,,2\r\n",
           "+IPD,2,4\r\n+HTTPCLIENT:3,abc\r\nOK\r\n");
    assert(strcmp(esp_at_http_get("https://example.com"), "abc") == 0);
    script_done();
    script("AT+CIPRECVDATA=2,4\r\n", "+CIPRECVDATA:4,body\r\nOK\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.length == 4U);
    script_done();
    receive_text("2,CLOSED\r\n2,CONNECT\r\n+IPD,2,3\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CONNECTED &&
           chunk.generation != generation);
    assert(!esp_at_server_send(2U, generation, "stale", 5U));
    assert(!esp_at_server_close(2U, generation));
    generation = chunk.generation;
    script("AT+CIPRECVDATA=2,3\r\n", "+CIPRECVDATA:3,new\r\nOK\r\n");
    assert(esp_at_server_poll(&chunk) && memcmp(chunk.data, "new", 3U) == 0);
    script_done();
    script("AT+CIPCLOSE=2\r\n", "2,CLOSED\r\nOK\r\n");
    assert(esp_at_server_close(2U, generation));
    script_done();
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CLOSED);
    receive_text("ready\r\n");
    assert(!esp_at_is_ready() && !esp_at_server_ready());
    initialize();
    server_start_script(true, "+CIFSR:STAIP,\"10.0.0.8\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip) && strcmp(ip, "10.0.0.8") == 0);
    script_done();
}

/** 功能：验证载荷边界和连接编号复用隔离；参数：无；返回：无。 */
static void server_adversarial(void) {
    esp_server_chunk_t chunk;
    receive_text("0,CONNECT\r\n+IPD,0,14\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CONNECTED);
    uint32_t generation = chunk.generation;
    /* 套接字载荷包含空字节、完整 ERROR 行和伪造的 +IPD 通知。 */
    static const char binary_reply[] = "+CIPRECVDATA:14,\0\r\nERROR\r\n+IPD\r\nOK\r\n";
    const char command[] = "AT+CIPRECVDATA=0,14\r\n";
    script_binary(command, strlen(command), binary_reply, sizeof binary_reply - 1U);
    assert(esp_at_server_poll(&chunk) && chunk.length == 14U);
    assert(memcmp(chunk.data, "\0\r\nERROR\r\n+IPD", 14U) == 0);
    assert(esp_at_is_ready());
    script_done();
    /* 空闲时接收到的半行主动通知必须跨无关事务保留。 */
    receive_text("+IPD,0,");
    script("AT+CIPSNTPCFG=1,8\r\n", "3\r\nOK\r\n");
    assert(esp_at_sntp_init());
    script_done();
    script("AT+CIPRECVDATA=0,3\r\n", "+CIPRECVDATA:3,abc\r\nOK\r\n");
    assert(esp_at_server_poll(&chunk) && memcmp(chunk.data, "abc", 3U) == 0);
    script_done();
    char payload[1600];
    memset(payload, 's', sizeof payload);
    script("AT+CIPSEND=0,1460\r\n", "OK\r\n> ");
    script_binary(payload, 1460U, "SEND OK\r\n", 9U);
    script("AT+CIPSEND=0,140\r\n", "OK\r\n>");
    script_binary(payload + 1460U, 140U, "SEND OK\r\n", 9U);
    assert(esp_at_server_send(0U, generation, payload, sizeof payload));
    script_done();
    script("AT+CIPSEND=0,3\r\n", "ERROR\r\n");
    assert(!esp_at_server_send(0U, generation, "abc", 3U));
    script_done();
    assert(esp_at_is_ready() && esp_at_server_ready());
    /* 命令前静默期内连接编号复用时，绝不能关闭新连接。 */
    delay_reply = "0,CLOSED\r\n0,CONNECT\r\n";
    assert(!esp_at_server_close(0U, generation));
    script_done();
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CONNECTED);
    generation = chunk.generation;
    /* 提前到达的 OK 不能在数据帧之前完成被动读取。 */
    receive_text("+IPD,0,3\r\n");
    script_delayed("AT+CIPRECVDATA=0,3\r\n", "OK\r\n", "+CIPRECVDATA:3,xyz\r\nOK\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.length == 3U && memcmp(chunk.data, "xyz", 3U) == 0);
    script_done();
    /* 新通知的待读总长度不能重复扣除本次已读取字节。 */
    receive_text("+IPD,0,3\r\n");
    script("AT+CIPRECVDATA=0,3\r\n", "+CIPRECVDATA:3,abc\r\n+IPD,0,2\r\nOK\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.length == 3U);
    script_done();
    script("AT+CIPRECVDATA=0,2\r\n", "+CIPRECVDATA:2,de\r\nOK\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.length == 2U);
    script_done();
    /* 命令完成前连接编号被复用时，必须丢弃旧连接的数据。 */
    receive_text("+IPD,0,3\r\n");
    script("AT+CIPRECVDATA=0,3\r\n",
           "+CIPRECVDATA:3,old\r\n0,CLOSED\r\n0,CONNECT\r\n+IPD,0,2\r\nOK\r\n");
    assert(!esp_at_server_poll(&chunk));
    script_done();
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CONNECTED &&
           chunk.generation != generation);
    generation = chunk.generation;
    script("AT+CIPRECVDATA=0,2\r\n", "+CIPRECVDATA:2,hi\r\nOK\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.length == 2U);
    script_done();
    /* 接收窗口可以大于微控制器固定数据块。 */
    receive_text("+IPD,0,600\r\n");
    char frame[320], body[257];
    memset(body, 'b', sizeof body - 1U);
    body[256] = '\0';
    for (unsigned n = 0U; n < 2U; ++n) {
        (void)snprintf(frame, sizeof frame, "+CIPRECVDATA:256,%s\r\nOK\r\n", body);
        script("AT+CIPRECVDATA=0,256\r\n", frame);
        assert(esp_at_server_poll(&chunk) && chunk.length == ESP_SERVER_CHUNK_CAPACITY);
        script_done();
    }
    body[88] = '\0';
    (void)snprintf(frame, sizeof frame, "+CIPRECVDATA:88,%s\r\nOK\r\n", body);
    script("AT+CIPRECVDATA=0,88\r\n", frame);
    assert(esp_at_server_poll(&chunk) && chunk.length == 88U);
    script_done();
    assert(!esp_at_server_poll(&chunk));
    script_delayed("AT+CIPSEND=0,3\r\n", "OK\r\n", "> ");
    script("abc", "OK\r\n");
    assert(!esp_at_server_send(0U, generation, "abc", 3U));
    script_done();
    assert(!esp_at_is_ready() && !esp_at_server_ready());
}

/** 功能：验证服务器配置及传输失败处理；参数：无；返回：无。 */
static void server_failures(void) {
    char ip[16];
    esp_server_chunk_t chunk;
    initialize();
    script("AT+CIPMUX=1\r\n", "ERROR\r\n");
    assert(!esp_at_server_start(80U, ip));
    script_done();
    assert(esp_at_is_ready() && !esp_at_server_ready());
    script("AT+CIPSNTPCFG=1,8\r\n", "OK\r\n");
    assert(esp_at_sntp_init());
    script_done();
    server_start_script(false, "+CIFSR:APIP,\"192.168.4.1\"\r\nOK\r\n");
    assert(!esp_at_server_start(80U, ip));
    script_done();
    assert(esp_at_is_ready() && !esp_at_server_ready());
    server_start_script(false, "+CIFSR:STAIP,\"192.168.1.20\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    script_done();
    while (esp_at_server_poll(&chunk)) {
        assert(chunk.event == ESP_SERVER_EVENT_CLOSED);
    }
    receive_text("1,CONNECT\r\n+IPD,1,300\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.link == 1U &&
           chunk.event == ESP_SERVER_EVENT_CONNECTED);
    char frame[400];
    memset(frame, 'x', sizeof frame);
    memcpy(frame, "+CIPRECVDATA:300,", 17U);
    memcpy(frame + 317U, "\r\nOK\r\n", 6U);
    frame[323U] = '\0';
    script("AT+CIPRECVDATA=1,256\r\n", frame);
    unsigned before = arm_count;
    assert(!esp_at_server_poll(&chunk));
    script_done();
    assert(!esp_at_is_ready() && !esp_at_server_ready() && arm_count - before == strlen(frame));
    initialize();
    server_start_script(false, "+CIFSR:STAIP,\"192.168.1.20\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    script_done();
    receive_text("WIFI DISCONNECT\r\n");
    assert(esp_at_is_ready() && !esp_at_server_ready());
    assert(!esp_at_server_poll(&chunk));
}

/** 功能：验证丢失发送提示符后的数据排空；参数：无；返回：无。 */
static void server_data_entry_recovery(void) {
    char ip[16];
    esp_server_chunk_t chunk;
    static const char zeros[256] = {0};
    initialize();
    server_start_script(false, "+CIFSR:STAIP,\"192.168.1.20\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    script_done();
    while (esp_at_server_poll(&chunk)) {
        assert(chunk.event == ESP_SERVER_EVENT_CLOSED);
    }
    receive_text("0,CONNECT\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CONNECTED);
    uint32_t generation = chunk.generation;
    char body[600];
    memset(body, 'p', sizeof body);
    model_data_entry = true;
    script("AT+CIPSEND=0,600\r\n", "OK\r\n>\r\n0,CLOSED\r\n0,CONNECT\r\n");
    script_binary(zeros, 256U, NULL, 0U);
    script_binary(zeros, 256U, NULL, 0U);
    script_binary(zeros, 88U, "SEND OK\r\n", 9U);
    script("\r\n", "ERROR\r\n");
    assert(!esp_at_server_send(0U, generation, body, sizeof body));
    script_done();
    assert(data_entry_remaining == 0U && !esp_at_is_ready());
    initialize(); /* 发送重启命令前，模拟 ESP 已回到命令模式。 */
    server_start_script(false, "+CIFSR:STAIP,\"192.168.1.20\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    script_done();
    while (esp_at_server_poll(&chunk)) {
        assert(chunk.event == ESP_SERVER_EVENT_CLOSED);
    }
    receive_text("0,CONNECT\r\n");
    assert(esp_at_server_poll(&chunk));
    generation = chunk.generation;
    /* 即使观察到 OK，提示符丢失仍会使数据输入状态不确定。 */
    script("AT+CIPSEND=0,3\r\n", "OK\r\n");
    script_binary(zeros, 3U, "SEND OK\r\n", 9U);
    script("\r\n", "ERROR\r\n");
    assert(!esp_at_server_send(0U, generation, "bad", 3U));
    script_done();
    assert(data_entry_remaining == 0U && !esp_at_is_ready());
    initialize();
    model_data_entry = false;
}

/** 功能：验证数据排空失败状态跨初始化保留；参数：无；返回：无。 */
static void server_retained_data_entry(void) {
    char ip[16];
    esp_server_chunk_t chunk;
    static const char zeros[3] = {0};
    server_start_script(false, "+CIFSR:STAIP,\"192.168.1.20\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    script_done();
    while (esp_at_server_poll(&chunk)) {
        assert(chunk.event == ESP_SERVER_EVENT_CLOSED);
    }
    receive_text("0,CONNECT\r\n");
    assert(esp_at_server_poll(&chunk));
    uint32_t generation = chunk.generation;
    model_data_entry = true;
    script("AT+CIPSEND=0,3\r\n", "OK\r\n>");
    script("bad", NULL);
    transmitted_length[script_length - 1U] = 1U;
    transmit_result[script_length - 1U] = HAL_ERROR;
    script_binary(zeros, sizeof zeros, NULL, 0U);
    transmitted_length[script_length - 1U] = 1U;
    transmit_result[script_length - 1U] = HAL_ERROR;
    assert(!esp_at_server_send(0U, generation, "bad", 3U));
    script_done();
    assert(data_entry_remaining == 1U && !esp_at_is_ready());
    /* 排空失败状态必须跨初始化保留，上限填充可能进入命令模式，因此下次重启前必须发送换行。 */
    script_binary(zeros, sizeof zeros, "SEND OK\r\n", 9U);
    script("\r\n", "ERROR\r\n");
    initialize();
    assert(data_entry_remaining == 0U);
    model_data_entry = false;
}

/** 功能：验证代次校验与命令发送间的编号复用；参数：无；返回：无。 */
static void server_guard_race(void) {
    char ip[16];
    esp_server_chunk_t chunk;
    server_start_script(false, "+CIFSR:STAIP,\"192.168.1.20\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    script_done();
    while (esp_at_server_poll(&chunk)) {
        assert(chunk.event == ESP_SERVER_EVENT_CLOSED);
    }
    receive_text("1,CONNECT\r\n");
    assert(esp_at_server_poll(&chunk));
    uint32_t generation = chunk.generation;
    /* 刚离开接收准备与代次校验临界区时复用编号。 */
    critical_exit_skips = 1U;
    critical_exit_reply = "1,CLOSED\r\n1,CONNECT\r\n";
    assert(!esp_at_server_close(1U, generation));
    script_done();
    assert(esp_at_is_ready() && esp_at_server_ready());
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CONNECTED);
    generation = chunk.generation;
    static const char zeros[3] = {0};
    model_data_entry = true;
    script("AT+CIPSEND=1,3\r\n", "OK\r\n>");
    script_binary(zeros, sizeof zeros, "SEND OK\r\n", 9U);
    script("\r\n", "ERROR\r\n");
    critical_exit_skips = 3U;
    critical_exit_reply = "1,CLOSED\r\n1,CONNECT\r\n";
    assert(!esp_at_server_send(1U, generation, "old", 3U));
    script_done();
    assert(data_entry_remaining == 0U && !esp_at_is_ready());
    initialize();
    model_data_entry = false;
}

static const uint8_t mqtt_ca[] = "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";
static esp_mqtt_config_t mqtt_config = {
    "mqtt.example.com", 8883U, "clock", "product", "token", mqtt_ca, sizeof mqtt_ca, NULL};
/** 功能：配置包含服务器名称策略的 MQTT
 * 应答；参数：present，证书是否已存在；server_name，模拟输入；返回：无。 */
static void mqtt_configure_name_script(bool present, const char *server_name) {
    script("AT+SYSLOG=1\r\n", "OK\r\n");
    static char certificate[200];
    (void)snprintf(certificate, sizeof certificate, "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8,%zu,",
                   sizeof mqtt_ca);
    size_t head = strlen(certificate);
    memcpy(certificate + head, mqtt_ca, sizeof mqtt_ca);
    memcpy(certificate + head + sizeof mqtt_ca, "\r\nOK\r\n", 6U);
    if (!present) {
        script("AT+SYSMFG=1,\"mqtt_ca\",\"mqtt_ca\"\r\n", "ERROR\r\n");
        static char write[80];
        (void)snprintf(write, sizeof write, "AT+SYSMFG=2,\"mqtt_ca\",\"mqtt_ca\",8,%zu\r\n",
                       sizeof mqtt_ca);
        script_delayed(write, "OK\r\n", ">");
        script_binary(mqtt_ca, sizeof mqtt_ca, "OK\r\n", 4U);
    }
    static const char read[] = "AT+SYSMFG=1,\"mqtt_ca\",\"mqtt_ca\"\r\n";
    script_binary(read, sizeof read - 1U, certificate, head + sizeof mqtt_ca + 6U);
    script("AT+MQTTCLEAN=0\r\n", "OK\r\n");
    script("AT+MQTTUSERCFG=0,3,\"clock\",\"product\",\"\",0,0,\"\"\r\n", "OK\r\n");
    script("AT+MQTTLONGPASSWORD=0,5\r\n", "OK\r\n>");
    script("token", "OK\r\n");
    if (server_name[0] != '\0') {
        static char sni_command[180];
        (void)snprintf(sni_command, sizeof sni_command, "AT+MQTTSNI=0,\"%s\"\r\n", server_name);
        script(sni_command, "OK\r\n");
    }
    script("AT+MQTTCONNCFG=0,60,0,\"\",\"\",0,0\r\n", "OK\r\n");
}
/** 功能：配置默认 MQTT 应答；参数：present，证书是否已存在；返回：无。 */
static void mqtt_configure_script(bool present) {
    mqtt_configure_name_script(present, "mqtt.example.com");
}
/** 功能：配置 MQTT 连接和状态核验应答；参数：state，连接状态应答；返回：无。 */
static void mqtt_connect_script(const char *state) {
    script("AT+MQTTCONN=0,\"mqtt.example.com\",8883,0\r\n", "OK\r\n");
    script("AT+MQTTCONN?\r\n", state);
}
/** 功能：验证首次 MQTT 配置接受明确的已清理状态；参数：无；返回：无。 */
static void mqtt_initial_cleanup(void) {
    /* 官方 ESP32-C3 4.1.1 在尚未创建 MQTT 客户端时拒绝清理，此明确的已清理状态不能阻止首次配置。 */
    mqtt_configure_script(true);
    rx_reply[2] = "ERR CODE:0x010a6003\r\n\r\nERROR\r\n";
    reply_length[2] = strlen(rx_reply[2]);
    assert(esp_at_mqtt_configure(&mqtt_config));
    script_done();
    esp_mqtt_diagnostic_t diagnostic = esp_at_mqtt_diagnostic();
    assert(strcmp(diagnostic.step, "MQTTCONNCFG") == 0 && strcmp(diagnostic.reply, "OK") == 0 &&
           diagnostic.at_error == 0U);
}
/** 功能：验证 MQTT 各配置步骤的失败诊断；参数：无；返回：无。 */
static void mqtt_configuration_diagnostics(void) {
    static const char *const rejected[] = {
        "ERROR\r\n",
        "ERR CODE:0x010a6004\r\nERROR\r\n",
        "ERR CODE:0x00006003\r\nERROR\r\n",
        "ERR CODE:0x010a6003f\r\nERROR\r\n",
        "ERR CODE:0x010a600z\r\nERROR\r\n",
        "token=ERR CODE:0x010a6003\r\nERROR\r\n",
        "ERR CODE:0x010a6003\r\nERR CODE:0x010a6003\r\nERROR\r\n",
        "ERR CODE:0x010a6003\r\nbusy p...\r\n"};
    for (unsigned i = 0U; i < sizeof rejected / sizeof rejected[0]; ++i) {
        mqtt_configure_script(true);
        rx_reply[2] = rejected[i];
        reply_length[2] = strlen(rejected[i]);
        script_length = 3U;
        assert(!esp_at_mqtt_configure(&mqtt_config));
        script_done();
        esp_mqtt_diagnostic_t diagnostic = esp_at_mqtt_diagnostic();
        assert(strcmp(diagnostic.step, "MQTTCLEAN") == 0 && esp_at_is_ready());
        assert(strcmp(diagnostic.reply, i == 7U ? "BUSY" : "ERROR") == 0);
        assert(diagnostic.at_error == (i == 1U   ? 0x010a6004U
                                       : i == 2U ? 0x00006003U
                                       : i == 7U ? 0x010a6003U
                                                 : 0U));
        assert(esp_at_mqtt_last_error() == ESP_MQTT_ERROR_COMMAND);
    }
    /* 没有结束 ERROR、发生重启或串口状态不明时，均不得当作已清理。 */
    mqtt_configure_script(true);
    rx_reply[2] = "ERR CODE:0x010a6003\r\n";
    reply_length[2] = strlen(rx_reply[2]);
    script_length = 3U;
    assert(!esp_at_mqtt_configure(&mqtt_config));
    script_done();
    esp_mqtt_diagnostic_t diagnostic = esp_at_mqtt_diagnostic();
    assert(strcmp(diagnostic.reply, "TIMEOUT") == 0 && !esp_at_is_ready());
    initialize();
    /* 清理命令的例外不能用于用户配置、口令或服务器名称命令。 */
    static const unsigned failure_index[] = {0U, 3U, 4U, 5U, 6U, 7U};
    static const char *const failure_step[] = {
        "SYSLOG",  "MQTTUSERCFG", "MQTTLONGPASSWORD_PROMPT", "MQTTLONGPASSWORD_DATA",
        "MQTTSNI", "MQTTCONNCFG"};
    for (unsigned i = 0U; i < sizeof failure_index / sizeof failure_index[0]; ++i) {
        unsigned slot = failure_index[i];
        mqtt_configure_script(true);
        rx_reply[slot] = "ERR CODE:0x010a6003\r\nERROR\r\n";
        reply_length[slot] = strlen(rx_reply[slot]);
        script_length = slot + 1U;
        assert(!esp_at_mqtt_configure(&mqtt_config));
        script_done();
        diagnostic = esp_at_mqtt_diagnostic();
        assert(strcmp(diagnostic.step, failure_step[i]) == 0 &&
               strcmp(diagnostic.reply, "ERROR") == 0);
        assert(diagnostic.at_error == 0x010a6003U && esp_at_is_ready());
    }
    script("AT+CIPSNTPCFG=1,8\r\n", "OK\r\n");
    assert(esp_at_sntp_init());
    script_done();
}
/** 功能：验证证书与服务器名称覆盖策略；参数：无；返回：无。 */
static void mqtt_server_name_policy(void) {
    esp_mqtt_config_t config = mqtt_config;
    /* OneNET
     * 专用官方证书可认证服务，但不包含产品域名；显式省略服务器名称，保留证书回读与方案三验证。 */
    config.tls_server_name = "";
    mqtt_configure_name_script(true, "");
    assert(esp_at_mqtt_configure(&config));
    script_done();
    mqtt_connect_script("+MQTTCONN:0,5,3,\"mqtt.example.com\",8883,\"\",0\r\nOK\r\n");
    assert(esp_at_mqtt_connect(3000U));
    script_done();
    /* 其他代理保留现有主机名称，除非调用方明确覆盖。 */
    config.tls_server_name = "verified.example.com";
    mqtt_configure_name_script(true, "verified.example.com");
    assert(esp_at_mqtt_configure(&config));
    script_done();
    config.tls_server_name = NULL;
    mqtt_configure_script(true);
    assert(esp_at_mqtt_configure(&config));
    script_done();
    config.tls_server_name = "bad\"name";
    assert(!esp_at_mqtt_configure(&config) && esp_at_mqtt_last_error() == ESP_MQTT_ERROR_ARGUMENT);
    script_done();
}
/** 功能：验证真实带引号端口格式及目标核验；参数：无；返回：无。 */
static void mqtt_state_quoted_port(void) {
    mqtt_configure_script(true);
    assert(esp_at_mqtt_configure(&mqtt_config));
    script_done();
    /* 使用 ESP32-C3 4.1.1 的真实格式，主机、端口和路径均带引号。 */
    mqtt_connect_script("+MQTTCONN:0,4,3,\"mqtt.example.com\",\"8883\",\"\",0\r\nOK\r\n");
    assert(esp_at_mqtt_connect(3000U) && esp_at_mqtt_connected());
    script_done();
    mqtt_connect_script("+MQTTCONN:0,5,3,\"mqtt.example.com\",\"8883\",\"\",0\r\nOK\r\n");
    assert(esp_at_mqtt_connect(3000U) && esp_at_mqtt_connected());
    script_done();
    mqtt_connect_script("+MQTTCONN:0,6,3,\"mqtt.example.com\",\"8883\",\"\",0\r\nOK\r\n");
    assert(esp_at_mqtt_connect(3000U) && esp_at_mqtt_connected());
    script_done();
    static const char *const rejected[] = {
        "+MQTTCONN:0,5,3,\"mqtt.example.com\",\"8884\",\"\",0\r\nOK\r\n",
        "+MQTTCONN:0,5,3,\"mqtt.example.com\",\"8883x\",\"\",0\r\nOK\r\n",
        "+MQTTCONN:0,5,3,\"mqtt.example.com\",\"8883,\"\",0\r\nOK\r\n",
        "+MQTTCONN:0,5,3,\"mqtt.example.com\",\"\",\"\",0\r\nOK\r\n",
        "+MQTTCONN:0,5,3,\"other.example.com\",\"8883\",\"\",0\r\nOK\r\n",
        "+MQTTCONN:0,5,2,\"mqtt.example.com\",\"8883\",\"\",0\r\nOK\r\n",
        "+MQTTCONN:0,3,3,\"mqtt.example.com\",\"8883\",\"\",0\r\nOK\r\n"};
    for (unsigned i = 0U; i < sizeof rejected / sizeof rejected[0]; ++i) {
        mqtt_connect_script(rejected[i]);
        assert(!esp_at_mqtt_connect(3000U) && !esp_at_mqtt_connected());
        script_done();
        assert(esp_at_is_ready());
    }
}
/** 功能：验证 MQTT 消息与其他传输交错处理；参数：无；返回：无。 */
static void mqtt_transport(void) {
    /* 普通连接 OK 不能将未连接会话标记为在线。 */
    mqtt_configure_script(false);
    assert(esp_at_mqtt_configure(&mqtt_config));
    script_done();
    mqtt_connect_script("+MQTTCONN:0,3,3,\"mqtt.example.com\",8883,\"\",0\r\nOK\r\n");
    assert(!esp_at_mqtt_connect(3000U) && !esp_at_mqtt_connected());
    script_done();
    mqtt_configure_script(true);
    assert(esp_at_mqtt_configure(&mqtt_config));
    script_done();
    mqtt_connect_script("+MQTTCONN:0,5,3,\"mqtt.example.com\",8883,\"\",0\r\nOK\r\n");
    assert(esp_at_mqtt_connect(3000U) && esp_at_mqtt_connected());
    script_done();
    script("AT+MQTTSUB=0,\"set\",1\r\n", "OK\r\n");
    assert(esp_at_mqtt_subscribe("set", 1U));
    script_done();
    receive_text("+MQTTSUBRECV:0,\"set\",12,");
    assert(esp_at_mqtt_pending());
    static const char binary[] = "\0\r\nOK\r\nERROR";
    receive_bytes(binary, 12U);
    receive_text("\r\n");
    esp_mqtt_message_t message;
    assert(esp_at_mqtt_poll(&message) && message.length == 12U);
    assert(strcmp(message.topic, "set") == 0 && memcmp(message.data, binary, 12U) == 0);
    assert(message.received_ms == tick);
    assert(!esp_at_mqtt_pending() && !esp_at_mqtt_poll(&message));
    /* 半帧主动通知的头部与载荷必须跨无关命令保留。 */
    receive_text("+MQTTSUBRECV:0,\"se");
    script("AT+CIPSNTPCFG=1,8\r\n", "t\",3,abc\r\nOK\r\n");
    assert(esp_at_sntp_init());
    script_done();
    assert(esp_at_mqtt_poll(&message) && message.length == 3U &&
           memcmp(message.data, "abc", 3U) == 0);
    script("AT+HTTPCGET=\"https://example.com\",2048,2048,2000\r\n",
           "+MQTTSUBRECV:0,\"set\",3,xyz\r\n+HTTPCGET:3,hey\r\nOK\r\n");
    const char *body = esp_at_http_get_bounded("https://example.com", 2000U);
    assert(body && strcmp(body, "hey") == 0);
    script_done();
    assert(esp_at_mqtt_poll(&message) && memcmp(message.data, "xyz", 3U) == 0);
    char ip[16];
    esp_server_chunk_t socket;
    server_start_script(false, "+CIFSR:STAIP,\"192.168.1.20\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    script_done();
    while (esp_at_server_poll(&socket)) {
        assert(socket.event == ESP_SERVER_EVENT_CLOSED);
    }
    receive_text("0,CONNECT\r\n+IPD,0,3\r\n");
    assert(esp_at_server_poll(&socket) && socket.event == ESP_SERVER_EVENT_CONNECTED);
    script("AT+CIPRECVDATA=0,3\r\n",
           "+MQTTSUBRECV:0,\"set\",3,mqt\r\n+CIPRECVDATA:3,tcp\r\nOK\r\n");
    assert(esp_at_server_poll(&socket) && socket.length == 3U &&
           memcmp(socket.data, "tcp", 3U) == 0);
    script_done();
    assert(esp_at_mqtt_poll(&message) && message.length == 3U &&
           memcmp(message.data, "mqt", 3U) == 0);
    /* 异步 MQTT 前缀尚未被识别时，不得挤占合法大天气应答的剩余缓冲空间。 */
    static char large_http[2200];
    const char http_prefix[] = "+HTTPCGET:2018,";
    memcpy(large_http, http_prefix, sizeof http_prefix - 1U);
    memset(large_http + sizeof http_prefix - 1U, 'h', 2018U);
    static const char http_tail[] = "\r\n+MQTTSUBRECV:0,\"set\",3,xyz\r\nOK\r\n";
    memcpy(large_http + sizeof http_prefix - 1U + 2018U, http_tail, sizeof http_tail);
    script("AT+HTTPCGET=\"https://example.com\",2048,2048,2000\r\n", large_http);
    body = esp_at_http_get_bounded("https://example.com", 2000U);
    assert(body && strlen(body) == 2018U && esp_at_is_ready());
    script_done();
    assert(esp_at_mqtt_poll(&message) && message.length == 3U &&
           memcmp(message.data, "xyz", 3U) == 0);
    /* 发布完成仅由 +MQTTPUB:OK 确认，普通 OK 不足以确认。 */
    script_delayed("AT+MQTTPUBRAW=0,\"reply\",3,1,0\r\n", "OK\r\n", ">");
    script_delayed("abc", "OK\r\n", "+MQTTPUB:OK\r\n");
    assert(esp_at_mqtt_publish("reply", "abc", 3U, 1U, false));
    script_done();
    assert(strlen(body) == 2018U);
    script("AT+MQTTPUBRAW=0,\"reply\",3,1,0\r\n", "OK\r\n>");
    script("abc", "+MQTTPUB:FAIL\r\n");
    assert(!esp_at_mqtt_publish("reply", "abc", 3U, 1U, false));
    script_done();
    assert(esp_at_is_ready());
    script("AT+MQTTPUBRAW=0,\"reply\",3,0,0\r\n", "OK\r\n>");
    script("abc", "+MQTTDISCONNECTED:0\r\n+MQTTPUB:OK\r\n");
    assert(!esp_at_mqtt_publish("reply", "abc", 3U, 0U, false));
    script_done();
    assert(esp_at_mqtt_last_error() == ESP_MQTT_ERROR_DISCONNECTED && esp_at_is_ready());
    receive_text("+MQTTDISCONNECTED:0\r\n");
    assert(!esp_at_mqtt_connected() && esp_at_is_ready());
    mqtt_configure_script(true);
    assert(esp_at_mqtt_configure(&mqtt_config));
    script_done();
    mqtt_connect_script("\r\n\r\n+MQTTCONN:0,5,3,\"mqtt.example.com\",8883,\"\",0\r\nOK\r\n");
    assert(esp_at_mqtt_connect(3000U));
    script_done();
    for (unsigned n = 0U; n < 4U; ++n) {
        receive_text("+MQTTSUBRECV:0,\"set\",1,x\r\n");
    }
    assert(!esp_at_is_ready() && !esp_at_mqtt_connected());
    assert(esp_at_mqtt_last_error() == ESP_MQTT_ERROR_QUEUE_OVERFLOW);
    assert(!esp_at_mqtt_poll(&message));
    initialize();
}

/** 功能：验证证书、原始消息和缓冲上限；参数：无；返回：无。 */
static void mqtt_adversarial(void) {
    esp_mqtt_config_t config = mqtt_config;
    config.ca_pem = NULL;
    assert(!esp_at_mqtt_configure(&config) && esp_at_mqtt_last_error() == ESP_MQTT_ERROR_ARGUMENT);
    assert(!esp_at_mqtt_connect(0U) &&
           !esp_at_mqtt_publish("reply", "x", ESP_MQTT_PAYLOAD_CAPACITY + 1U, 0U, false));
    script_done();
    /* 逐字节核验证书回读，包括空字节及类似结束应答的文本，无需把四千字节证书装入两千字节应答缓冲。
     */
    static uint8_t ca[ESP_MQTT_CA_LIMIT];
    static char readback[ESP_MQTT_CA_LIMIT + 100U];
    memset(ca, 'c', sizeof ca);
    memcpy(ca + 90U, "\0\r\nERROR\r\n", 10U);
    const char ca_prefix[] = "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8,4096,";
    memcpy(readback, ca_prefix, sizeof ca_prefix - 1U);
    memcpy(readback + sizeof ca_prefix - 1U, ca, sizeof ca);
    memcpy(readback + sizeof ca_prefix - 1U + sizeof ca, "\r\nOK\r\n", 6U);
    config = mqtt_config;
    config.ca_pem = ca;
    config.ca_length = sizeof ca;
    static char token[ESP_MQTT_TOKEN_LIMIT + 1U];
    memset(token, 't', sizeof token - 1U);
    token[sizeof token - 1U] = '\0';
    config.password = token;
    static const char read[] = "AT+SYSMFG=1,\"mqtt_ca\",\"mqtt_ca\"\r\n";
    script("AT+SYSLOG=1\r\n", "OK\r\n");
    script_binary(read, sizeof read - 1U, readback, sizeof ca_prefix - 1U + sizeof ca + 6U);
    script("AT+MQTTCLEAN=0\r\n", "OK\r\n");
    script("AT+MQTTUSERCFG=0,3,\"clock\",\"product\",\"\",0,0,\"\"\r\n", "OK\r\n");
    script_delayed("AT+MQTTLONGPASSWORD=0,1024\r\n", "OK\r\n", ">");
    script(token, "OK\r\n");
    script("AT+MQTTSNI=0,\"mqtt.example.com\"\r\n", "OK\r\n");
    script("AT+MQTTCONNCFG=0,60,0,\"\",\"\",0,0\r\n", "OK\r\n");
    assert(esp_at_mqtt_configure(&config));
    script_done();
    mqtt_connect_script("+MQTTCONN:0,5,2,\"mqtt.example.com\",8883,\"\",0\r\nOK\r\n");
    assert(!esp_at_mqtt_connect(3000U) && !esp_at_mqtt_connected());
    script_done();
    mqtt_connect_script(
        "+MQTTCONN:0,5,3,\"mqtt.example.com\",8883,\"\",0\r\n+MQTTDISCONNECTED:0\r\nOK\r\n");
    assert(!esp_at_mqtt_connect(3000U) && !esp_at_mqtt_connected());
    script_done();
    /* 证书不存在时先写入，随后比较失败必须阻止用户配置和代理通信，同时保留本地 AT 可用性。 */
    script("AT+SYSLOG=1\r\n", "OK\r\n");
    script(read, "ERROR\r\n");
    static char write[80];
    (void)snprintf(write, sizeof write, "AT+SYSMFG=2,\"mqtt_ca\",\"mqtt_ca\",8,%zu\r\n",
                   sizeof mqtt_ca);
    script(write, "OK\r\n>");
    script_binary(mqtt_ca, sizeof mqtt_ca, "OK\r\n", 4U);
    script(read, "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8,3,bad\r\nOK\r\n");
    assert(!esp_at_mqtt_configure(&mqtt_config));
    script_done();
    assert(esp_at_is_ready() && esp_at_mqtt_last_error() == ESP_MQTT_ERROR_TLS_CA);
    script("AT+CIPSNTPCFG=1,8\r\n", "OK\r\n");
    assert(esp_at_sntp_init());
    script_done();
    mqtt_configure_script(true);
    assert(esp_at_mqtt_configure(&mqtt_config));
    script_done();
    mqtt_connect_script("+MQTTCONN:0,5,3,\"mqtt.example.com\",8883,\"\",0\r\nOK\r\n");
    assert(esp_at_mqtt_connect(3000U));
    script_done();
    /* 最大原始发布应保留二进制内容和接收帧边界。 */
    static uint8_t payload[ESP_MQTT_PAYLOAD_CAPACITY];
    memset(payload, 'p', sizeof payload);
    script("AT+MQTTPUBRAW=0,\"reply\",1536,0,0\r\n", "OK\r\n>");
    script_binary(payload, sizeof payload, "+MQTTPUB:OK\r\n", 13U);
    assert(esp_at_mqtt_publish("reply", payload, sizeof payload, 0U, false));
    script_done();
    receive_text("+MQTTSUBRECV:0,\"set\",1537,");
    receive_bytes(payload, sizeof payload);
    receive_text("x\r\nOK\r\n");
    assert(!esp_at_is_ready() && !esp_at_mqtt_connected());
    initialize();
    mqtt_configure_script(true);
    assert(esp_at_mqtt_configure(&mqtt_config));
    script_done();
    mqtt_connect_script("+MQTTCONN:0,5,3,\"mqtt.example.com\",8883,\"\",0\r\nOK\r\n");
    assert(esp_at_mqtt_connect(3000U));
    script_done();
    /* 普通 OK 不确认 MQTT 发布，必须要求重新同步，防止迟到完成行确认后续命令。 */
    script("AT+MQTTPUBRAW=0,\"reply\",3,0,0\r\n", "OK\r\n>");
    script("abc", "OK\r\n");
    assert(!esp_at_mqtt_publish("reply", "abc", 3U, 0U, false));
    script_done();
    assert(!esp_at_is_ready());
    initialize();
}

/** 功能：验证 MQTT 长数据输入状态恢复；参数：无；返回：无。 */
static void mqtt_data_entry_recovery(void) {
    static const char zeros[256] = {0};
    static const char read[] = "AT+SYSMFG=1,\"mqtt_ca\",\"mqtt_ca\"\r\n";
    /* 长口令提示符丢失后，即使此前收到普通 OK，也必须先排空数据输入再发送重启命令。 */
    static char certificate[200];
    (void)snprintf(certificate, sizeof certificate, "+SYSMFG:\"mqtt_ca\",\"mqtt_ca\",8,%zu,",
                   sizeof mqtt_ca);
    size_t head = strlen(certificate);
    memcpy(certificate + head, mqtt_ca, sizeof mqtt_ca);
    memcpy(certificate + head + sizeof mqtt_ca, "\r\nOK\r\n", 6U);
    script("AT+SYSLOG=1\r\n", "OK\r\n");
    script_binary(read, sizeof read - 1U, certificate, head + sizeof mqtt_ca + 6U);
    script("AT+MQTTCLEAN=0\r\n", "OK\r\n");
    script("AT+MQTTUSERCFG=0,3,\"clock\",\"product\",\"\",0,0,\"\"\r\n", "OK\r\n");
    script("AT+MQTTLONGPASSWORD=0,5\r\n", "OK\r\n");
    script_binary(zeros, 5U, "OK\r\n", 4U);
    script("\r\n", "ERROR\r\n");
    model_data_entry = true;
    assert(!esp_at_mqtt_configure(&mqtt_config));
    script_done();
    assert(data_entry_remaining == 0U && !esp_at_is_ready());
    initialize();
    /* 证书部分发送后仍保留完整上限债务，填充可能进入命令模式，因此必须以换行结束。 */
    script("AT+SYSLOG=1\r\n", "OK\r\n");
    script(read, "ERROR\r\n");
    static char write[80];
    (void)snprintf(write, sizeof write, "AT+SYSMFG=2,\"mqtt_ca\",\"mqtt_ca\",8,%zu\r\n",
                   sizeof mqtt_ca);
    script(write, "OK\r\n>");
    script_binary(mqtt_ca, sizeof mqtt_ca, NULL, 0U);
    transmitted_length[script_length - 1U] = 2U;
    transmit_result[script_length - 1U] = HAL_ERROR;
    script_binary(zeros, sizeof mqtt_ca, "OK\r\n", 4U);
    script("\r\n", "ERROR\r\n");
    assert(!esp_at_mqtt_configure(&mqtt_config));
    script_done();
    assert(data_entry_remaining == 0U && !esp_at_is_ready());
    initialize();
    model_data_entry = false;
}

/** 功能：验证旧 NVS 自动连接的禁用及加入期限；参数：无；返回：无。 */
static void wifi_recovery(void) {
    script("AT+SYSSTORE=0\r\n", "OK\r\n");
    script("AT+CWAUTOCONN?\r\n", "+CWAUTOCONN:1\r\nOK\r\n");
    script("AT+CWAUTOCONN=0\r\n", "OK\r\n");
    script("AT+CWMODE=1,0\r\n", "OK\r\n");
    wifi_init_suffix();
    assert(esp_at_wifi_init());
    script_done();
    script("AT+SYSSTORE=0\r\n", "OK\r\n");
    script("AT+CWAUTOCONN?\r\n", "+CWAUTOCONN:10\r\nOK\r\n");
    assert(!esp_at_wifi_init());
    script_done();
    initialize();
    script("AT+CWJAP=\"missing\",\"bad-password\",,0,0,3,0,15\r\n", NULL);
    TickType_t before = tick;
    assert(!esp_at_connect_wifi("missing", "bad-password", NULL));
    assert(tick - before >= 20000U && tick - before <= 20300U && !esp_at_is_ready());
    script_done();
    initialize();
    script("AT+CWJAP=\"missing\",\"bad-password\",,0,0,3,0,15\r\n", "busy p...\r\n");
    assert(!esp_at_connect_wifi("missing", "bad-password", NULL) && esp_at_is_ready());
    script_done();
    char too_long[65];
    memset(too_long, 'x', sizeof too_long - 1U);
    too_long[sizeof too_long - 1U] = '\0';
    assert(!esp_at_connect_wifi("missing", too_long, NULL));
    script_done();
}

/** 功能：建立确定地址和 DHCP 范围的临时热点应答；参数：地址应答；返回：无。 */
static void setup_script(const char *address) {
    script("AT+SYSSTORE=0\r\n", "OK\r\n");
    script("AT+CWQAP\r\n", "WIFI DISCONNECT\r\nOK\r\n");
    script("AT+CWMODE=3,0\r\n", "OK\r\n");
    script("AT+CWSAP=\"Clock-Setup\",\"12345678\",6,3,4,0\r\n", "OK\r\n");
    script("AT+CWDHCP=0,2\r\n", "OK\r\n");
    script("AT+CIPAP=\"192.168.4.1\",\"192.168.4.1\",\"255.255.255.0\"\r\n", "OK\r\n");
    script("AT+CWDHCP=1,3\r\n", "OK\r\n");
    script("AT+CWDHCPS=1,120,\"192.168.4.2\",\"192.168.4.20\"\r\n", "OK\r\n");
    script("AT+CIFSR\r\n", address);
}
/** 功能：验证配网服务的终端断线隔离、地址选择及关闭顺序；参数：无；返回：无。 */
static void setup_transport(void) {
    bool connected = true;
    char ip[16];
    esp_server_chunk_t chunk;
    assert(!esp_at_setup_has_client(&connected) && !connected);
    assert(!esp_at_setup_start("", "12345678"));
    assert(!esp_at_setup_start("Clock", "short"));
    assert(!esp_at_setup_start("Clock", "1234567\n"));
    script_done();
    setup_script("+CIFSR:APIP,\"192.168.4.1\"\r\n+CIFSR:STAIP,\"0.0.0.0\"\r\nOK\r\n");
    assert(esp_at_setup_start("Clock-Setup", "12345678"));
    script_done();
    server_start_script(false,
                        "+CIFSR:APIP,\"192.168.4.1\"\r\n+CIFSR:STAIP,\"192.168.1.9\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip) && strcmp(ip, "192.168.4.1") == 0);
    script_done();
    while (esp_at_server_poll(&chunk)) {
        assert(chunk.event == ESP_SERVER_EVENT_CLOSED);
    }
    receive_text("0,CONNECT\r\n");
    assert(esp_at_server_poll(&chunk) && chunk.event == ESP_SERVER_EVENT_CONNECTED);
    uint32_t generation = chunk.generation;
    receive_text("WIFI DISCONNECT\r\n");
    assert(esp_at_server_ready() && esp_at_is_ready());
    script("AT+CIPSEND=0,3\r\n", "OK\r\n>");
    script("hey", "SEND OK\r\n");
    assert(esp_at_server_send(0U, generation, "hey", 3U));
    script_done();
    script("AT+CWLIF\r\n", "OK\r\n");
    assert(esp_at_setup_has_client(&connected) && !connected);
    script_done();
    script("AT+CWLIF\r\n", "+CWLIF:192.168.4.2,00:11:22:33:44:55\r\nOK\r\n");
    assert(esp_at_setup_has_client(&connected) && connected);
    script_done();
    script("AT+CWLIF\r\n", "+CWLIF:192.168.4.2,bad\r\nOK\r\n");
    assert(!esp_at_setup_has_client(&connected) && !connected && esp_at_server_ready());
    script_done();
    script("AT+CWJAP=\"missing\",\"bad-password\",,0,0,3,0,15\r\n",
           "WIFI DISCONNECT\r\n+CWJAP:2\r\nERROR\r\n");
    assert(!esp_at_connect_wifi("missing", "bad-password", NULL));
    script_done();
    assert(esp_at_server_ready() && esp_at_is_ready());
    script("AT+CIPSERVER=0,1\r\n", "0,CLOSED\r\nOK\r\n");
    script("AT+CWQAP\r\n", "WIFI DISCONNECT\r\nOK\r\n");
    assert(esp_at_disconnect_wifi() && !esp_at_server_ready() && esp_at_is_ready());
    assert(!esp_at_server_send(0U, generation, "hey", 3U));
    script_done();
    server_start_script(false, "+CIFSR:APIP,\"192.168.4.1\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip) && strcmp(ip, "192.168.4.1") == 0);
    script_done();
    script("AT+CIPSERVER=0,1\r\n", "0,CLOSED\r\nOK\r\n");
    script("AT+CWMODE=1,0\r\n", "OK\r\n");
    assert(esp_at_setup_stop() && !esp_at_server_ready());
    assert(!esp_at_server_send(0U, generation, "hey", 3U));
    assert(!esp_at_setup_has_client(&connected) && !connected);
    script_done();
    setup_script("+CIFSR:APIP,\"192.168.4.1\"\r\nOK\r\n");
    assert(esp_at_setup_start("Clock-Setup", "12345678"));
    script_done();
    receive_text("ready\r\n");
    assert(!esp_at_is_ready() && !esp_at_server_ready());
    assert(!esp_at_setup_has_client(&connected) && !connected);
    script_done();
    initialize();
    setup_script("+CIFSR:APIP,\"192.168.4.1\"\r\nOK\r\n");
    assert(esp_at_setup_start("Clock-Setup", "12345678"));
    script_done();
    script("AT+CWQAP\r\n", "ERROR\r\n");
    assert(!esp_at_disconnect_wifi() && !esp_at_is_ready() && !esp_at_server_ready());
    script_done();
    assert(!esp_at_setup_has_client(&connected) && !connected);
    initialize();
    setup_script("+CIFSR:APIP,\"192.168.8.1\"\r\nOK\r\n");
    assert(!esp_at_setup_start("Clock-Setup", "12345678"));
    script_done();
    assert(!esp_at_setup_has_client(&connected) && !connected);
    initialize();
    setup_script("+CIFSR:APIP,\"192.168.4.1\"\r\nOK\r\n");
    assert(esp_at_setup_start("Clock-Setup", "12345678"));
    script_done();
    script("AT+CIPSERVER=0,1\r\n", "ERROR\r\n");
    assert(!esp_at_setup_stop() && !esp_at_server_ready() && !esp_at_is_ready());
    script_done();
    initialize();
    setup_script("+CIFSR:APIP,\"192.168.4.1\"\r\nOK\r\n");
    assert(esp_at_setup_start("Clock-Setup", "12345678"));
    script_done();
    script("AT+CWLIF\r\n", NULL);
    TickType_t before = tick;
    assert(!esp_at_setup_has_client(&connected) && !connected);
    assert(!esp_at_is_ready() && tick - before <= 2300U);
    script_done();
    initialize();
}

/** 功能：验证清理失败时禁止继续使用热点或终端传输；参数：无；返回：无。 */
static void disconnect_failures(void) {
    char ip[16];
    setup_script("+CIFSR:APIP,\"192.168.4.1\"\r\nOK\r\n");
    assert(esp_at_setup_start("Clock-Setup", "12345678"));
    script_done();
    server_start_script(false, "+CIFSR:APIP,\"192.168.4.1\"\r\nOK\r\n");
    assert(esp_at_server_start(80U, ip));
    script_done();
    script("AT+CIPSERVER=0,1\r\n", "ERROR\r\n");
    assert(!esp_at_disconnect_wifi() && !esp_at_is_ready());
    script_done();
    initialize();
    script("AT+SYSSTORE=0\r\n", "OK\r\n");
    script("AT+CWQAP\r\n", "ERROR\r\n");
    assert(!esp_at_setup_start("Clock-Setup", "12345678") && !esp_at_is_ready());
    script_done();
    initialize();
    refuse_command_lock = true;
    assert(!esp_at_disconnect_wifi() && !esp_at_is_ready());
    script_done();
    initialize();
}

/** 功能：执行全部传输和重启模式回归；参数：argc，命令行参数数量；argv，命令行参数数组；返回：对应模拟结果或测试退出码。 */
int main(int argc, char **argv) {
    /* 仅复位微控制器会清空静态内存，但 ESP
     * 可能仍等待最大长度证书；首次重启命令前必须在没有内存债务记录的情况下恢复。 */
    static const char boot_zeros[256] = {0};
    bool boot_in_data_mode = argc <= 1;
    if (!boot_in_data_mode) {
        assert(argc == 2 && strcmp(argv[1], "--command-mode-boot") == 0);
    }
    model_data_entry = true;
    data_entry_remaining = boot_in_data_mode ? ESP_MQTT_CA_LIMIT : 0U;
    for (unsigned n = 0U; n < 16U; ++n) {
        script_binary(boot_zeros, 256U, n == 15U && boot_in_data_mode ? "SEND OK\r\n" : NULL,
                      n == 15U && boot_in_data_mode ? 9U : 0U);
    }
    script("\r\n", "ERROR\r\n");
    script_delayed("AT+RST\r\n", "OK\r\n", "ready\r\n");
    script("ATE0\r\n", "OK\r\n");
    assert(esp_at_init());
    script_done();
    assert(data_entry_remaining == 0U);
    model_data_entry = false;
    script_delayed("AT+RST\r\n", NULL, "OK\r\n");
    assert(!esp_at_init());
    script_done(); /* 过期 OK 不得完成同步。 */
    initialize();
    script("AT+RST\r\n", "busy p...\r\n");
    script("AT+RST\r\n", "ERROR\r\n");
    script_delayed("AT+RST\r\n", "OK\r\n", "ready\r\n");
    script("ATE0\r\n", "OK\r\n");
    assert(esp_at_init());
    script_done(); /* 明确拒绝只允许有限次数重试。 */
    firmware_diagnostics();
    mqtt_diagnostics();
    wifi_init_prefix();
    script("AT+CWMODE=1,0\r\n", "busy p...\r\n");
    script("AT+CWMODE=1,0\r\n", "OK\r\n");
    wifi_init_suffix();
    assert(esp_at_wifi_init());
    script_done();
    script("AT+CWJAP=\"a\\\"b\",\"p\\\\q\",\"00:11:22:33:44:55\",0,0,3,0,15\r\n",
           "WIFI CONNECTED\r\nWIFI GOT IP\r\nOK\r\n");
    assert(esp_at_connect_wifi("a\"b", "p\\q", "00:11:22:33:44:55"));
    script_done();
    assert(!esp_at_connect_wifi("AP\r\nAT+RESTORE", "pw", NULL));
    script("AT+CWSTATE?\r\n", "+CWSTATE:2,\"Clock AP\"\r\nOK\r\n");
    script("AT+CWJAP?\r\n", "+CWJAP:\"Clock AP\",\"00:11:22:33:44:55\",6,-48\r\nOK\r\n");
    esp_wifi_info_t wifi;
    assert(esp_at_get_wifi_info(&wifi));
    assert(wifi.connected && wifi.channel == 6);
    script_done();
    script("AT+CIPSNTPCFG=1,8\r\n", "OK\r\n");
    assert(esp_at_sntp_init());
    script_done();
    script("AT+CIPSNTPTIME?\r\n", "+CIPSNTPTIME:Thu Feb 29 23:59:59 2024\r\nOK\r\n");
    esp_date_time_t date;
    assert(esp_at_sntp_get_time(&date));
    assert(date.year == 2024 && date.day == 29);
    script_done();
    script("AT+HTTPCLIENT=2,1,\"https://example.com\",,,2\r\n",
           "+HTTPCLIENT:8,\r\nOK\r\nx!\r\nOK\r\n");
    const char *http = esp_at_http_get("https://example.com");
    assert(http && strcmp(http, "\r\nOK\r\nx!") == 0);
    script_done();
    wifi_init_prefix();
    script("AT+CWMODE=1,0\r\n", "OK\r\n");
    wifi_init_suffix();
    assert(esp_at_wifi_init());
    script_done();
    assert(strstr(http, "x!")); /* 非 HTTP 命令不得改写正文快照。 */
    char firmware[64];
    script("AT+GMR\r\n", "AT version:test\r\nOK\r\n");
    assert(esp_at_get_firmware_info(firmware, sizeof firmware));
    script_done();
    script("AT+MQTTCONN?\r\n", "ERROR\r\n");
    assert(esp_at_probe_mqtt() == ESP_MQTT_PROBE_REJECTED);
    script_done();
    assert(strcmp(http, "\r\nOK\r\nx!") == 0);
    /* 正常固件可能在 JSON 字符串内部拆帧，天气解析前必须拼接正文而非插入帧头。 */
    const char first[] = "{\"results\":[{\"location\":{\"name\":\"Hefei\",\"path\":\"China,Hefei\"}"
                         ",\"now\":{\"text\":\"Cl";
    const char second[] = "oudy\",\"code\":\"4\",\"temperature\":\"23.5\"}}]}";
    char framed[512];
    (void)snprintf(framed, sizeof framed, "+HTTPCLIENT:%zu,%s\r\n+HTTPCLIENT:%zu,%s\r\nOK\r\n",
                   strlen(first), first, strlen(second), second);
    script("AT+HTTPCLIENT=2,1,\"https://example.com\",,,2\r\n", framed);
    http = esp_at_http_get("https://example.com");
    assert(http &&
           strcmp(http,
                  "{\"results\":[{\"location\":{\"name\":\"Hefei\",\"path\":\"China,Hefei\"},"
                  "\"now\":{\"text\":\"Cloudy\",\"code\":\"4\",\"temperature\":\"23.5\"}}]}") == 0);
    weather_info_t weather;
    assert(parse_seniverse_response(http, &weather));
    assert(strcmp(weather.city, "Hefei") == 0 && strcmp(weather.weather, "Cloudy") == 0);
    assert(weather.weather_code == 4 && weather.temperature == 23.5f);
    script_done();
    static char oversized[3000];
    memset(oversized, 'x', sizeof oversized - 1);
    unsigned before = arm_count;
    script("AT+HTTPCLIENT=2,1,\"https://example.com\",,,2\r\n", oversized);
    assert(esp_at_http_get("https://example.com") == NULL);
    script_done();
    assert(arm_count - before >= sizeof oversized - 1); /* 缓冲溢出后仍保持接收中断。 */
    assert(!esp_at_wifi_init());                        /* 溢出后必须重新同步。 */
    initialize();
    script("AT+SYSSTORE=0\r\n", NULL);
    inject_uart_error = true;
    before = arm_count;
    assert(!esp_at_wifi_init());
    script_done();
    assert(arm_count > before);
    assert(!esp_at_wifi_init());
    initialize();
    /* 空闲时的主动结束行不得确认下一条命令。 */
    receive_text("OK\r\n");
    script("AT+SYSSTORE=0\r\n", NULL);
    assert(!esp_at_wifi_init());
    script_done();
    assert(!esp_at_wifi_init());
    initialize();
    receive_text("ready\r\n");
    assert(!esp_at_wifi_init());
    initialize();
    server_transport();
    server_adversarial();
    server_failures();
    server_data_entry_recovery();
    server_retained_data_entry();
    server_guard_race();
    mqtt_initial_cleanup();
    mqtt_configuration_diagnostics();
    mqtt_server_name_policy();
    mqtt_state_quoted_port();
    mqtt_transport();
    mqtt_adversarial();
    mqtt_data_entry_recovery();
    wifi_recovery();
    setup_transport();
    disconnect_failures();
    script("AT+SYSSTORE=0\r\n", "OK\r\nready\r\n");
    assert(!esp_at_wifi_init());
    script_done();
    assert(scheduler_suspend_depth == 0U);
    puts(boot_in_data_mode ? "esp transport tests passed (data-entry boot)"
                           : "esp transport tests passed (command-mode boot)");
    return 0;
}
