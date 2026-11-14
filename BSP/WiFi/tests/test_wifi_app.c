#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "wifi.h"
void wifi_setup_page_display(const wifi_view_t *view);
/* 直接编译真实应用决策，外部设备与调度边界由下方桩提供。 */
#include "../../../app/app.c"

enum { TEST_CLOCK, TEST_SENSOR, TEST_NETWORK, TEST_TASK_COUNT };
/** 保存调度器登记的真实任务入口。 */
typedef struct {
    TaskFunction_t function; // 任务入口
    void *parameter;         // 启动参数
} test_task_t;
static test_task_t test_tasks[TEST_TASK_COUNT];
static jmp_buf stopped;
static unsigned running_task;
static TickType_t test_tick, duration_left;
static bool blocked, display_locked, setup_active, radio_connected;
static bool inject_submission, queued_submission, response_closed;
static bool reconnect_failure;
static unsigned unready_marks;
static void (*delay_hook)(void);
static wifi_view_t view;
static void (*background_callback)(void);
static int (*submit_callback)(const char *, uint32_t);
static void (*status_callback)(char *, size_t, uint32_t);
static unsigned qr_draws, normal_draws, main_draws, wifi_polls, wifi_queries, reconnects;
static unsigned web_polls, web_available, cloud_polls, cloud_networks, weather_requests,
    time_requests;
static unsigned health_polls, discovery_polls, joined_candidates, sensor_copies;
static unsigned temperature_draws, humidity_draws, ssid_draws, city_draws;
static float drawn_temperature, drawn_humidity;
static char drawn_ssid[64], drawn_city[32];

/** 功能：推进虚拟节拍并结束有界任务运行；参数：ticks，经过节拍；返回：无。 */
static void advance(TickType_t ticks) {
    test_tick += ticks;
    if (delay_hook) {
        delay_hook();
    }
    if (ticks >= duration_left) {
        longjmp(stopped, 1);
    }
    duration_left -= ticks;
}

/** 功能：运行真实任务直到指定时长；参数：index，任务编号；duration，持续节拍；返回：无。 */
static void run_task(unsigned index, TickType_t duration) {
    assert(index < TEST_TASK_COUNT && test_tasks[index].function);
    running_task = index;
    duration_left = duration;
    if (setjmp(stopped) == 0) {
        test_tasks[index].function(test_tasks[index].parameter);
    }
}

/** 功能：登记生产任务入口；参数：同任务创建接口；返回：创建成功。 */
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth, void *parameter,
                       UBaseType_t priority, TaskHandle_t *handle) {
    unsigned index = strcmp(name, "clock") == 0
                         ? TEST_CLOCK
                         : (strcmp(name, "sensor") == 0 ? TEST_SENSOR : TEST_NETWORK);
    (void)depth;
    (void)priority;
    assert(!test_tasks[index].function);
    test_tasks[index].function = function;
    test_tasks[index].parameter = parameter;
    *handle = &test_tasks[index];
    return pdPASS;
}

/** 功能：读取虚拟时钟；参数：无；返回：节拍。 */
TickType_t xTaskGetTickCount(void) {
    return test_tick;
}
/** 功能：运行任务延时边界；参数：ticks，节拍；返回：无。 */
void vTaskDelay(TickType_t ticks) {
    advance(ticks);
}
/** 功能：运行周期任务延时边界；参数：previous，前次唤醒；increment，周期；返回：无。 */
void vTaskDelayUntil(TickType_t *previous, TickType_t increment) {
    *previous += increment;
    advance(increment);
}
/** 功能：返回堆余量；参数：无；返回：测试值。 */
size_t xPortGetFreeHeapSize(void) {
    return 32000U;
}
/** 功能：返回最小堆余量；参数：无；返回：测试值。 */
size_t xPortGetMinimumEverFreeHeapSize(void) {
    return 24000U;
}
/** 功能：返回栈余量；参数：task，任务；返回：测试值。 */
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
    (void)task;
    return 512U;
}
/** 功能：执行诊断工作；参数：work，入口；parameter，参数；返回：成功。 */
bool workqueue_try_run(work_t work, void *parameter) {
    work(parameter);
    return true;
}
/** 功能：丢弃格式日志；参数：格式与可变参数；返回：零。 */
int clock_log(const char *format, ...) {
    (void)format;
    return 0;
}
/** 功能：丢弃文本日志；参数：message，文本；返回：零。 */
int clock_log_line(const char *message) {
    (void)message;
    return 0;
}

/** 功能：模拟闹钟优先显示锁；参数：无；返回：能否取得锁。 */
bool alarm_service_display_begin(void) {
    assert(!display_locked);
    display_locked = !blocked;
    return display_locked;
}
/** 功能：释放显示锁；参数：无；返回：无。 */
void alarm_service_display_end(void) {
    assert(display_locked);
    display_locked = false;
}
/** 功能：检查所有主页面写入均持锁；参数：无；返回：无。 */
static void normal_draw(void) {
    assert(display_locked);
    ++normal_draws;
}
/** 功能：记录主页面完整恢复；参数：无；返回：无。 */
void main_page_display(void) {
    normal_draw();
    ++main_draws;
}
/** 功能：记录时间绘制；参数：date，日期；返回：无。 */
void main_page_redraw_time(rtc_date_time_t *date) {
    assert(date->year == 2026U);
    normal_draw();
}
/** 功能：记录日期绘制；参数：date，日期；返回：无。 */
void main_page_redraw_date(rtc_date_time_t *date) {
    assert(date->month == 11U);
    normal_draw();
}
/** 功能：记录室温绘制；参数：value，温度；返回：无。 */
void main_page_redraw_inner_temperature(float value) {
    normal_draw();
    ++temperature_draws;
    drawn_temperature = value;
}
/** 功能：记录湿度绘制；参数：value，湿度；返回：无。 */
void main_page_redraw_inner_humidity(float value) {
    normal_draw();
    ++humidity_draws;
    drawn_humidity = value;
}
/** 功能：记录名称绘制；参数：value，名称；返回：无。 */
void main_page_redraw_wifi_ssid(const char *value) {
    normal_draw();
    ++ssid_draws;
    assert(strlen(value) < sizeof drawn_ssid);
    memcpy(drawn_ssid, value, strlen(value) + 1U);
}
/** 功能：记录城市绘制；参数：value，城市；返回：无。 */
void main_page_redraw_outdoor_city(const char *value) {
    normal_draw();
    ++city_draws;
    assert(strlen(value) < sizeof drawn_city);
    memcpy(drawn_city, value, strlen(value) + 1U);
}
/** 功能：记录室外温度；参数：value，温度；返回：无。 */
void main_page_redraw_outdoor_temperature(float value) {
    assert(value == 23.0f);
    normal_draw();
}
/** 功能：记录天气图标；参数：code，图标；返回：无。 */
void main_page_redraw_outdoor_weather_icon(int code) {
    assert(code == 4);
    normal_draw();
}
/** 功能：检查二维码只能由时钟任务持锁绘制；参数：snapshot，页面快照；返回：无。 */
void wifi_setup_page_display(const wifi_view_t *snapshot) {
    assert(running_task == TEST_CLOCK && display_locked && snapshot->active);
    ++qr_draws;
}

/** 功能：提供固定 RTC 日期；参数：date，输出；返回：无。 */
void rtc_get_time(rtc_date_time_t *date) {
    *date = (rtc_date_time_t){2026, 11, 15, 12, 34, 56, 7};
}
/** 功能：接受 RTC 校时；参数：date，日期；返回：无。 */
void rtc_set_time(const rtc_date_time_t *date) {
    assert(date->year == 2026U);
}
/** 功能：报告 RTC 正常；参数：无；返回：真。 */
bool clock_rtc_ready(void) {
    return true;
}
/** 功能：报告传感器初始化成功；参数：无；返回：真。 */
bool aht20_init(void) {
    return true;
}
/** 功能：报告传感器触发成功；参数：无；返回：真。 */
bool aht20_start_measurement(void) {
    return true;
}
/** 功能：报告传感器测量完成；参数：无；返回：真。 */
bool aht20_wait_for_measurement(void) {
    return true;
}
/** 功能：提供最新室内测量；参数：温度与湿度输出；返回：成功。 */
bool aht20_read_measurement(float *temperature, float *humidity) {
    *temperature = 27.25f;
    *humidity = 61.5f;
    return true;
}
/** 功能：提供空传感器诊断；参数：无；返回：空诊断。 */
aht20_diagnostics_t aht20_get_diagnostics(void) {
    aht20_diagnostics_t result = {0};
    return result;
}

/** 功能：报告网络模块可用；参数：无；返回：真。 */
bool wifi_ready(void) {
    return true;
}
/** 功能：模拟初始化；参数：无；返回：成功。 */
bool wifi_init(void) {
    return true;
}
/** 功能：模拟断线恢复；参数：无；返回：成功。 */
bool wifi_wait_connect(void) {
    ++reconnects;
    radio_connected = !reconnect_failure;
    return radio_connected;
}
/** 功能：模拟模块失效；参数：无；返回：无。 */
void wifi_mark_unready(void) {
    ++unready_marks;
    radio_connected = false;
}
/** 功能：提供配网模式；参数：无；返回：是否配网。 */
bool wifi_setup_active(void) {
    return setup_active;
}
/** 功能：提供热点传输可用性；参数：无；返回：是否配网。 */
bool wifi_setup_available(void) {
    return setup_active;
}
/** 功能：提供独立界面快照；参数：snapshot，输出；返回：无。 */
void wifi_get_view(wifi_view_t *snapshot) {
    *snapshot = view;
    snapshot->active = setup_active;
}
/** 功能：消费已排队候选并检查上个应答已结束；参数：now_ms，时刻；返回：无。 */
void wifi_poll(uint32_t now_ms) {
    assert(now_ms == test_tick);
    ++wifi_polls;
    if (queued_submission) {
        assert(response_closed);
        queued_submission = false;
        ++joined_candidates;
    }
}
/** 功能：排队模拟候选；参数：正文与会话；返回：已接受。 */
int wifi_setup_submit(const char *body, uint32_t session) {
    (void)body;
    (void)session;
    assert(!response_closed);
    queued_submission = true;
    return 202;
}
/** 功能：提供配网状态桩；参数：输出区、容量、会话；返回：无。 */
void wifi_setup_status(char *output, size_t capacity, uint32_t session) {
    (void)session;
    (void)snprintf(output, capacity, "{}");
}
/** 功能：报告 AT 可用；参数：无；返回：真。 */
bool esp_at_is_ready(void) {
    return true;
}
/** 功能：提供普通 STA 状态；参数：info，输出；返回：成功。 */
bool esp_at_get_wifi_info(esp_wifi_info_t *info) {
    ++wifi_queries;
    memset(info, 0, sizeof *info);
    info->connected = radio_connected;
    memcpy(info->ssid, "fixture-ap", sizeof "fixture-ap");
    return true;
}
/** 功能：提供网络校时；参数：date，输出；返回：成功。 */
bool esp_at_sntp_get_time(esp_date_time_t *date) {
    ++time_requests;
    *date = (esp_date_time_t){2026, 11, 15, 12, 34, 56, 7};
    return true;
}
/** 功能：提供固定天气响应；参数：url，请求；返回：非空响应。 */
const char *esp_at_http_get(const char *url) {
    assert(strstr(url, "fixture-key"));
    ++weather_requests;
    return "fixture";
}
/** 功能：提供有界天气响应；参数：url，请求；timeout_ms，超时；返回：响应。 */
const char *esp_at_http_get_bounded(const char *url, uint32_t timeout_ms) {
    (void)timeout_ms;
    return esp_at_http_get(url);
}
/** 功能：提供解析后的固定天气；参数：response，响应；weather，输出；返回：成功。 */
bool parse_seniverse_response(const char *response, weather_info_t *weather) {
    assert(response);
    memset(weather, 0, sizeof *weather);
    memcpy(weather->city, "FixtureCity", sizeof "FixtureCity");
    weather->temperature = 23.0f;
    weather->weather_code = 4;
    return true;
}

/** 功能：登记普通网页后台回调；参数：poll，入口；返回：无。 */
void clock_web_set_background_poll(void (*poll)(void)) {
    background_callback = poll;
}
/** 功能：登记真实配网回调；参数：submit，提交；status，状态；返回：无。 */
void clock_web_set_wifi_handlers(int (*submit)(const char *, uint32_t),
                                 void (*status)(char *, size_t, uint32_t)) {
    submit_callback = submit;
    status_callback = status;
}
/** 功能：接受路由模式通知；参数：enabled，启用；session，会话；返回：无。 */
void clock_web_setup(bool enabled, uint32_t session) {
    (void)enabled;
    (void)session;
}
/** 功能：记录本地传输可用性；参数：connected，可用性；now_ms，时刻；返回：无。 */
void clock_web_network(bool connected, uint32_t now_ms) {
    assert(now_ms == test_tick);
    if (connected) {
        ++web_available;
    }
}
/** 功能：模拟应答分段回调及完成关闭；参数：now_ms，时刻；返回：无。 */
void clock_web_poll(uint32_t now_ms) {
    assert(now_ms == test_tick);
    ++web_polls;
    if (inject_submission) {
        inject_submission = false;
        response_closed = false;
        assert(submit_callback && submit_callback("fixture", 1U) == 202);
        assert(queued_submission && joined_candidates == 0U);
        if (background_callback) {
            background_callback();
        }
        assert(joined_candidates == 0U);
        response_closed = true;
    }
}
/** 功能：报告配置了云服务；参数：无；返回：真。 */
bool onenet_service_configured(void) {
    return true;
}
/** 功能：记录云网络维护；参数：连接、时间与时刻；返回：无。 */
void onenet_service_network(bool connected, bool ready, uint32_t now_ms) {
    (void)connected;
    (void)ready;
    assert(now_ms == test_tick);
    ++cloud_networks;
}
/** 功能：记录云通信轮询；参数：now_ms，时刻；返回：无。 */
void onenet_service_poll(uint32_t now_ms) {
    assert(now_ms == test_tick);
    ++cloud_polls;
}
/** 功能：记录本地传感器快照更新；参数：温度与湿度；返回：无。 */
void onenet_service_sensor(float temperature, float humidity) {
    (void)temperature;
    (void)humidity;
    ++sensor_copies;
}
/** 功能：报告云服务空闲；参数：无；返回：假。 */
bool onenet_service_busy(void) {
    return false;
}

#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
/** 功能：接受 OTA 启动；参数：无；返回：无。 */
void ota_service_start(void) {
}
/** 功能：接受真实本地任务健康心跳；参数：source，来源；返回：无。 */
void ota_service_heartbeat(uint8_t source) {
    assert(source == OTA_HEARTBEAT_CLOCK);
}
/** 功能：记录 OTA 健康确认网络边界；参数：now_ms，时刻；返回：无。 */
void ota_service_network_poll(uint32_t now_ms) {
    assert(now_ms == test_tick);
    assert(!queued_submission || response_closed);
    ++health_polls;
}
/** 功能：记录 OTA 自动发现；参数：连接和时刻；返回：无。 */
void ota_service_autocheck_poll(bool connected, uint32_t now_ms) {
    (void)connected;
    assert(now_ms == test_tick);
    ++discovery_polls;
}
/** 功能：提供空诊断 JSON；参数：输出区、容量、会话；返回：无。 */
void ota_service_status(char *output, size_t capacity, uint32_t session) {
    (void)session;
    (void)snprintf(output, capacity, "{}");
}
/** 功能：提供空 EEPROM 诊断；参数：diagnostic，输出；返回：无。 */
void bl24c512_get_diagnostic(bl24c512_diagnostic_t *diagnostic) {
    memset(diagnostic, 0, sizeof *diagnostic);
}
#endif

/** 功能：在同一个持续运行的时钟任务中切换闹钟抢占和配网代次；参数：无；返回：无。 */
static void clock_display_phases(void) {
    assert(normal_draws == 0U);
    if (test_tick == 1000U) {
        assert(qr_draws == 0U);
        blocked = false;
    } else if (test_tick == 2000U) {
        assert(qr_draws == 1U);
    } else if (test_tick == 3000U) {
        assert(qr_draws == 1U);
        ++view.generation;
    } else if (test_tick == 4000U) {
        assert(qr_draws == 2U);
        blocked = true;
        app_request_main_redraw();
    } else if (test_tick == 5000U) {
        assert(qr_draws == 2U);
        blocked = false;
    } else {
        assert(qr_draws == 3U);
    }
}

/** 功能：执行单个隔离的真实应用回归场景；参数：场景编号；返回：通过为零。 */
int main(int argc, char **argv) {
    assert(argc == 2);
    unsigned scenario = (unsigned)atoi(argv[1]);
    app_init();
    view.generation = 1U;
    view.ready = true;
    if (scenario == 1U) {
        assert(submit_callback == wifi_setup_submit && status_callback == wifi_setup_status);
        assert(background_callback);
    } else if (scenario == 2U) {
        setup_active = true;
        run_task(TEST_NETWORK, 1000U);
        assert(wifi_polls > 0U && web_polls > 0U && web_available > 0U);
        assert(wifi_queries == 0U && reconnects == 0U);
        assert(cloud_polls == 0U && cloud_networks == 0U && weather_requests == 0U &&
               time_requests == 0U);
        assert(discovery_polls == 0U);
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
        assert(health_polls > 0U);
#endif
        background_callback();
        assert(cloud_polls == 0U);
    } else if (scenario == 3U) {
        setup_active = true;
        blocked = true;
        delay_hook = clock_display_phases;
        run_task(TEST_CLOCK, 7000U);
        assert(qr_draws == 3U && normal_draws == 0U);
    } else if (scenario == 4U) {
        setup_active = true;
        publish_indoor(18.0f, 30.0f);
        publish_ssid("pending-ap");
        update_weather();
        assert(normal_draws == 0U);
        run_task(TEST_CLOCK, 1000U);
        assert(qr_draws == 1U);
        run_task(TEST_SENSOR, 3000U);
        publish_ssid("latest-ap");
        update_weather();
        assert(sensor_copies == 2U && normal_draws == 0U);
        setup_active = false;
        publish_indoor(29.5f, 65.25f);
        publish_ssid("saved-ap");
        assert(normal_draws == 0U);
        run_task(TEST_CLOCK, 1000U);
        assert(main_draws == 1U && temperature_draws == 1U && humidity_draws == 1U);
        assert(drawn_temperature == 29.5f && drawn_humidity == 65.25f);
        assert(ssid_draws == 1U && strcmp(drawn_ssid, "saved-ap") == 0);
        assert(city_draws == 1U && strcmp(drawn_city, "FixtureCity") == 0);
    } else if (scenario == 5U) {
        run_task(TEST_NETWORK, 6000U);
        assert(wifi_polls > 0U && wifi_queries > 0U && reconnects > 0U && web_available > 0U);
        assert(cloud_polls > 0U && cloud_networks > 0U && time_requests > 0U &&
               weather_requests > 0U);
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
        assert(health_polls > 0U && discovery_polls > 0U);
#endif
    } else if (scenario == 7U) {
        run_task(TEST_NETWORK, 1000U);
        assert(radio_connected && reconnects > 0U);
        reconnect_failure = true;
        radio_connected = false;
        run_task(TEST_NETWORK, 20000U);
        assert(reconnects >= 5U && wifi_queries >= 5U && unready_marks == 0U);
        assert(!setup_active);
    } else {
        assert(scenario == 6U);
        setup_active = true;
        inject_submission = true;
        run_task(TEST_NETWORK, 1000U);
        assert(response_closed && joined_candidates == 1U && !queued_submission);
        assert(cloud_polls == 0U);
    }
    printf("Wi-Fi production application scenario %u passed\n", scenario);
    return 0;
}
