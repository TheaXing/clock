#include "wifi.h"
#include "wifi_identity.h"
#include "wifi_store.h"
#include "wifi_form.h"
#include "clock_web.h"
#include "clock_console.h"
#include "esp_at.h"
#include "bl24c512.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define WIFI_SETUP_RETRY_MS 5000U
#define WIFI_SETUP_CLIENT_MS 2000U
#define WIFI_SETUP_GRACE_MS 15000U

static bool initialized, credentials_loaded, ever_connected;
static bool setup_requested, ap_started, pending, identity_ready, attempted;
static uint32_t setup_session, last_attempt, last_client, saved_ms;
static wifi_credentials_t selected, candidate;
static wifi_view_t current_view;

/** 功能：适配事务内 EEPROM 读取。参数：上下文、地址、输出和长度。返回：读取结果。 */
static bool read_credentials(void *context, uint32_t address, void *data, size_t length) {
    (void)context;
    return bl24c512_read(address, data, length);
}

/** 功能：适配事务内 EEPROM 写入。参数：上下文、地址、输入和长度。返回：写入结果。 */
static bool write_credentials(void *context, uint32_t address, const void *data, size_t length) {
    (void)context;
    return bl24c512_write(address, data, length);
}

/** 功能：更新跨任务阶段快照。参数：阶段和固定错误名。返回：无。 */
static void set_state(wifi_setup_state_t state, const char *error) {
    taskENTER_CRITICAL();
    if (current_view.state != state || strcmp(current_view.error, error) != 0) {
        current_view.state = state;
        (void)snprintf(current_view.error, sizeof current_view.error, "%s", error);
        ++current_view.generation;
    }
    taskEXIT_CRITICAL();
}

/** 功能：发布页面可见性变化。参数：显示、就绪和手机连接标志。返回：无。 */
static void set_visibility(bool active, bool ready, bool client) {
    taskENTER_CRITICAL();
    if (current_view.active != active || current_view.ready != ready ||
        current_view.client != client) {
        current_view.active = active;
        current_view.ready = ready;
        current_view.client = client;
        ++current_view.generation;
    }
    taskEXIT_CRITICAL();
}

/** 功能：首次启动加载配置，未配置时兼容编译默认值。参数：无。返回：无。 */
static void load_credentials(void) {
    if (credentials_loaded) {
        return;
    }
    credentials_loaded = true;
    wifi_store_io_t io = {read_credentials, write_credentials, NULL};
    wifi_store_result_t result = WIFI_STORE_IO_ERROR;
    if (bl24c512_transaction_begin()) {
        result = wifi_store_load(&io, &selected);
        bl24c512_transaction_end();
    }
    if (result == WIFI_STORE_OK) {
        clock_log_line("[WIFI] Loaded saved network configuration");
        return;
    }
    memset(&selected, 0, sizeof selected);
    if (strlen(CLOCK_WIFI_SSID) < sizeof selected.ssid &&
        strlen(CLOCK_WIFI_PASSWORD) < sizeof selected.password) {
        memcpy(selected.ssid, CLOCK_WIFI_SSID, strlen(CLOCK_WIFI_SSID) + 1U);
        memcpy(selected.password, CLOCK_WIFI_PASSWORD, strlen(CLOCK_WIFI_PASSWORD) + 1U);
    }
    if (!wifi_credentials_valid(&selected)) {
        memset(&selected, 0, sizeof selected);
    }
    if (result == WIFI_STORE_IO_ERROR) {
        clock_log_line("[WIFI] Configuration read failed; defaults or setup remain available");
    }
}

/** 功能：请求配网，后续由轮询建立热点。参数：无。返回：无。 */
static void request_setup(void) {
    setup_requested = true;
    attempted = false;
    set_state(WIFI_SETUP_WAITING, "none");
    set_visibility(true, false, false);
}

/** 功能：取得无线初始化状态。参数：无。返回：是否已初始化。 */
bool wifi_ready(void) {
    return initialized;
}

/** 功能：请求失步传输重新初始化，保留热点身份及持久化凭据。参数：无。返回：无。 */
void wifi_mark_unready(void) {
    /* 已经持久化成功时直接恢复新网络，避免 ESP 在结果展示期复位后要求用户重复配网。 */
    if (setup_requested && current_view.state == WIFI_SETUP_SAVED) {
        setup_requested = false;
        clock_web_setup(false, 0U);
    }
    initialized = false;
    ap_started = false;
    attempted = false;
    pending = false;
    memset(&candidate, 0, sizeof candidate);
    set_state(WIFI_SETUP_FAILED, "module");
    set_visibility(setup_requested || !ever_connected, false, false);
}

/** 功能：初始化传输而不依赖已配置网络。参数：无。返回：成功为真。 */
bool wifi_init(void) {
    initialized = false;
    load_credentials();
    if (!esp_at_init()) {
        set_state(WIFI_SETUP_FAILED, "module");
        set_visibility(setup_requested || !ever_connected, false, false);
        return false;
    }
    char firmware[768];
    if (esp_at_get_firmware_info(firmware, sizeof firmware)) {
        clock_log("[ESP] Firmware information:\r\n%s", firmware);
    }
    if (esp_at_probe_mqtt() == ESP_MQTT_PROBE_TRANSPORT_ERROR || !esp_at_wifi_init() ||
        !esp_at_sntp_init()) {
        set_state(WIFI_SETUP_FAILED, "module");
        set_visibility(setup_requested || !ever_connected, false, false);
        return false;
    }
    initialized = true;
    ap_started = false;
    attempted = false;
    if (!setup_requested) {
        set_visibility(false, false, false);
    }
    clock_log_line("[WIFI] AT and SNTP initialized");
    return true;
}

/** 功能：验证已取得目标网络地址，拒绝残留的其他网络。参数：凭据。返回：连接有效为真。 */
static bool connected_to(const wifi_credentials_t *credentials) {
    esp_wifi_info_t info = {0};
    return esp_at_get_wifi_info(&info) && info.connected &&
           strcmp(info.ssid, credentials->ssid) == 0;
}

/** 功能：首次连接失败进入配网，运行期掉线维持原网络重连。参数：无。返回：已联网为真。 */
bool wifi_wait_connect(void) {
    if (!initialized || setup_requested) {
        return false;
    }
    if (!wifi_credentials_valid(&selected)) {
        request_setup();
        return false;
    }
    if (connected_to(&selected) ||
        (esp_at_connect_wifi(selected.ssid, selected.password, NULL) && connected_to(&selected))) {
        ever_connected = true;
        return true;
    }
    if (!ever_connected) {
        request_setup();
    }
    return false;
}

/** 功能：查询配网流程状态。参数：无。返回：活动为真。 */
bool wifi_setup_active(void) {
    return setup_requested;
}

/** 功能：查询本地热点可用性。参数：无。返回：可承载网页为真。 */
bool wifi_setup_available(void) {
    return setup_requested && ap_started && initialized && esp_at_is_ready();
}

/** 功能：复制显示快照。参数：输出。返回：无。 */
void wifi_get_view(wifi_view_t *view) {
    if (view != NULL) {
        taskENTER_CRITICAL();
        *view = current_view;
        taskEXIT_CRITICAL();
    }
}

/** 功能：校验并排队候选，联网等待在 HTTP 关闭后执行。参数：表单和会话。返回：HTTP 状态码。 */
int wifi_setup_submit(const char *body, uint32_t session) {
    if (!wifi_setup_available() || !esp_at_server_ready() || session != setup_session) {
        return 503;
    }
    if (pending || current_view.state == WIFI_SETUP_SAVED) {
        return 409;
    }
    wifi_credentials_t submitted = {0};
    if (!wifi_form_parse(body, session, submitted.ssid, submitted.password) ||
        !wifi_credentials_valid(&submitted)) {
        return 400;
    }
    candidate = submitted;
    pending = true;
    set_state(WIFI_SETUP_CONNECTING, "none");
    return 202;
}

/** 功能：输出不包含密码及用户输入的状态 JSON。参数：缓冲、容量和会话。返回：无。 */
void wifi_setup_status(char *output, size_t capacity, uint32_t session) {
    static const char *const names[] = {"waiting", "connecting", "verifying",
                                        "saving",  "saved",      "failed"};
    if (output != NULL && capacity > 0U) {
        (void)snprintf(output, capacity, "{\"session\":%lu,\"state\":\"%s\",\"error\":\"%s\"}",
                       (unsigned long)session, names[current_view.state], current_view.error);
    }
}

/** 功能：清理失败候选的终端连接和旧请求，避免局域网继续访问配网。参数：固定错误名。返回：无。 */
static void reject_candidate(const char *error) {
    memset(&candidate, 0, sizeof candidate);
    if (!esp_at_is_ready() || !esp_at_disconnect_wifi()) {
        wifi_mark_unready();
        return;
    }
    /* 物理连接已经关闭，丢弃 HTTP 分片；下一次维护立即重建仅有热点的服务。 */
    clock_web_network(false, (uint32_t)xTaskGetTickCount());
    set_state(WIFI_SETUP_FAILED, error);
    set_visibility(true, false, current_view.client);
}

/** 功能：验证候选并原子保存，失败保留旧记录。参数：无。返回：无。 */
static void process_candidate(void) {
    pending = false;
    set_state(WIFI_SETUP_VERIFYING, "none");
    if (!esp_at_connect_wifi(candidate.ssid, candidate.password, NULL) ||
        !connected_to(&candidate)) {
        reject_candidate("connection");
        return;
    }
    set_state(WIFI_SETUP_SAVING, "none");
    wifi_store_io_t io = {read_credentials, write_credentials, NULL};
    wifi_store_result_t result = WIFI_STORE_IO_ERROR;
    if (bl24c512_transaction_begin()) {
        result = wifi_store_save(&io, &candidate);
        bl24c512_transaction_end();
    }
    if (result == WIFI_STORE_OK) {
        selected = candidate;
        ever_connected = true;
        saved_ms = (uint32_t)xTaskGetTickCount();
        set_state(WIFI_SETUP_SAVED, "none");
        clock_log_line("[WIFI] Network configuration verified and saved");
    } else {
        reject_candidate("storage");
    }
    memset(&candidate, 0, sizeof candidate);
}

/** 功能：维护热点和候选事务，完成后关闭热点。参数：当前毫秒。返回：无。 */
void wifi_poll(uint32_t now_ms) {
    if (!setup_requested || !initialized) {
        return;
    }
    if (!esp_at_is_ready()) {
        wifi_mark_unready();
        return;
    }
    if (!ap_started) {
        if (attempted && now_ms - last_attempt < WIFI_SETUP_RETRY_MS) {
            return;
        }
        attempted = true;
        last_attempt = now_ms;
        if (!identity_ready) {
            char name[33] = {0};
            char password[17] = {0};
            if (!wifi_setup_identity(name, password, &setup_session)) {
                set_state(WIFI_SETUP_FAILED, "random");
                return;
            }
            taskENTER_CRITICAL();
            memcpy(current_view.ap_ssid, name, sizeof name);
            memcpy(current_view.ap_password, password, sizeof password);
            ++current_view.generation;
            taskEXIT_CRITICAL();
            identity_ready = true;
        }
        if (!esp_at_setup_start(current_view.ap_ssid, current_view.ap_password)) {
            set_state(WIFI_SETUP_FAILED, "module");
            return;
        }
        ap_started = true;
        last_client = now_ms - WIFI_SETUP_CLIENT_MS;
        clock_web_setup(true, setup_session);
        set_state(WIFI_SETUP_WAITING, "none");
    }
    bool has_client = current_view.client;
    if (now_ms - last_client >= WIFI_SETUP_CLIENT_MS) {
        last_client = now_ms;
        if (!esp_at_setup_has_client(&has_client)) {
            has_client = false;
        }
    }
    set_visibility(true, esp_at_server_ready(), has_client);
    if (pending) {
        process_candidate();
        return;
    }
    if (current_view.state == WIFI_SETUP_SAVED && now_ms - saved_ms >= WIFI_SETUP_GRACE_MS) {
        if (!esp_at_setup_stop()) {
            wifi_mark_unready();
            return;
        }
        clock_web_setup(false, 0U);
        setup_requested = false;
        ap_started = false;
        set_visibility(false, false, false);
    }
}
