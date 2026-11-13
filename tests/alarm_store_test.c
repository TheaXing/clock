#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alarm_store.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define EEPROM_SIZE 65536U
#define SLOT_SIZE 1024U
#define COMMIT_OFFSET 896U
#define RECORD_SIZE 116U
#define COMMIT_SIZE 16U

typedef struct {
    unsigned char memory[EEPROM_SIZE];
    size_t operation;
    size_t fail_operation;
    size_t corrupt_read_operation;
    size_t lost_ack_operation;
    size_t bytes_written;
    size_t cut_after;
    unsigned int blocked_slots;
    int power_lost;
    int write_protect;
} fake_eeprom_t;
static fake_eeprom_t chip;
static unsigned char original[EEPROM_SIZE];
static unsigned int scenarios;

static bool fake_read(void *context, uint32_t address, void *data, size_t length)
{
    fake_eeprom_t *e = context;
    ++e->operation;
    CHECK(address <= EEPROM_SIZE && length <= EEPROM_SIZE - address);
    if (e->power_lost || e->operation == e->fail_operation ||
        (address < 2048U && (e->blocked_slots & (1U << (address / SLOT_SIZE))) != 0U)) return false;
    memcpy(data, &e->memory[address], length);
    if (length != 0U && e->operation == e->corrupt_read_operation) ((unsigned char *)data)[0] ^= 1U;
    return true;
}
static bool fake_write(void *context, uint32_t address, const void *data, size_t length)
{
    fake_eeprom_t *e = context;
    const unsigned char *source = data;
    size_t i;
    ++e->operation;
    CHECK(address < 2048U && length <= 2048U - address);
    CHECK(length > 0U && address / 128U == (address + (uint32_t)length - 1U) / 128U);
    if (e->power_lost || e->operation == e->fail_operation) return false;
    for (i = 0U; i < length; ++i) {
        if (e->bytes_written == e->cut_after) { e->power_lost = 1; return false; }
        if (!e->write_protect) e->memory[address + i] = source[i];
        ++e->bytes_written;
    }
    return e->operation != e->lost_ack_operation;
}
static void clear_faults(void)
{
    chip.operation = 0U; chip.fail_operation = 0U; chip.corrupt_read_operation = 0U;
    chip.lost_ack_operation = 0U;
    chip.bytes_written = 0U; chip.cut_after = (size_t)-1; chip.blocked_slots = 0U;
    chip.power_lost = 0; chip.write_protect = 0;
}
static void reset_chip(void)
{
    memset(chip.memory, 0xff, sizeof chip.memory);
    clear_faults();
}
static void init(alarm_store_t *store)
{
    alarm_store_io_t io = { fake_read, fake_write, &chip };
    alarm_store_init(store, io);
}
static alarm_config_t config_at(unsigned int hour)
{
    alarm_config_t config;
    alarm_config_defaults(&config);
    config.entries[0].enabled = 1U;
    config.entries[0].year = 2026U;
    config.entries[0].month = 11U;
    config.entries[0].day = 13U;
    config.entries[0].hour = (uint8_t)hour;
    config.entries[0].minute = 37U;
    return config;
}
static bool config_equal(const alarm_config_t *a, const alarm_config_t *b)
{
    size_t i;
    for (i = 0U; i < ALARM_COUNT; ++i) {
        const alarm_entry_t *x = &a->entries[i], *y = &b->entries[i];
        if (x->year != y->year || x->ring_seconds != y->ring_seconds ||
            x->snooze_seconds != y->snooze_seconds || x->month != y->month ||
            x->day != y->day || x->hour != y->hour || x->minute != y->minute ||
            x->repeat_mask != y->repeat_mask || x->enabled != y->enabled) return false;
    }
    return true;
}
static void test_empty_and_round_trip(void)
{
    alarm_store_t store, reboot;
    alarm_config_t a = config_at(7U), out = config_at(1U), unchanged = out;
    reset_chip(); init(&store);
    CHECK(store.active_slot == -1);
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_EMPTY);
    CHECK(config_equal(&out, &unchanged));
    CHECK(alarm_store_save(&store, &a) == ALARM_STORE_OK);
    CHECK(store.active_slot == 0 && store.generation == 1U);
    init(&reboot);
    CHECK(alarm_store_load(&reboot, &out) == ALARM_STORE_OK);
    CHECK(config_equal(&out, &a));
    CHECK(reboot.active_slot == 0 && reboot.generation == 1U);
    CHECK(memcmp(chip.memory + 2048U, original + 2048U, EEPROM_SIZE - 2048U) == 0);
    ++scenarios;
}
static void test_every_written_byte_prefix(void);
static void test_every_operation_failure(void);
static void test_readback_corruption_and_write_protect(void);
static void test_invalid_arguments_are_unchanged(void);
static void test_schema_crc_ranges_and_marker(void);
static void test_read_failures_preserve_only_valid_slot(void);
static void test_generation_wrap_and_recovery_after_lost_ack(void);
static void test_damaged_active_read_then_torn_write(void);
static void test_first_save_and_second_slot_byte_prefixes(void);
static void test_all_eight_entries_and_little_endian(void);
static void test_fallback_and_corrupt_storage_repair(void);
static void test_load_failures_leave_output_unchanged(void);
static void test_strict_update_rejects_any_slot_io_and_known_regression(void);
static void test_strict_update_baseline_cannot_change_before_save(void);
static void test_strict_selected_reread_failure_is_not_default_recovery(void);
int main(void)
{
    memset(original, 0xff, sizeof original);
    test_empty_and_round_trip();
    test_strict_selected_reread_failure_is_not_default_recovery();
    test_strict_update_rejects_any_slot_io_and_known_regression();
    test_strict_update_baseline_cannot_change_before_save();
    test_load_failures_leave_output_unchanged();
    test_first_save_and_second_slot_byte_prefixes();
    test_all_eight_entries_and_little_endian();
    test_fallback_and_corrupt_storage_repair();
    test_damaged_active_read_then_torn_write();
    test_every_written_byte_prefix();
    test_every_operation_failure();
    test_readback_corruption_and_write_protect();
    test_invalid_arguments_are_unchanged();
    test_schema_crc_ranges_and_marker();
    test_read_failures_preserve_only_valid_slot();
    test_generation_wrap_and_recovery_after_lost_ack();
    printf("alarm store: %u scenarios passed\n", scenarios);
    return 0;
}

/* These tests break if commit ordering, slot selection, CRC validation, or
 * readback checking is removed. Faults occur at the EEPROM I/O boundary. */
static void put32_test(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16); p[3] = (unsigned char)(value >> 24);
}
static uint32_t test_crc_byte(uint32_t crc, unsigned char byte)
{
    unsigned int bit;
    crc ^= byte;
    for (bit = 0U; bit < 8U; ++bit) {
        if ((crc & 1U) != 0U) crc = (crc >> 1) ^ UINT32_C(0xedb88320);
        else crc >>= 1;
    }
    return crc;
}
static void rechecksum(unsigned int slot)
{
    unsigned char *body = chip.memory + slot * SLOT_SIZE;
    uint32_t crc = UINT32_MAX;
    size_t i;
    for (i = 0U; i < 16U; ++i) crc = test_crc_byte(crc, body[i]);
    for (i = 20U; i < RECORD_SIZE; ++i) crc = test_crc_byte(crc, body[i]);
    crc = ~crc;
    put32_test(body + 16U, crc);
    put32_test(body + COMMIT_OFFSET + 8U, crc);
}
static void set_generation(unsigned int slot, uint32_t generation)
{
    unsigned char *body = chip.memory + slot * SLOT_SIZE;
    put32_test(body + 12U, generation);
    put32_test(body + COMMIT_OFFSET + 4U, generation);
    put32_test(body + COMMIT_OFFSET + 12U, ~generation);
    rechecksum(slot);
}
static void prepare_reused_slots(alarm_store_t *store, alarm_config_t *current, alarm_config_t *next)
{
    alarm_config_t older = config_at(6U);
    *current = config_at(7U); *next = config_at(8U);
    reset_chip(); init(store);
    CHECK(alarm_store_save(store, &older) == ALARM_STORE_OK);
    CHECK(alarm_store_save(store, current) == ALARM_STORE_OK);
    CHECK(store->active_slot == 1 && store->generation == 2U);
    memcpy(original, chip.memory, sizeof original);
    clear_faults();
}
static void check_outside_reservation(void)
{
    CHECK(memcmp(chip.memory + 2048U, original + 2048U, EEPROM_SIZE - 2048U) == 0);
}
static void recover_old_or_new(const alarm_config_t *old, const alarm_config_t *next, bool success)
{
    alarm_store_t reboot;
    alarm_config_t out;
    clear_faults(); init(&reboot);
    CHECK(alarm_store_load(&reboot, &out) == ALARM_STORE_OK);
    CHECK(config_equal(&out, old) || config_equal(&out, next));
    if (success) CHECK(config_equal(&out, next));
    check_outside_reservation();
}
static void test_every_written_byte_prefix(void)
{
    alarm_store_t store, before;
    alarm_config_t current, next;
    size_t prefix;
    prepare_reused_slots(&store, &current, &next); before = store;
    for (prefix = 0U; prefix <= 128U + RECORD_SIZE + COMMIT_SIZE; ++prefix) {
        alarm_store_result_t result;
        memcpy(chip.memory, original, sizeof original); clear_faults(); store = before;
        chip.cut_after = prefix;
        result = alarm_store_save(&store, &next);
        if (prefix < 128U + RECORD_SIZE + COMMIT_SIZE) {
            CHECK(result == ALARM_STORE_IO_ERROR);
            CHECK(store.active_slot == before.active_slot && store.generation == before.generation);
        } else CHECK(result == ALARM_STORE_OK);
        CHECK(memcmp(chip.memory + SLOT_SIZE, original + SLOT_SIZE, SLOT_SIZE) == 0);
        recover_old_or_new(&current, &next, result == ALARM_STORE_OK);
        ++scenarios;
    }
}
static void test_every_operation_failure(void)
{
    alarm_store_t store, before;
    alarm_config_t current, next;
    size_t count, operation;
    prepare_reused_slots(&store, &current, &next); before = store;
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_OK);
    count = chip.operation;
    for (operation = 1U; operation <= count; ++operation) {
        alarm_store_result_t result;
        memcpy(chip.memory, original, sizeof original); clear_faults(); store = before;
        chip.fail_operation = operation;
        result = alarm_store_save(&store, &next);
        CHECK(result == ALARM_STORE_OK || result == ALARM_STORE_IO_ERROR);
        if (result != ALARM_STORE_OK) {
            CHECK(store.active_slot == before.active_slot && store.generation == before.generation);
            CHECK(memcmp(chip.memory + SLOT_SIZE, original + SLOT_SIZE, SLOT_SIZE) == 0);
        }
        recover_old_or_new(&current, &next, result == ALARM_STORE_OK);
        ++scenarios;
    }
}
static void test_readback_corruption_and_write_protect(void)
{
    alarm_store_t store, before;
    alarm_config_t current, next, out;
    size_t count, operation;
    prepare_reused_slots(&store, &current, &next); before = store;
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_OK);
    count = chip.operation;
    for (operation = 1U; operation <= count; ++operation) {
        alarm_store_result_t result;
        memcpy(chip.memory, original, sizeof original); clear_faults(); store = before;
        chip.corrupt_read_operation = operation;
        result = alarm_store_save(&store, &next);
        CHECK(result == ALARM_STORE_OK || result == ALARM_STORE_IO_ERROR || result == ALARM_STORE_CORRUPT);
        if (result != ALARM_STORE_OK) {
            CHECK(store.active_slot == before.active_slot && store.generation == before.generation);
            CHECK(memcmp(chip.memory + SLOT_SIZE, original + SLOT_SIZE, SLOT_SIZE) == 0);
        }
        recover_old_or_new(&current, &next, result == ALARM_STORE_OK);
        ++scenarios;
    }
    memcpy(chip.memory, original, sizeof original); clear_faults(); store = before;
    chip.write_protect = 1;
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
    CHECK(memcmp(chip.memory, original, sizeof original) == 0);
    clear_faults(); init(&store);
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK && config_equal(&out, &current));
    ++scenarios;
    /* An already-invalid marker cannot detect WP by itself: body readback must. */
    reset_chip(); init(&store); CHECK(alarm_store_save(&store, &current) == ALARM_STORE_OK);
    memset(chip.memory + SLOT_SIZE + COMMIT_OFFSET, 0, 128U);
    clear_faults(); chip.write_protect = 1;
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
    clear_faults(); init(&store);
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK && config_equal(&out, &current));
    ++scenarios;
}
static void test_invalid_arguments_are_unchanged(void)
{
    alarm_store_t store, before;
    alarm_config_t config = config_at(7U), out, unchanged;
    alarm_store_io_t bad = { NULL, NULL, NULL };
    reset_chip(); init(&store); before = store;
    alarm_store_init(NULL, bad);
    CHECK(alarm_store_load(NULL, &config) == ALARM_STORE_INVALID);
    CHECK(alarm_store_save(NULL, &config) == ALARM_STORE_INVALID);
    CHECK(alarm_store_load(&store, NULL) == ALARM_STORE_INVALID);
    CHECK(alarm_store_save(&store, NULL) == ALARM_STORE_INVALID);
    config.entries[0].hour = 24U;
    CHECK(alarm_store_save(&store, &config) == ALARM_STORE_INVALID);
    CHECK(chip.operation == 0U);
    CHECK(store.active_slot == before.active_slot && store.generation == before.generation);
    out = config_at(4U); unchanged = out;
    alarm_store_init(&store, bad);
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_INVALID);
    CHECK(config_equal(&out, &unchanged));
    CHECK(alarm_store_save(&store, &unchanged) == ALARM_STORE_INVALID);
    store.io.read = fake_read; store.io.context = &chip;
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_INVALID);
    store.io.read = NULL; store.io.write = fake_write;
    CHECK(alarm_store_save(&store, &unchanged) == ALARM_STORE_INVALID);
    ++scenarios;
}
static void test_schema_crc_ranges_and_marker(void)
{
    alarm_store_t store;
    alarm_config_t config = config_at(7U), out = config_at(1U), unchanged = out;
    /* Each mutation must be rejected even with a fresh valid CRC and marker. */
    static const struct { unsigned int offset; unsigned char value; } mutations[] = {
        {0U, 0U}, {4U, 2U}, {5U, 1U}, {6U, 19U}, {7U, 1U},
        {8U, 95U}, {9U, 1U}, {10U, 1U}, {11U, 1U},
        {20U, 0U}, {21U, 0U}, {22U, 0U}, {23U, 0xffU}, {25U, 0xffU}, {20U + 6U, 0U}, {20U + 7U, 32U}, {20U + 8U, 24U},
        {20U + 9U, 60U}, {20U + 10U, 128U}, {20U + 11U, 2U}
    };
    size_t i;
    reset_chip(); init(&store); CHECK(alarm_store_save(&store, &config) == ALARM_STORE_OK);
    memcpy(original, chip.memory, sizeof original);
    for (i = 0U; i < sizeof mutations / sizeof mutations[0]; ++i) {
        memcpy(chip.memory, original, sizeof original); clear_faults(); init(&store);
        chip.memory[mutations[i].offset] = mutations[i].value; rechecksum(0U);
        CHECK(alarm_store_load(&store, &out) == ALARM_STORE_CORRUPT);
        CHECK(config_equal(&out, &unchanged));
        ++scenarios;
    }
    for (i = 0U; i < COMMIT_SIZE; ++i) {
        memcpy(chip.memory, original, sizeof original); clear_faults(); init(&store);
        chip.memory[COMMIT_OFFSET + i] ^= 1U;
        CHECK(alarm_store_load(&store, &out) == ALARM_STORE_CORRUPT);
        CHECK(config_equal(&out, &unchanged)); ++scenarios;
    }
    memcpy(chip.memory, original, sizeof original); clear_faults(); init(&store);
    chip.memory[16U] ^= 1U;
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_CORRUPT); ++scenarios;
    memcpy(chip.memory, original, sizeof original); clear_faults(); init(&store);
    chip.memory[29U] ^= 1U;
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_CORRUPT); ++scenarios;
    reset_chip(); chip.memory[500U] = 0U; init(&store);
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_CORRUPT); ++scenarios;
}
static void test_read_failures_preserve_only_valid_slot(void)
{
    alarm_store_t store;
    alarm_config_t config = config_at(7U), next = config_at(8U), out = config_at(1U), unchanged = out;
    reset_chip(); init(&store); CHECK(alarm_store_save(&store, &config) == ALARM_STORE_OK);
    memcpy(original, chip.memory, sizeof original);
    clear_faults(); chip.blocked_slots = 2U; init(&store);
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK && config_equal(&out, &config));
    CHECK(store.active_slot == 0);
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
    CHECK(memcmp(chip.memory, original, SLOT_SIZE) == 0);
    recover_old_or_new(&config, &next, false); ++scenarios;
    memcpy(chip.memory, original, sizeof original); clear_faults(); chip.blocked_slots = 1U; init(&store);
    out = unchanged;
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_IO_ERROR && config_equal(&out, &unchanged));
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
    CHECK(chip.bytes_written == 0U && memcmp(chip.memory, original, sizeof original) == 0); ++scenarios;
    clear_faults(); chip.blocked_slots = 3U;
    CHECK(alarm_store_load(&store, &out) == ALARM_STORE_IO_ERROR); ++scenarios;
}
static void test_generation_wrap_and_recovery_after_lost_ack(void)
{
    alarm_store_t store, reboot;
    alarm_config_t current, next, out;
    prepare_reused_slots(&store, &current, &next);
    set_generation(0U, UINT32_MAX); set_generation(1U, 0U);
    init(&store); CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK);
    CHECK(config_equal(&out, &current) && store.generation == 0U && store.active_slot == 1);
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_OK && store.generation == 1U);
    init(&reboot); CHECK(alarm_store_load(&reboot, &out) == ALARM_STORE_OK && config_equal(&out, &next));
    ++scenarios;
    prepare_reused_slots(&store, &current, &next);
    /* Operation 22 is the final commit readback, after the write is durable. */
    chip.fail_operation = 22U;
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
    CHECK(store.generation == 2U && store.active_slot == 1);
    clear_faults(); init(&reboot);
    CHECK(alarm_store_load(&reboot, &out) == ALARM_STORE_OK && config_equal(&out, &next));
    CHECK(reboot.generation == 3U);
    /* A lost write ACK has the same durability ambiguity as lost readback. */
    prepare_reused_slots(&store, &current, &next);
    chip.lost_ack_operation = 21U;
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
    CHECK(store.generation == 2U && store.active_slot == 1);
    clear_faults(); init(&reboot);
    CHECK(alarm_store_load(&reboot, &out) == ALARM_STORE_OK && config_equal(&out, &next));
    CHECK(reboot.generation == 3U); ++scenarios;
    /* Without reboot, the following save must discover that durable generation. */
    CHECK(alarm_store_save(&store, &current) == ALARM_STORE_OK && store.generation == 4U);
    init(&reboot); CHECK(alarm_store_load(&reboot, &out) == ALARM_STORE_OK && config_equal(&out, &current));
    ++scenarios;
}

static void test_damaged_active_read_then_torn_write(void)
{
    alarm_store_t store;
    alarm_config_t current, next;
    prepare_reused_slots(&store, &current, &next);
    /* A transient corrupt read of the known active slot must not cause the
     * next failed write to destroy that slot and fall back two generations. */
    chip.corrupt_read_operation = 9U;
    chip.cut_after = 129U;
    CHECK(alarm_store_save(&store, &next) != ALARM_STORE_OK);
    CHECK(memcmp(chip.memory + SLOT_SIZE, original + SLOT_SIZE, SLOT_SIZE) == 0);
    recover_old_or_new(&current, &next, false); ++scenarios;
}

static void test_first_save_and_second_slot_byte_prefixes(void)
{
    alarm_store_t store, before, reboot;
    alarm_config_t config = config_at(7U), next = config_at(8U), out, unchanged = config_at(1U);
    size_t prefix;
    unsigned int mode;
    for (mode = 0U; mode < 2U; ++mode) {
        reset_chip(); init(&store);
        if (mode != 0U) CHECK(alarm_store_save(&store, &config) == ALARM_STORE_OK);
        before = store; memcpy(original, chip.memory, sizeof original);
        for (prefix = 0U; prefix <= 128U + RECORD_SIZE + COMMIT_SIZE; ++prefix) {
            alarm_store_result_t saved, loaded;
            memcpy(chip.memory, original, sizeof original); clear_faults(); store = before;
            chip.cut_after = prefix;
            saved = alarm_store_save(&store, &next);
            CHECK(saved == (prefix < 128U + RECORD_SIZE + COMMIT_SIZE ? ALARM_STORE_IO_ERROR : ALARM_STORE_OK));
            if (mode != 0U) CHECK(memcmp(chip.memory, original, SLOT_SIZE) == 0);
            clear_faults(); init(&reboot); out = unchanged;
            loaded = alarm_store_load(&reboot, &out);
            if (prefix == 128U + RECORD_SIZE + COMMIT_SIZE) {
                CHECK(loaded == ALARM_STORE_OK && config_equal(&out, &next));
            } else if (mode != 0U) {
                CHECK(loaded == ALARM_STORE_OK && config_equal(&out, &config));
            } else {
                CHECK(loaded == (prefix == 0U ? ALARM_STORE_EMPTY : ALARM_STORE_CORRUPT));
                CHECK(config_equal(&out, &unchanged));
            }
            check_outside_reservation(); ++scenarios;
        }
    }
}
static void test_all_eight_entries_and_little_endian(void)
{
    alarm_store_t store;
    alarm_config_t config = config_at(7U), out;
    unsigned int i;
    for (i = 0U; i < ALARM_COUNT; ++i) {
        alarm_entry_t *e = &config.entries[i];
        e->year = (uint16_t)(2026U + i); e->ring_seconds = (uint16_t)(300U + i);
        e->snooze_seconds = (uint16_t)(1800U + i); e->month = (uint8_t)(1U + i);
        e->day = (uint8_t)(13U + i); e->hour = (uint8_t)(7U + i);
        e->minute = (uint8_t)(37U + i); e->repeat_mask = (uint8_t)(1U << i);
        if (i == 7U) e->repeat_mask = 0U;
        e->enabled = (uint8_t)(i % 2U);
    }
    reset_chip(); init(&store); CHECK(alarm_store_save(&store, &config) == ALARM_STORE_OK);
    CHECK(chip.memory[20U] == 0xeaU && chip.memory[21U] == 0x07U);
    CHECK(chip.memory[22U] == 0x2cU && chip.memory[23U] == 0x01U);
    CHECK(chip.memory[24U] == 0x08U && chip.memory[25U] == 0x07U);
    CHECK(chip.memory[26U] == 1U && chip.memory[27U] == 13U);
    CHECK(chip.memory[28U] == 7U && chip.memory[29U] == 37U);
    CHECK(chip.memory[30U] == 1U && chip.memory[31U] == 0U);
    init(&store); CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK);
    CHECK(config_equal(&out, &config)); ++scenarios;
}
static void test_fallback_and_corrupt_storage_repair(void)
{
    alarm_store_t store;
    alarm_config_t current, next, out;
    prepare_reused_slots(&store, &current, &next);
    chip.memory[SLOT_SIZE + 29U] ^= 1U;
    init(&store); CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK);
    CHECK(out.entries[0].hour == 6U && store.active_slot == 0);
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_OK);
    init(&store); CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK && config_equal(&out, &next));
    ++scenarios;
    reset_chip(); chip.memory[500U] = 0U; chip.memory[SLOT_SIZE + 20U] = 0U;
    init(&store); CHECK(alarm_store_load(&store, &out) == ALARM_STORE_CORRUPT);
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_OK);
    init(&store); CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK && config_equal(&out, &next));
    ++scenarios;
}



static void test_load_failures_leave_output_unchanged(void)
{
    alarm_store_t store;
    alarm_config_t current, next, out, unchanged = config_at(1U);
    size_t count, operation;
    unsigned int corrupt;
    prepare_reused_slots(&store, &current, &next);
    init(&store); CHECK(alarm_store_load(&store, &out) == ALARM_STORE_OK);
    count = chip.operation;
    for (corrupt = 0U; corrupt < 2U; ++corrupt) {
        for (operation = 1U; operation <= count; ++operation) {
            alarm_store_result_t result;
            memcpy(chip.memory, original, sizeof original); clear_faults(); init(&store);
            out = unchanged;
            if (corrupt != 0U) chip.corrupt_read_operation = operation;
            else chip.fail_operation = operation;
            result = alarm_store_load(&store, &out);
            if (result == ALARM_STORE_OK) {
                CHECK(out.entries[0].hour == 6U || config_equal(&out, &current));
            } else {
                CHECK(result == ALARM_STORE_IO_ERROR || result == ALARM_STORE_CORRUPT);
                CHECK(config_equal(&out, &unchanged));
                CHECK(store.active_slot == -1 && store.generation == 0U);
            }
            CHECK(memcmp(chip.memory, original, sizeof original) == 0);
            ++scenarios;
        }
    }
}


static void test_strict_update_rejects_any_slot_io_and_known_regression(void)
{
    alarm_store_t store, before;
    alarm_config_t current, next, out = config_at(1U), unchanged = out;
    unsigned int blocked;
    prepare_reused_slots(&store, &current, &next); before = store;
    for (blocked = 1U; blocked <= 2U; ++blocked) {
        clear_faults(); store = before; chip.blocked_slots = blocked;
        CHECK(alarm_store_load_for_update(&store, &out) == ALARM_STORE_IO_ERROR);
        CHECK(config_equal(&out, &unchanged));
        CHECK(store.active_slot == before.active_slot && store.generation == before.generation);
        CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
        CHECK(chip.bytes_written == 0U && memcmp(chip.memory, original, sizeof original) == 0);
        ++scenarios;
    }
    clear_faults(); store = before; chip.corrupt_read_operation = 9U;
    CHECK(alarm_store_load_for_update(&store, &out) == ALARM_STORE_IO_ERROR);
    CHECK(config_equal(&out, &unchanged));
    CHECK(store.active_slot == before.active_slot && store.generation == before.generation);
    CHECK(chip.bytes_written == 0U); ++scenarios;
    /* Losing every previously known valid record is an error, never a claim
     * that defaults are now a safe baseline for overwriting other alarms. */
    clear_faults(); store = before;
    chip.memory[29U] ^= 1U; chip.memory[SLOT_SIZE + 29U] ^= 1U;
    CHECK(alarm_store_load_for_update(&store, &out) == ALARM_STORE_IO_ERROR);
    CHECK(config_equal(&out, &unchanged));
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
    CHECK(chip.bytes_written == 0U); ++scenarios;
    clear_faults(); init(&store);
    CHECK(alarm_store_load_for_update(NULL, &out) == ALARM_STORE_INVALID);
    CHECK(alarm_store_load_for_update(&store, NULL) == ALARM_STORE_INVALID); ++scenarios;
}
static void test_strict_update_baseline_cannot_change_before_save(void)
{
    alarm_store_t store, writer;
    alarm_config_t current, next, out = config_at(1U), unchanged = out;
    reset_chip(); init(&store);
    CHECK(alarm_store_load_for_update(&store, &out) == ALARM_STORE_EMPTY);
    CHECK(config_equal(&out, &unchanged));
    /* A valid record newly discovered after a default-based merge invalidates
     * that merge. The next strict load must supply the durable full snapshot. */
    init(&writer); current = config_at(7U); next = config_at(8U);
    current.entries[6].enabled = 1U;
    CHECK(alarm_store_save(&writer, &current) == ALARM_STORE_OK);
    memcpy(original, chip.memory, sizeof original); clear_faults();
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_IO_ERROR);
    CHECK(chip.bytes_written == 0U && memcmp(chip.memory, original, sizeof original) == 0);
    CHECK(alarm_store_load_for_update(&store, &out) == ALARM_STORE_OK);
    CHECK(config_equal(&out, &current)); ++scenarios;
    /* A valid strict baseline also cannot silently advance after the merge. */
    CHECK(alarm_store_save(&writer, &next) == ALARM_STORE_OK);
    memcpy(original, chip.memory, sizeof original); clear_faults();
    CHECK(alarm_store_save(&store, &out) == ALARM_STORE_IO_ERROR);
    CHECK(chip.bytes_written == 0U && memcmp(chip.memory, original, sizeof original) == 0);
    CHECK(alarm_store_load_for_update(&store, &out) == ALARM_STORE_OK);
    CHECK(config_equal(&out, &next));
    CHECK(alarm_store_save(&store, &out) == ALARM_STORE_OK); ++scenarios;
    /* Verified corrupt storage can be repaired after the same strict check. */
    reset_chip(); chip.memory[500U] = 0U; init(&store); out = unchanged;
    CHECK(alarm_store_load_for_update(&store, &out) == ALARM_STORE_CORRUPT);
    CHECK(config_equal(&out, &unchanged));
    CHECK(alarm_store_save(&store, &next) == ALARM_STORE_OK); ++scenarios;
}

static void test_strict_selected_reread_failure_is_not_default_recovery(void)
{
    alarm_store_t store, before;
    alarm_config_t current, next, out = config_at(1U), unchanged = out;
    prepare_reused_slots(&store, &current, &next); before = store;
    clear_faults(); chip.corrupt_read_operation = 17U;
    CHECK(alarm_store_load_for_update(&store, &out) == ALARM_STORE_IO_ERROR);
    CHECK(config_equal(&out, &unchanged));
    CHECK(store.active_slot == before.active_slot && store.generation == before.generation);
    CHECK(store.update_baseline == before.update_baseline); ++scenarios;
}

