#include "alarm_store.h"
#include <string.h>

/* Version 1 wire format; all integers are little endian, never C structures.
 * 0: magic "ALRM" u32, 4: version u16, 6: header size u16,
 * 8: payload size u32, 12: generation u32, 16: CRC32 u32.
 * Payload: eight entries of year/ring/snooze u16, then month/day/hour/minute/
 * repeat/enabled u8. CRC32/ISO-HDLC covers bytes [0,16) and [20,116).
 * The last, separate EEPROM page holds "ALMC", generation, CRC, ~generation.
 * Nothing at or above EEPROM byte 2048 belongs to this journal.
 */
#define SLOT_BYTES 1024U
#define PAGE_BYTES 128U
#define COMMIT_OFFSET 896U
#define COMMIT_BYTES 16U
#define HEADER_BYTES 20U
#define ENTRY_BYTES 12U
#define PAYLOAD_BYTES (ALARM_COUNT * ENTRY_BYTES)
#define RECORD_BYTES (HEADER_BYTES + PAYLOAD_BYTES)
#define RECORD_MAGIC UINT32_C(0x4d524c41)
#define COMMIT_MAGIC UINT32_C(0x434d4c41)
#define SCHEMA_VERSION 1U

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static uint32_t record_crc(const uint8_t *record)
{
    uint32_t crc = UINT32_MAX;
    size_t i;
    unsigned int bit;
    for (i = 0U; i < RECORD_BYTES; ++i) {
        if (i >= 16U && i < HEADER_BYTES) continue;
        crc ^= record[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) != 0U ? UINT32_C(0xedb88320) : 0U);
    }
    return ~crc;
}
static void encode_record(uint8_t *record, const alarm_config_t *config, uint32_t generation)
{
    size_t i;
    put32(record, RECORD_MAGIC);
    put16(record + 4U, SCHEMA_VERSION);
    put16(record + 6U, HEADER_BYTES);
    put32(record + 8U, PAYLOAD_BYTES);
    put32(record + 12U, generation);
    for (i = 0U; i < ALARM_COUNT; ++i) {
        const alarm_entry_t *entry = &config->entries[i];
        uint8_t *p = record + HEADER_BYTES + i * ENTRY_BYTES;
        put16(p, entry->year); put16(p + 2U, entry->ring_seconds);
        put16(p + 4U, entry->snooze_seconds);
        p[6] = entry->month; p[7] = entry->day;
        p[8] = entry->hour; p[9] = entry->minute;
        p[10] = entry->repeat_mask; p[11] = entry->enabled;
    }
    put32(record + 16U, record_crc(record));
}
static bool decode_record(const uint8_t *record, alarm_config_t *config)
{
    size_t i;
    if (get32(record) != RECORD_MAGIC || get16(record + 4U) != SCHEMA_VERSION ||
        get16(record + 6U) != HEADER_BYTES || get32(record + 8U) != PAYLOAD_BYTES ||
        get32(record + 16U) != record_crc(record)) return false;
    for (i = 0U; i < ALARM_COUNT; ++i) {
        alarm_entry_t decoded;
        alarm_entry_t *entry = &decoded;
        const uint8_t *p = record + HEADER_BYTES + i * ENTRY_BYTES;
        entry->year = get16(p); entry->ring_seconds = get16(p + 2U);
        entry->snooze_seconds = get16(p + 4U);
        entry->month = p[6]; entry->day = p[7];
        entry->hour = p[8]; entry->minute = p[9];
        entry->repeat_mask = p[10]; entry->enabled = p[11];
        if (!alarm_entry_valid(entry)) return false;
        if (config != NULL) config->entries[i] = decoded;
    }
    return true;
}
static void encode_commit(uint8_t *marker, uint32_t generation, uint32_t crc)
{
    put32(marker, COMMIT_MAGIC); put32(marker + 4U, generation);
    put32(marker + 8U, crc); put32(marker + 12U, ~generation);
}
static bool valid_io(const alarm_store_t *store)
{
    return store != NULL && store->io.read != NULL && store->io.write != NULL;
}

/* Read the entire slot: EMPTY means genuinely erased, including unused bytes.
 * The buffer never exceeds one EEPROM page and every read is bounded.
 */
static alarm_store_result_t read_slot(const alarm_store_t *store, unsigned int slot,
                                     alarm_config_t *config, uint32_t *generation)
{
    uint8_t page[PAGE_BYTES];
    uint32_t crc = 0U, gen = 0U, offset;
    bool empty = true, record_valid = false, committed = false;
    size_t i;
    for (offset = 0U; offset < SLOT_BYTES; offset += PAGE_BYTES) {
        if (!store->io.read(store->io.context, slot * SLOT_BYTES + offset, page, sizeof page))
            return ALARM_STORE_IO_ERROR;
        for (i = 0U; i < sizeof page; ++i)
            if (page[i] != 0xffU) empty = false;
        if (offset == 0U) {
            record_valid = decode_record(page, config);
            gen = get32(page + 12U); crc = get32(page + 16U);
        }
        if (offset == COMMIT_OFFSET)
            committed = get32(page) == COMMIT_MAGIC && get32(page + 4U) == gen &&
                        get32(page + 8U) == crc && get32(page + 12U) == ~gen;
    }
    if (empty) return ALARM_STORE_EMPTY;
    if (!record_valid || !committed) return ALARM_STORE_CORRUPT;
    *generation = gen;
    return ALARM_STORE_OK;
}
static bool newer(uint32_t candidate, uint32_t reference)
{
    uint32_t distance = candidate - reference;
    return distance != 0U && distance < UINT32_C(0x80000000);
}
static alarm_store_result_t select_slot(const alarm_store_t *store,
                                       int *active_slot, uint32_t *generation, bool strict)
{
    uint32_t candidate_generation = 0U;
    bool io_error = false, corrupt = false;
    unsigned int slot;
    *active_slot = -1; *generation = 0U;
    for (slot = 0U; slot < 2U; ++slot) {
        alarm_store_result_t result = read_slot(store, slot, NULL, &candidate_generation);
        if (result == ALARM_STORE_OK && (*active_slot < 0 || newer(candidate_generation, *generation))) {
            *active_slot = (int)slot; *generation = candidate_generation;
        }
        if (result == ALARM_STORE_IO_ERROR) io_error = true;
        if (result == ALARM_STORE_CORRUPT) corrupt = true;
    }
    if (strict && io_error) return ALARM_STORE_IO_ERROR;
    if (*active_slot >= 0) return ALARM_STORE_OK;
    if (io_error) return ALARM_STORE_IO_ERROR;
    return corrupt ? ALARM_STORE_CORRUPT : ALARM_STORE_EMPTY;
}
static bool generation_regressed(const alarm_store_t *store, alarm_store_result_t result,
                                 int slot, uint32_t generation)
{
    return store->active_slot >= 0 &&
           (result != ALARM_STORE_OK ||
            (generation != store->generation && !newer(generation, store->generation)) ||
            (generation == store->generation && slot != store->active_slot));
}
static bool verify_bytes(const alarm_store_t *store, uint32_t address,
                         const uint8_t *expected, size_t length)
{
    uint8_t actual[PAGE_BYTES];
    return store->io.read(store->io.context, address, actual, length) &&
           memcmp(actual, expected, length) == 0;
}
void alarm_store_init(alarm_store_t *store, alarm_store_io_t io)
{
    if (store == NULL) return;
    store->io = io; store->generation = 0U; store->active_slot = -1;
    store->update_baseline = false;
}
static alarm_store_result_t load_config(alarm_store_t *store, alarm_config_t *config, bool strict)
{
    alarm_config_t selected;
    int slot;
    uint32_t generation;
    alarm_store_result_t result;
    if (!valid_io(store) || config == NULL) return ALARM_STORE_INVALID;
    result = select_slot(store, &slot, &generation, strict);
    if (result == ALARM_STORE_IO_ERROR) return result;
    if (strict && generation_regressed(store, result, slot, generation)) return ALARM_STORE_IO_ERROR;
    if (result == ALARM_STORE_OK) {
        uint32_t decoded_generation;
        result = read_slot(store, (unsigned int)slot, &selected, &decoded_generation);
        /* The scan just proved this slot valid. A contradictory reread is
         * an I/O inconsistency, never permission to merge against defaults.
         */
        if (strict && result != ALARM_STORE_OK) return ALARM_STORE_IO_ERROR;
        if (result == ALARM_STORE_OK && decoded_generation != generation) return ALARM_STORE_IO_ERROR;
        if (result == ALARM_STORE_OK) {
            *config = selected; store->active_slot = slot; store->generation = generation;
            store->update_baseline = strict;
        }
    } else if (strict && (result == ALARM_STORE_EMPTY || result == ALARM_STORE_CORRUPT)) {
        store->update_baseline = true;
    }
    return result;
}
alarm_store_result_t alarm_store_load(alarm_store_t *store, alarm_config_t *config)
{
    return load_config(store, config, false);
}
alarm_store_result_t alarm_store_load_for_update(alarm_store_t *store, alarm_config_t *config)
{
    return load_config(store, config, true);
}
alarm_store_result_t alarm_store_save(alarm_store_t *store, const alarm_config_t *config)
{
    uint8_t page[PAGE_BYTES];
    uint32_t generation, address, crc;
    int active_slot, target_slot;
    alarm_store_result_t result;
    if (!valid_io(store) || config == NULL || !alarm_config_valid(config)) return ALARM_STORE_INVALID;
    /* Rescan before writing, even after failed loads/saves or a lost final ACK.
     * A readable valid slot always remains untouched. Any slot I/O failure
     * prevents all writes: an unread slot may contain a newer configuration.
     * A durable commit with lost ACK may appear on the next boot;
     * callers must apply a new RAM config only after this function returns OK.
     */
    result = select_slot(store, &active_slot, &generation, true);
    if (result == ALARM_STORE_IO_ERROR) return result;
    /* A transient damaged read must not demote a previously known active
     * generation and turn it into the write target. A fresh load may recover
     * an older valid generation explicitly, but save never silently does so.
     */
    if (generation_regressed(store, result, active_slot, generation))
        return ALARM_STORE_IO_ERROR;
    /* A strict read/merge is valid only for the snapshot it actually read.
     * This includes an empty/corrupt baseline: newly discovered valid data
     * must be read and merged rather than overwritten by defaults.
     */
    if (store->update_baseline &&
        (active_slot != store->active_slot || generation != store->generation))
        return ALARM_STORE_IO_ERROR;
    target_slot = active_slot == 0 ? 1 : 0;
    address = (uint32_t)target_slot * SLOT_BYTES;
    ++generation;
    /* Invalidate and verify the whole independent marker page first. Old
     * markers from earlier use must never authenticate a partial rewrite.
     * EEPROM page completion/ACK polling belongs to the write callback.
     */
    memset(page, 0, sizeof page);
    if (!store->io.write(store->io.context, address + COMMIT_OFFSET, page, sizeof page) ||
        !verify_bytes(store, address + COMMIT_OFFSET, page, sizeof page)) return ALARM_STORE_IO_ERROR;
    encode_record(page, config, generation);
    crc = get32(page + 16U);
    if (!store->io.write(store->io.context, address, page, RECORD_BYTES) ||
        !verify_bytes(store, address, page, RECORD_BYTES)) return ALARM_STORE_IO_ERROR;
    encode_commit(page, generation, crc);
    if (!store->io.write(store->io.context, address + COMMIT_OFFSET, page, COMMIT_BYTES) ||
        !verify_bytes(store, address + COMMIT_OFFSET, page, COMMIT_BYTES)) return ALARM_STORE_IO_ERROR;
    store->generation = generation; store->active_slot = target_slot;
    store->update_baseline = false;
    return ALARM_STORE_OK;
}


