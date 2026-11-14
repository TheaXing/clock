#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi.h"
#include "wifi_store.h"
#include "esp_at.h"

static wifi_credentials_t durable;
static bool have_durable, write_ok = true, random_ok = true;
static bool module_ok = true, transport, connected, join_ok = true, wrong_ssid;
static bool ap, client, server;
static bool disconnect_ok = true;
static unsigned disconnects, web_resets;
static unsigned joins, saves, ap_starts, ap_stops, lock_depth;
static uint32_t tick;
static char joined_ssid[33];

/** 功能：提供无敏感内容的日志桩。参数：格式及可变参数。返回：零。 */
int clock_log(const char *format, ...) {
    (void)format;
    return 0;
}
/** 功能：提供无敏感内容的行日志桩。参数：文字。返回：零。 */
int clock_log_line(const char *text) {
    (void)text;
    return 0;
}
/** 功能：提供测试时钟。参数：无。返回：当前节拍。 */
uint32_t xTaskGetTickCount(void) {
    return tick;
}
/** 功能：模拟完整 EEPROM 事务加锁。参数：无。返回：成功。 */
bool bl24c512_transaction_begin(void) {
    assert(lock_depth == 0U);
    ++lock_depth;
    return true;
}
/** 功能：模拟事务解锁。参数：无。返回：无。 */
void bl24c512_transaction_end(void) {
    assert(lock_depth == 1U);
    --lock_depth;
}
/** 功能：提供不执行的存储回调。参数：地址、数据、长度。返回：失败。 */
bool bl24c512_read(uint32_t address, void *data, size_t length) {
    (void)address;
    (void)data;
    (void)length;
    return false;
}
/** 功能：提供不执行的存储回调。参数：地址、数据、长度。返回：失败。 */
bool bl24c512_write(uint32_t address, const void *data, size_t length) {
    (void)address;
    (void)data;
    (void)length;
    return false;
}
/** 功能：模拟启动读取，验证事务锁。参数：回调和凭据输出。返回：读取结果。 */
wifi_store_result_t wifi_store_load(const wifi_store_io_t *io, wifi_credentials_t *out) {
    assert(lock_depth == 1U && io->read != NULL);
    if (!have_durable) {
        return WIFI_STORE_EMPTY;
    }
    *out = durable;
    return WIFI_STORE_OK;
}
/** 功能：模拟持久化，拒绝连接失败时保存。参数：回调及凭据。返回：保存结果。 */
wifi_store_result_t wifi_store_save(const wifi_store_io_t *io, const wifi_credentials_t *value) {
    assert(lock_depth == 1U && io->write != NULL && connected && !wrong_ssid);
    ++saves;
    if (!write_ok) {
        return WIFI_STORE_IO_ERROR;
    }
    durable = *value;
    have_durable = true;
    return WIFI_STORE_OK;
}
/** 功能：提供测试所需的凭据校验边界。参数：凭据。返回：字段是否有效。 */
bool wifi_credentials_valid(const wifi_credentials_t *value) {
    size_t length = strlen(value->password);
    return value->ssid[0] != '\0' && strlen(value->ssid) <= 32U &&
           (length == 0U || (length >= 8U && length <= 63U));
}
/** 功能：产生固定测试身份，不作为产品默认值。参数：热点及会话输出。返回：随机源可用性。 */
bool wifi_setup_identity(char ssid[33], char password[17], uint32_t *session) {
    if (!random_ok) {
        return false;
    }
    strcpy_s(ssid, 33U, "Clock-Setup-12345678");
    strcpy_s(password, 17U, "1234567890abcdef");
    *session = 77U;
    return true;
}
/** 功能：模拟无线模块初始化。参数：无。返回：是否可通信。 */
bool esp_at_init(void) {
    transport = module_ok;
    connected = false;
    return transport;
}
/** 功能：模拟就绪查询。参数：无。返回：传输状态。 */
bool esp_at_is_ready(void) {
    return transport;
}
/** 功能：省略固件版本输出。参数：缓冲及容量。返回：不可用。 */
bool esp_at_get_firmware_info(char *out, size_t size) {
    (void)out;
    (void)size;
    return false;
}
/** 功能：模拟能力探测。参数：无。返回：支持。 */
esp_mqtt_probe_result_t esp_at_probe_mqtt(void) {
    return ESP_MQTT_PROBE_SUPPORTED;
}
/** 功能：模拟无线模式配置。参数：无。返回：成功。 */
bool esp_at_wifi_init(void) {
    return true;
}
/** 功能：模拟校时配置。参数：无。返回：成功。 */
bool esp_at_sntp_init(void) {
    return true;
}
/** 功能：模拟查询，支持未取得 IP 和错误网络。参数：信息输出。返回：传输状态。 */
bool esp_at_get_wifi_info(esp_wifi_info_t *out) {
    memset(out, 0, sizeof *out);
    out->connected = connected;
    strcpy_s(out->ssid, sizeof out->ssid, wrong_ssid ? "unexpected" : joined_ssid);
    return transport;
}
/** 功能：记录真实候选网络，验证联网未持有存储锁。参数：凭据及可选地址。返回：连接结果。 */
bool esp_at_connect_wifi(const char *ssid, const char *password, const char *mac) {
    assert(lock_depth == 0U && password != NULL && mac == NULL);
    ++joins;
    strcpy_s(joined_ssid, sizeof joined_ssid, ssid);
    tick += 200U;
    connected = join_ok;
    return join_ok;
}
/** 功能：模拟热点启动。参数：热点名称及密码。返回：成功。 */
bool esp_at_setup_start(const char *ssid, const char *password) {
    assert(strncmp(ssid, "Clock-Setup-", 12U) == 0 && strlen(password) == 16U);
    ++ap_starts;
    ap = true;
    return true;
}
/** 功能：模拟热点关闭。参数：无。返回：成功。 */
bool esp_at_setup_stop(void) {
    ++ap_stops;
    ap = false;
    server = false;
    return true;
}
/** 功能：模拟热点客户端。参数：连接输出。返回：成功。 */
bool esp_at_setup_has_client(bool *out) {
    *out = client;
    return true;
}
/** 功能：模拟 HTTP 服务状态。参数：无。返回：就绪状态。 */
bool esp_at_server_ready(void) {
    return server;
}
/** 功能：清理失败候选的终端连接及所有旧网页连接。参数：无。返回：断开是否确认。 */
bool esp_at_disconnect_wifi(void) {
    ++disconnects;
    server = false;
    if (!disconnect_ok) {
        transport = false;
        return false;
    }
    connected = false;
    return true;
}
/** 功能：确认失败候选清理网页解析器。参数：可用性和时刻。返回：无。 */
void clock_web_network(bool available, uint32_t now_ms) {
    assert(!available && now_ms == tick);
    ++web_resets;
}
/** 功能：记录模式切换。参数：配网模式和会话。返回：无。 */
void clock_web_setup(bool enabled, uint32_t session) {
    if (enabled) {
        assert(session == 77U);
    }
}

/** 功能：开始一次可服务的配网会话。参数：无。返回：无。 */
static void enter_setup(void) {
    assert(wifi_init());
    assert(!wifi_wait_connect());
    assert(wifi_setup_active());
    wifi_poll(tick);
    assert(ap && wifi_setup_available());
    server = true;
    client = true;
    tick += 3000U;
    wifi_poll(tick);
    wifi_view_t view;
    wifi_get_view(&view);
    assert(view.active && view.ready && view.client);
}

/** 功能：检查状态 JSON 不泄漏密码。参数：期望状态片段。返回：无。 */
static void expect_status(const char *expected) {
    char json[256];
    wifi_setup_status(json, sizeof json, 77U);
    assert(strstr(json, expected) != NULL);
    assert(strstr(json, "secret123") == NULL && strstr(json, "password") == NULL);
}

/** 功能：执行独立进程中的状态机回归。参数：场景编号。返回：零。 */
int main(int argc, char **argv) {
    assert(argc == 2);
    unsigned scenario = (unsigned)strtoul(argv[1], NULL, 10);
    if (scenario == 9U) {
        assert(wifi_init() && wifi_wait_connect());
        assert(strcmp(joined_ssid, "factory-network") == 0 && !wifi_setup_active());
        assert(saves == 0U);
    } else if (scenario == 2U || scenario == 10U) {
        have_durable = true;
        strcpy_s(durable.ssid, sizeof durable.ssid, "saved-network");
        strcpy_s(durable.password, sizeof durable.password, "saved-password");
        assert(wifi_init() && wifi_wait_connect());
        assert(strcmp(joined_ssid, "saved-network") == 0 && !wifi_setup_active());
        connected = false;
        join_ok = false;
        assert(!wifi_wait_connect() && !wifi_setup_active() && saves == 0U);
        if (scenario == 10U) {
            wifi_mark_unready();
            wifi_view_t view;
            wifi_get_view(&view);
            assert(!view.active);
            module_ok = false;
            assert(!wifi_init());
            wifi_get_view(&view);
            assert(!view.active);
        }
    } else if (scenario == 3U) {
        random_ok = false;
        assert(wifi_init() && !wifi_wait_connect());
        wifi_poll(tick);
        assert(!ap && !wifi_setup_available());
        expect_status("random");
    } else if (scenario == 4U) {
        module_ok = false;
        assert(!wifi_init());
        wifi_view_t view;
        wifi_get_view(&view);
        assert(view.active && !view.ready);
        module_ok = true;
        enter_setup();
    } else {
        enter_setup();
        assert(wifi_setup_submit("session=76&ssid=home&password=secret123", 77U) == 400);
        unsigned before = joins;
        assert(wifi_setup_submit("session=77&ssid=home&password=secret123", 77U) == 202);
        assert(joins == before && saves == 0U);
        assert(wifi_setup_submit("session=77&ssid=home&password=secret123", 77U) == 409);
        if (scenario == 5U) {
            join_ok = false;
        }
        if (scenario == 6U) {
            wrong_ssid = true;
        }
        if (scenario == 7U || scenario == 11U) {
            write_ok = false;
        }
        if (scenario == 11U) {
            disconnect_ok = false;
        }
        wifi_poll(tick);
        if (scenario == 11U) {
            assert(!wifi_ready() && !wifi_setup_available());
            expect_status("module");
        } else if (scenario == 5U || scenario == 6U || scenario == 7U) {
            assert(wifi_setup_active() && !have_durable);
            assert(!connected && disconnects == 1U && web_resets == 1U);
            expect_status(scenario == 7U ? "storage" : "connection");
            assert(saves == (scenario == 7U ? 1U : 0U));
            /* 清理旧连接后，手机重新连接网页可以再次提交，失败不会永久锁住流程。 */
            server = true;
            join_ok = true;
            wrong_ssid = false;
            write_ok = true;
            wifi_poll(tick);
            assert(wifi_setup_submit("session=77&ssid=home&password=secret123", 77U) == 202);
            wifi_poll(tick);
            assert(have_durable && connected && strcmp(durable.ssid, "home") == 0);
            expect_status("saved");
        } else {
            assert(have_durable && saves == 1U && strcmp(durable.ssid, "home") == 0);
            expect_status("saved");
            if (scenario == 8U) {
                transport = false;
                ap = false;
                server = false;
                wifi_mark_unready();
                assert(!wifi_setup_active());
                assert(wifi_init() && wifi_wait_connect());
                assert(strcmp(joined_ssid, "home") == 0 && saves == 1U);
                puts("Saved configuration survives ESP reset");
                return 0;
            }
            uint32_t completed = tick;
            wifi_poll(completed + 14999U);
            assert(ap && wifi_setup_active());
            wifi_poll(completed + 15000U);
            assert(!ap && !wifi_setup_active() && ap_stops == 1U);
            assert(wifi_wait_connect());
        }
    }
    assert(lock_depth == 0U);
    puts("Wi-Fi state scenario passed");
    return 0;
}
