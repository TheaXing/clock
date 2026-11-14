#include "boot_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); exit(1); } } while (0)

/** 保存每个状态在同一安装身份下应执行的启动动作。 */
typedef struct {
    ota_state_t state; // EEPROM 中的状态
    boot_action_t matching; // 安装描述完全相同时的动作
    boot_action_t changed; // 安装描述已有变化时的动作
} policy_case_t;

/** 保存真实日志模块所访问的 EEPROM 和写应答故障。 */
typedef struct {
    uint8_t bytes[0x1000]; // 包含两个完整 OTA 日志槽的设备字节
    unsigned writes; // 当前测试的页写调用数
    unsigned fail_write; // 指定失败的页写调用，零表示禁用
    bool lost_ack; // 失败页是否实际写入但丢失成功应答
} policy_eeprom_t;

/** 功能：读取模拟 EEPROM 原始字节。参数：上下文、地址、输出和长度。返回：完整读取成功时为真。 */
static bool policy_eeprom_read(void *context, uint32_t address, void *data, size_t length) {
    policy_eeprom_t *eeprom = context;
    if (address > sizeof(eeprom->bytes) || length > sizeof(eeprom->bytes) - address) {
        return false;
    }
    memcpy(data, eeprom->bytes + address, length);
    return true;
}

/** 功能：模拟单页写入的写前失败或提交应答丢失。参数：上下文、地址、输入和长度。返回：收到成功应答时为真。 */
static bool policy_eeprom_write(void *context, uint32_t address, const void *data, size_t length) {
    policy_eeprom_t *eeprom = context;
    bool failed;
    CHECK(address >= 0x800 && address + length <= sizeof(eeprom->bytes));
    CHECK(length != 0 && length <= 128 && address / 128 == (address + length - 1) / 128);
    ++eeprom->writes;
    failed = eeprom->fail_write == eeprom->writes;
    if (!failed || eeprom->lost_ack) {
        memcpy(eeprom->bytes + address, data, length);
    }
    return !failed;
}

/** 功能：构造调用前已完整认证的安装描述样本。参数：安装描述和记录输出。返回：无。 */
static void fixture(ota_manifest_t *installed, ota_record_t *record) {
    size_t index;
    memset(installed, 0, sizeof(*installed));
    memset(record, 0, sizeof(*record));
    for (index = 0; index < 252; ++index) {
        installed->raw[index] = (uint8_t)(index * 17u + 3u);
    }
    installed->image_size = 4096;
    installed->version = 7;
    installed->raw[252] = 0x4F;
    installed->raw[253] = 0x54;
    installed->raw[254] = 0x41;
    installed->raw[255] = 0x31;
    memcpy(record->manifest, installed->raw, 256);
    record->state = OTA_STATE_TRIAL;
}

/** 功能：验证未经完整镜像验证或强制恢复时始终拒绝启动。参数：无。返回：无。 */
static void test_full_validation_and_force(void) {
    ota_manifest_t installed;
    ota_record_t record;
    unsigned state;
    unsigned store;
    fixture(&installed, &record);
    for (store = OTA_STORE_OK; store <= OTA_STORE_INVALID; ++store) {
        for (state = OTA_STATE_IDLE; state <= OTA_STATE_FAILED; ++state) {
            record.state = (ota_state_t)state;
            CHECK(boot_policy_action((ota_store_result_t)store, &record, NULL, false) == BOOT_RECOVER);
            CHECK(boot_policy_action((ota_store_result_t)store, &record, &installed, true) == BOOT_RECOVER);
        }
    }
    CHECK(boot_policy_action(OTA_STORE_OK, NULL, &installed, false) == BOOT_RECOVER);
    CHECK(boot_policy_action((ota_store_result_t)99, &record, &installed, false) == BOOT_RECOVER);
    record.state = (ota_state_t)99;
    CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == BOOT_RECOVER);
}

/** 功能：验证空白、损坏和读取失败具有不同策略。参数：无。返回：无。 */
static void test_store_outcomes(void) {
    ota_manifest_t installed;
    ota_record_t record;
    fixture(&installed, &record);
    CHECK(boot_policy_action(OTA_STORE_EMPTY, NULL, &installed, false) == BOOT_REPAIR_TRIAL);
    CHECK(boot_policy_action(OTA_STORE_IO, NULL, &installed, false) == BOOT_START_UNTRACKED);
    CHECK(boot_policy_action(OTA_STORE_CORRUPT, NULL, &installed, false) == BOOT_START_UNTRACKED);
    CHECK(boot_policy_action(OTA_STORE_INVALID, &record, &installed, false) == BOOT_RECOVER);
    record.state = OTA_STATE_REQUESTED;
    CHECK(boot_policy_action(OTA_STORE_EMPTY, &record, &installed, false) == BOOT_REPAIR_TRIAL);
    CHECK(boot_policy_action(OTA_STORE_IO, &record, &installed, false) == BOOT_START_UNTRACKED);
    CHECK(boot_policy_action(OTA_STORE_CORRUPT, &record, &installed, false) == BOOT_START_UNTRACKED);
}

/** 功能：验证全部日志状态及精确包头身份变化。参数：无。返回：无。 */
static void test_state_and_identity(void) {
    static const policy_case_t cases[] = {
        {OTA_STATE_IDLE, BOOT_REPAIR_TRIAL, BOOT_REPAIR_TRIAL},
        {OTA_STATE_REQUESTED, BOOT_RECOVER, BOOT_RECOVER},
        {OTA_STATE_INSTALLING, BOOT_REPAIR_TRIAL, BOOT_RECOVER},
        {OTA_STATE_TRIAL, BOOT_START_TRIAL, BOOT_REPAIR_TRIAL},
        {OTA_STATE_CONFIRMED, BOOT_START_CONFIRMED, BOOT_REPAIR_TRIAL},
        {OTA_STATE_FAILED, BOOT_RECOVER, BOOT_RECOVER}
    };
    ota_manifest_t installed;
    ota_record_t record;
    ota_manifest_t original_installed;
    ota_record_t original_record;
    size_t row;
    size_t byte;
    for (row = 0; row < sizeof(cases) / sizeof(cases[0]); ++row) {
        fixture(&installed, &record);
        record.state = cases[row].state;
        original_installed = installed;
        original_record = record;
        CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == cases[row].matching);
        CHECK(memcmp(&record, &original_record, sizeof(record)) == 0);
        CHECK(memcmp(&installed, &original_installed, sizeof(installed)) == 0);
        for (byte = 0; byte < 252; ++byte) {
            record.manifest[byte] ^= 1;
            CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == cases[row].changed);
            record.manifest[byte] ^= 1;
        }
        for (byte = 252; byte < 256; ++byte) {
            record.manifest[byte] ^= 0xFF;
            CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == cases[row].matching);
        }
    }
}

/** 功能：验证恰好三次未经确认的启动耗尽试启动额度。参数：无。返回：无。 */
static void test_three_attempts(void) {
    ota_manifest_t installed;
    ota_record_t record;
    uint32_t attempt;
    fixture(&installed, &record);
    for (attempt = 0; attempt < 3; ++attempt) {
        record.attempts = attempt;
        CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == BOOT_START_TRIAL);
    }
    record.attempts = 3;
    CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == BOOT_RECOVER);
    record.manifest[251] ^= 1;
    CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == BOOT_REPAIR_TRIAL);
    record.manifest[251] ^= 1;
    record.attempts = 0xFFFFFFFFu;
    CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == BOOT_RECOVER);
    record.state = OTA_STATE_CONFIRMED;
    CHECK(boot_policy_action(OTA_STORE_OK, &record, &installed, false) == BOOT_START_CONFIRMED);
}

/** 功能：验证耗尽试启动后禁止自动重装同一包，避免无限失败重启。参数：无。返回：无。 */
static void test_exhausted_candidate(void) {
    ota_manifest_t candidate;
    ota_record_t record;
    ota_manifest_t original_candidate;
    ota_record_t original_record;
    size_t byte;
    unsigned state;
    uint32_t attempt;
    fixture(&candidate, &record);
    CHECK(!boot_policy_candidate_allowed(NULL, NULL, false));
    CHECK(!boot_policy_candidate_allowed(&record, NULL, true));
    CHECK(boot_policy_candidate_allowed(NULL, &candidate, false));
    for (attempt = 0; attempt < 3; ++attempt) {
        record.attempts = attempt;
        CHECK(boot_policy_candidate_allowed(&record, &candidate, false));
    }
    record.attempts = 3;
    original_record = record;
    original_candidate = candidate;
    CHECK(!boot_policy_candidate_allowed(&record, &candidate, false));
    CHECK(boot_policy_candidate_allowed(&record, &candidate, true));
    CHECK(memcmp(&record, &original_record, sizeof(record)) == 0);
    CHECK(memcmp(&candidate, &original_candidate, sizeof(candidate)) == 0);
    for (byte = 0; byte < 252; ++byte) {
        candidate.raw[byte] ^= 1;
        CHECK(boot_policy_candidate_allowed(&record, &candidate, false));
        candidate.raw[byte] ^= 1;
    }
    for (byte = 252; byte < 256; ++byte) {
        candidate.raw[byte] ^= 0xFF;
        CHECK(!boot_policy_candidate_allowed(&record, &candidate, false));
    }
    record.attempts = 0xFFFFFFFFu;
    CHECK(!boot_policy_candidate_allowed(&record, &candidate, false));
    for (state = OTA_STATE_IDLE; state <= OTA_STATE_FAILED; ++state) {
        if (state != OTA_STATE_TRIAL) {
            record.state = (ota_state_t)state;
            CHECK(boot_policy_candidate_allowed(&record, &candidate, false));
        }
    }
}

/** 功能：验证启动许可必须等待真实日志提交成功，应答丢失也不允许启动。参数：无。返回：无。 */
static void test_trial_commit_permission(void) {
    policy_eeprom_t eeprom;
    policy_eeprom_t baseline;
    ota_journal_io_t io = {policy_eeprom_read, policy_eeprom_write, &eeprom};
    ota_manifest_t installed;
    ota_record_t record;
    ota_record_t original;
    ota_record_t loaded;
    memset(&eeprom, 0, sizeof(eeprom));
    memset(eeprom.bytes, 0xFF, sizeof(eeprom.bytes));
    fixture(&installed, &record);
    record.attempts = 2;
    CHECK(ota_journal_save(&io, &record) == OTA_STORE_OK);
    eeprom.writes = 0;
    baseline = eeprom;
    original = record;
    CHECK(boot_policy_commit_trial(&io, &record, &installed, false));
    CHECK(record.attempts == 3 && record.generation == original.generation + 1);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK && loaded.attempts == 3);

    eeprom = baseline;
    eeprom.fail_write = 1;
    record = original;
    CHECK(!boot_policy_commit_trial(&io, &record, &installed, false));
    CHECK(memcmp(&record, &original, sizeof(record)) == 0);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK && loaded.attempts == 2);

    eeprom = baseline;
    eeprom.fail_write = 5;
    eeprom.lost_ack = true;
    record = original;
    CHECK(!boot_policy_commit_trial(&io, &record, &installed, false));
    CHECK(memcmp(&record, &original, sizeof(record)) == 0);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK && loaded.attempts == 3);
    CHECK(boot_policy_action(OTA_STORE_OK, &loaded, &installed, false) == BOOT_RECOVER);

    eeprom = baseline;
    record = original;
    record.attempts = 3;
    CHECK(!boot_policy_commit_trial(&io, &record, &installed, false));
    CHECK(eeprom.writes == 0 && record.attempts == 3);
    record = original;
    record.manifest[251] ^= 1;
    CHECK(!boot_policy_commit_trial(&io, &record, &installed, false));
    CHECK(eeprom.writes == 0);
    CHECK(!boot_policy_commit_trial(NULL, &record, &installed, false));
    CHECK(!boot_policy_commit_trial(&io, NULL, &installed, true));
    CHECK(!boot_policy_commit_trial(&io, &record, NULL, true));

    memset(&eeprom, 0, sizeof(eeprom));
    memset(eeprom.bytes, 0xFF, sizeof(eeprom.bytes));
    memset(&record, 0, sizeof(record));
    CHECK(boot_policy_commit_trial(&io, &record, &installed, true));
    CHECK(record.state == OTA_STATE_TRIAL && record.attempts == 1);
    CHECK(memcmp(record.manifest, installed.raw, sizeof(record.manifest)) == 0);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK && loaded.attempts == 1);
    record.state = OTA_STATE_CONFIRMED;
    record.attempts = 9;
    record.error = 99;
    CHECK(boot_policy_commit_trial(&io, &record, &installed, true));
    CHECK(record.state == OTA_STATE_TRIAL && record.attempts == 1 && record.error == 0);
}

/** 功能：运行纯启动策略回归。参数：无。返回：进程状态。 */
int main(void) {
    test_full_validation_and_force();
    test_store_outcomes();
    test_state_and_identity();
    test_three_attempts();
    test_exhausted_candidate();
    test_trial_commit_permission();
    puts("Boot policy passed: validation/force/store/all states/252-byte identity/three attempts/candidate loop guard/verified trial commit");
    return 0;
}
