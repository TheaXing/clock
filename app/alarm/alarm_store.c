#include "alarm_store.h"
#include <string.h>

/* 版本一序列化格式：所有整数均为小端字节序，不直接保存 C 结构体。
 * 偏移 0：魔数 ALRM，u32；4：版本，u16；6：头长度，u16；
 * 8：载荷长度，u32；12：代数，u32；16：CRC32，u32。
 * 载荷含八个条目，每项依次为年份、响铃秒数、贪睡秒数三个 u16，以及月、日、小时、
 * 分钟、重复规则、启用标志六个 u8。CRC32/ISO-HDLC 覆盖 [0,16) 和 [20,116)。
 * 最后一个独立 EEPROM 页保存 ALMC、代数、CRC 和代数按位取反值。
 * 地址 2048 及以上的 EEPROM 字节不属于此日志。 */
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

/**
 * 功能：从字节缓冲区读取小端 16 位无符号整数。
 * 参数：p，至少包含两个字节的输入缓冲区。
 * 返回：解码后的整数。
 */
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}
/**
 * 功能：从字节缓冲区读取小端 32 位无符号整数。
 * 参数：p，至少包含四个字节的输入缓冲区。
 * 返回：解码后的整数。
 */
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/**
 * 功能：将 16 位无符号整数按小端顺序写入缓冲区。
 * 参数：p，至少两个字节的输出缓冲区；value，待写入整数。
 * 返回：无。
 */
static void put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
/**
 * 功能：将 32 位无符号整数按小端顺序写入缓冲区。
 * 参数：p，至少四个字节的输出缓冲区；value，待写入整数。
 * 返回：无。
 */
static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
/**
 * 功能：计算闹钟记录的 CRC32，跳过记录内部的校验值字段。
 * 参数：record，完整的序列化记录。
 * 返回：记录的 CRC32/ISO-HDLC 校验值。
 */
static uint32_t record_crc(const uint8_t *record) {
    uint32_t crc = UINT32_MAX;
    size_t i;
    unsigned int bit;
    for (i = 0U; i < RECORD_BYTES; ++i) {
        if (i >= 16U && i < HEADER_BYTES) {
            continue;
        }
        crc ^= record[i];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) != 0U ? UINT32_C(0xedb88320) : 0U);
        }
    }
    return ~crc;
}
/**
 * 功能：按版本一格式编码完整闹钟记录并写入校验值。
 * 参数：record，记录输出缓冲区；config，待编码配置；generation，记录代数。
 * 返回：无。
 */
static void encode_record(uint8_t *record, const alarm_config_t *config, uint32_t generation) {
    size_t i;
    put32(record, RECORD_MAGIC);
    put16(record + 4U, SCHEMA_VERSION);
    put16(record + 6U, HEADER_BYTES);
    put32(record + 8U, PAYLOAD_BYTES);
    put32(record + 12U, generation);
    for (i = 0U; i < ALARM_COUNT; ++i) {
        const alarm_entry_t *entry = &config->entries[i];
        uint8_t *p = record + HEADER_BYTES + i * ENTRY_BYTES;
        put16(p, entry->year);
        put16(p + 2U, entry->ring_seconds);
        put16(p + 4U, entry->snooze_seconds);
        p[6] = entry->month;
        p[7] = entry->day;
        p[8] = entry->hour;
        p[9] = entry->minute;
        p[10] = entry->repeat_mask;
        p[11] = entry->enabled;
    }
    put32(record + 16U, record_crc(record));
}
/**
 * 功能：校验记录头、校验值和各闹钟字段，并按需解码配置。
 * 参数：record，输入记录；config，接收解码配置，为空时仅校验。
 * 返回：记录及全部闹钟有效时返回 true，否则返回 false。
 */
static bool decode_record(const uint8_t *record, alarm_config_t *config) {
    size_t i;
    if (get32(record) != RECORD_MAGIC || get16(record + 4U) != SCHEMA_VERSION ||
        get16(record + 6U) != HEADER_BYTES || get32(record + 8U) != PAYLOAD_BYTES ||
        get32(record + 16U) != record_crc(record)) {
        return false;
    }
    for (i = 0U; i < ALARM_COUNT; ++i) {
        alarm_entry_t decoded;
        alarm_entry_t *entry = &decoded;
        const uint8_t *p = record + HEADER_BYTES + i * ENTRY_BYTES;
        entry->year = get16(p);
        entry->ring_seconds = get16(p + 2U);
        entry->snooze_seconds = get16(p + 4U);
        entry->month = p[6];
        entry->day = p[7];
        entry->hour = p[8];
        entry->minute = p[9];
        entry->repeat_mask = p[10];
        entry->enabled = p[11];
        if (!alarm_entry_valid(entry)) {
            return false;
        }
        if (config != NULL) {
            config->entries[i] = decoded;
        }
    }
    return true;
}
/**
 * 功能：编码独立提交标记，绑定记录代数和校验值。
 * 参数：marker，提交标记输出缓冲区；generation，记录代数；crc，记录校验值。
 * 返回：无。
 */
static void encode_commit(uint8_t *marker, uint32_t generation, uint32_t crc) {
    put32(marker, COMMIT_MAGIC);
    put32(marker + 4U, generation);
    put32(marker + 8U, crc);
    put32(marker + 12U, ~generation);
}
/**
 * 功能：检查存储对象及其读写回调是否有效。
 * 参数：store，待检查的存储对象。
 * 返回：对象与两个回调均有效时返回 true，否则返回 false。
 */
static bool valid_io(const alarm_store_t *store) {
    return store != NULL && store->io.read != NULL && store->io.write != NULL;
}

/* 读取完整槽位；EMPTY 表示连未使用字节也全部擦除。
 * 缓冲区不超过一个 EEPROM 页，每次读取均受长度限制。 */
/**
 * 功能：逐页读取完整槽位，校验记录及独立提交标记并识别全擦除状态。
 * 参数：store，存储对象；slot，槽位编号；config，可选配置输出；generation，有效记录的代数输出。
 * 返回：槽位有效、为空、损坏或读取失败的状态。
 */
static alarm_store_result_t read_slot(const alarm_store_t *store, unsigned int slot,
                                      alarm_config_t *config, uint32_t *generation) {
    uint8_t page[PAGE_BYTES];
    uint32_t crc = 0U, gen = 0U, offset;
    bool empty = true, record_valid = false, committed = false;
    size_t i;
    for (offset = 0U; offset < SLOT_BYTES; offset += PAGE_BYTES) {
        if (!store->io.read(store->io.context, slot * SLOT_BYTES + offset, page, sizeof page)) {
            return ALARM_STORE_IO_ERROR;
        }
        for (i = 0U; i < sizeof page; ++i) {
            if (page[i] != 0xffU) {
                empty = false;
            }
        }
        if (offset == 0U) {
            record_valid = decode_record(page, config);
            gen = get32(page + 12U);
            crc = get32(page + 16U);
        }
        if (offset == COMMIT_OFFSET) {
            committed = get32(page) == COMMIT_MAGIC && get32(page + 4U) == gen &&
                        get32(page + 8U) == crc && get32(page + 12U) == ~gen;
        }
    }
    if (empty) {
        return ALARM_STORE_EMPTY;
    }
    if (!record_valid || !committed) {
        return ALARM_STORE_CORRUPT;
    }
    *generation = gen;
    return ALARM_STORE_OK;
}
/**
 * 功能：按 32 位回绕规则比较两个记录代数的新旧。
 * 参数：candidate，候选代数；reference，参考代数。
 * 返回：候选代数在半周期范围内较新时返回 true，否则返回 false。
 */
static bool newer(uint32_t candidate, uint32_t reference) {
    uint32_t distance = candidate - reference;
    return distance != 0U && distance < UINT32_C(0x80000000);
}
/**
 * 功能：扫描两个槽位并选择代数最新的有效记录。
 * 参数：store，存储对象；active_slot，选中槽位输出，无有效项为 -1；generation，选中代数输出；strict，任一槽位读失败即报错。
 * 返回：所选记录或扫描过程的存储状态。
 */
static alarm_store_result_t select_slot(const alarm_store_t *store, int *active_slot,
                                        uint32_t *generation, bool strict) {
    uint32_t candidate_generation = 0U;
    bool io_error = false, corrupt = false;
    unsigned int slot;
    *active_slot = -1;
    *generation = 0U;
    for (slot = 0U; slot < 2U; ++slot) {
        alarm_store_result_t result = read_slot(store, slot, NULL, &candidate_generation);
        if (result == ALARM_STORE_OK &&
            (*active_slot < 0 || newer(candidate_generation, *generation))) {
            *active_slot = (int)slot;
            *generation = candidate_generation;
        }
        if (result == ALARM_STORE_IO_ERROR) {
            io_error = true;
        }
        if (result == ALARM_STORE_CORRUPT) {
            corrupt = true;
        }
    }
    if (strict && io_error) {
        return ALARM_STORE_IO_ERROR;
    }
    if (*active_slot >= 0) {
        return ALARM_STORE_OK;
    }
    if (io_error) {
        return ALARM_STORE_IO_ERROR;
    }
    return corrupt ? ALARM_STORE_CORRUPT : ALARM_STORE_EMPTY;
}
/**
 * 功能：检查本次扫描是否丢失或回退到已知活动记录之前。
 * 参数：store，保存已知基线的存储对象；result，扫描结果；slot，扫描选中的槽位；generation，扫描选中的代数。
 * 返回：已知活动记录发生回退或矛盾时返回 true，否则返回 false。
 */
static bool generation_regressed(const alarm_store_t *store, alarm_store_result_t result, int slot,
                                 uint32_t generation) {
    return store->active_slot >= 0 &&
           (result != ALARM_STORE_OK ||
            (generation != store->generation && !newer(generation, store->generation)) ||
            (generation == store->generation && slot != store->active_slot));
}
/**
 * 功能：回读 EEPROM 并与预期字节逐一比较。
 * 参数：store，存储对象；address，回读起始地址；expected，预期数据；length，比较长度，不超过一页。
 * 返回：读取成功且全部相等时返回 true，否则返回 false。
 */
static bool verify_bytes(const alarm_store_t *store, uint32_t address, const uint8_t *expected,
                         size_t length) {
    uint8_t actual[PAGE_BYTES];
    return store->io.read(store->io.context, address, actual, length) &&
           memcmp(actual, expected, length) == 0;
}
/**
 * 功能：绑定 EEPROM 读写接口并清空活动槽位和更新基线。
 * 参数：store，待初始化的存储对象；io，读写回调及上下文。
 * 返回：无。
 */
void alarm_store_init(alarm_store_t *store, alarm_store_io_t io) {
    if (store == NULL) {
        return;
    }
    store->io = io;
    store->generation = 0U;
    store->active_slot = -1;
    store->update_baseline = false;
}
/**
 * 功能：选择并复读最新记录，在严格模式下检查代数回退并捕获更新基线。
 * 参数：store，存储对象；config，配置输出；strict，是否启用更新用的严格读取。
 * 返回：读取和校验结果；成功时更新配置及存储状态。
 */
static alarm_store_result_t load_config(alarm_store_t *store, alarm_config_t *config, bool strict) {
    alarm_config_t selected;
    int slot;
    uint32_t generation;
    alarm_store_result_t result;
    if (!valid_io(store) || config == NULL) {
        return ALARM_STORE_INVALID;
    }
    result = select_slot(store, &slot, &generation, strict);
    if (result == ALARM_STORE_IO_ERROR) {
        return result;
    }
    if (strict && generation_regressed(store, result, slot, generation)) {
        return ALARM_STORE_IO_ERROR;
    }
    if (result == ALARM_STORE_OK) {
        uint32_t decoded_generation;
        result = read_slot(store, (unsigned int)slot, &selected, &decoded_generation);
        /* 扫描刚确认该槽位有效；复读矛盾属于读写不一致，不允许据此合并默认配置。 */
        if (strict && result != ALARM_STORE_OK) {
            return ALARM_STORE_IO_ERROR;
        }
        if (result == ALARM_STORE_OK && decoded_generation != generation) {
            return ALARM_STORE_IO_ERROR;
        }
        if (result == ALARM_STORE_OK) {
            *config = selected;
            store->active_slot = slot;
            store->generation = generation;
            store->update_baseline = strict;
        }
    } else if (strict && (result == ALARM_STORE_EMPTY || result == ALARM_STORE_CORRUPT)) {
        store->update_baseline = true;
    }
    return result;
}
/**
 * 功能：载入最新可读有效配置，允许启动时从另一有效槽位恢复。
 * 参数：store，存储对象；config，成功时接收配置。
 * 返回：配置加载结果。
 */
alarm_store_result_t alarm_store_load(alarm_store_t *store, alarm_config_t *config) {
    return load_config(store, config, false);
}
/**
 * 功能：严格读取待修改的持久化配置并捕获后续保存必须匹配的基线。
 * 参数：store，存储对象；config，成功时接收配置。
 * 返回：严格读取结果；空或损坏时保留输出并记录无有效基线。
 */
alarm_store_result_t alarm_store_load_for_update(alarm_store_t *store, alarm_config_t *config) {
    return load_config(store, config, true);
}
/**
 * 功能：重新确认基线，将配置写入备用槽位并逐段回读验证后提交。
 * 参数：store，存储对象；config，待保存的完整有效配置。
 * 返回：全部写入和验证成功返回 ALARM_STORE_OK，否则返回对应错误。
 */
alarm_store_result_t alarm_store_save(alarm_store_t *store, const alarm_config_t *config) {
    uint8_t page[PAGE_BYTES];
    uint32_t generation, address, crc;
    int active_slot, target_slot;
    alarm_store_result_t result;
    if (!valid_io(store) || config == NULL || !alarm_config_valid(config)) {
        return ALARM_STORE_INVALID;
    }
    /* 即使加载、保存失败或最终确认丢失，写入前仍重新扫描。可读的有效槽位始终保留，
     * 任一槽位读取失败均禁止全部写入，因为未读槽位可能包含更新配置。
     * 已持久化但确认丢失的提交可能在下次启动出现；仅当本函数返回 OK 后，调用者才可
     * 将新配置应用到内存。 */
    result = select_slot(store, &active_slot, &generation, true);
    if (result == ALARM_STORE_IO_ERROR) {
        return result;
    }
    /* 瞬时读取损坏不得使已知活动代数降级并成为写入目标。
     * 新的加载可显式恢复较旧的有效代数，但保存不得悄然如此处理。 */
    if (generation_regressed(store, result, active_slot, generation)) {
        return ALARM_STORE_IO_ERROR;
    }
    /* 严格读取和合并仅对实际读取的快照有效，包括空或损坏基线。
     * 新发现的有效数据必须先读取并合并，不能被默认值覆盖。 */
    if (store->update_baseline &&
        (active_slot != store->active_slot || generation != store->generation)) {
        return ALARM_STORE_IO_ERROR;
    }
    target_slot = active_slot == 0 ? 1 : 0;
    address = (uint32_t)target_slot * SLOT_BYTES;
    ++generation;
    /* 先使整个独立提交标记页失效并回读验证，防止旧标记认证部分重写的记录。
     * EEPROM 页写完成及应答轮询由写回调负责。 */
    memset(page, 0, sizeof page);
    if (!store->io.write(store->io.context, address + COMMIT_OFFSET, page, sizeof page) ||
        !verify_bytes(store, address + COMMIT_OFFSET, page, sizeof page)) {
        return ALARM_STORE_IO_ERROR;
    }
    encode_record(page, config, generation);
    crc = get32(page + 16U);
    if (!store->io.write(store->io.context, address, page, RECORD_BYTES) ||
        !verify_bytes(store, address, page, RECORD_BYTES)) {
        return ALARM_STORE_IO_ERROR;
    }
    encode_commit(page, generation, crc);
    if (!store->io.write(store->io.context, address + COMMIT_OFFSET, page, COMMIT_BYTES) ||
        !verify_bytes(store, address + COMMIT_OFFSET, page, COMMIT_BYTES)) {
        return ALARM_STORE_IO_ERROR;
    }
    store->generation = generation;
    store->active_slot = target_slot;
    store->update_baseline = false;
    return ALARM_STORE_OK;
}
