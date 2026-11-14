#include "ota_journal.h"
#include <string.h>

#define JOURNAL_SLOT_A 0x0800u
#define JOURNAL_SLOT_B 0x0C00u
#define JOURNAL_SLOT_SIZE 1024u
#define JOURNAL_BODY_SIZE 384u
#define JOURNAL_PAGE_SIZE 128u
#define JOURNAL_COMMIT_OFFSET 0x380u

/** 保存一次完整槽扫描，避免把读取失败误判为未初始化。 */
typedef struct {
    uint8_t body[JOURNAL_BODY_SIZE]; // 完整状态头和目标包头
    ota_record_t record; // 已解析的记录
    bool blank; // 整个一千零二十四字节槽是否全为擦除值
    bool valid; // 记录及独立提交页是否一致有效
} journal_slot_t;

/** 功能：读取显式小端字段。参数：四字节地址。返回：整数。 */
static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/** 功能：写入显式小端字段。参数：四字节地址和值。返回：无。 */
static void write_u32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

/** 功能：检查保留区或整页固定填充值。参数：数据、长度和值。返回：全部匹配时为真。 */
static bool bytes_are(const uint8_t *data, size_t length, uint8_t value) {
    size_t index;
    for (index = 0; index < length; ++index) {
        if (data[index] != value) {
            return false;
        }
    }
    return true;
}

/** 功能：计算跳过自身字段的标准日志 CRC32。参数：三百八十四字节槽体。返回：校验值。 */
static uint32_t body_crc(const uint8_t body[JOURNAL_BODY_SIZE]) {
    uint32_t crc = 0xFFFFFFFFu;
    size_t index;
    unsigned bit;
    for (index = 0; index < JOURNAL_BODY_SIZE; ++index) {
        if (index >= 16 && index < 20) {
            continue;
        }
        crc ^= body[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) != 0 ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

/** 功能：检查并解析槽体及独立提交页。参数：扫描槽和完整提交页。返回：无。 */
static void decode_slot(journal_slot_t *slot, const uint8_t commit[JOURNAL_PAGE_SIZE]) {
    const uint8_t *body = slot->body;
    uint32_t generation = read_u32(body + 12);
    uint32_t crc = read_u32(body + 16);
    bool manifest_empty = bytes_are(body + 128, 256, 0);
    slot->valid = false;
    if (memcmp(body, "OTAJ", 4) != 0 || body[4] != 1 || body[5] != 0 || body[6] != 128 || body[7] != 0 ||
        read_u32(body + 8) != JOURNAL_BODY_SIZE || read_u32(body + 20) > OTA_STATE_FAILED ||
        !bytes_are(body + 48, 80, 0) || crc != body_crc(body) ||
        memcmp(commit, "OTAC", 4) != 0 || read_u32(commit + 4) != generation ||
        read_u32(commit + 8) != crc || read_u32(commit + 12) != ~generation || !bytes_are(commit + 16, 112, 0)) {
        return;
    }
    if ((manifest_empty && !bytes_are(body + 32, 16, 0)) ||
        (!manifest_empty && memcmp(body + 32, body + 128 + 96, 16) != 0)) {
        return;
    }
    memset(&slot->record, 0, sizeof(slot->record));
    slot->record.generation = generation;
    slot->record.state = (ota_state_t)read_u32(body + 20);
    slot->record.attempts = read_u32(body + 24);
    slot->record.error = read_u32(body + 28);
    memcpy(slot->record.manifest, body + 128, 256);
    slot->valid = true;
}

/** 功能：按页扫描完整槽，包含未分配空隙。参数：访问接口、槽地址和输出。返回：所有读取成功时为真。 */
static bool scan_slot(const ota_journal_io_t *io, uint32_t address, journal_slot_t *slot) {
    uint8_t page[JOURNAL_PAGE_SIZE];
    uint8_t commit[JOURNAL_PAGE_SIZE];
    uint32_t offset;
    slot->blank = true;
    slot->valid = false;
    for (offset = 0; offset < JOURNAL_SLOT_SIZE; offset += JOURNAL_PAGE_SIZE) {
        if (!io->read(io->context, address + offset, page, sizeof(page))) {
            return false;
        }
        if (!bytes_are(page, sizeof(page), 0xFF)) {
            slot->blank = false;
        }
        if (offset < JOURNAL_BODY_SIZE) {
            memcpy(slot->body + offset, page, sizeof(page));
        }
        if (offset == JOURNAL_COMMIT_OFFSET) {
            memcpy(commit, page, sizeof(commit));
        }
    }
    decode_slot(slot, commit);
    return true;
}

/** 功能：严格扫描两槽并选择可比较的最新记录。参数：访问接口、两槽输出和选中编号。返回：扫描状态。 */
static ota_store_result_t scan_journal(const ota_journal_io_t *io, journal_slot_t slots[2], unsigned *selected) {
    uint32_t difference;
    if (!scan_slot(io, JOURNAL_SLOT_A, &slots[0]) || !scan_slot(io, JOURNAL_SLOT_B, &slots[1])) {
        return OTA_STORE_IO;
    }
    if (slots[0].blank && slots[1].blank) {
        return OTA_STORE_EMPTY;
    }
    if (!slots[0].valid && !slots[1].valid) {
        return OTA_STORE_CORRUPT;
    }
    if (!slots[0].valid || !slots[1].valid) {
        *selected = slots[0].valid ? 0u : 1u;
        return OTA_STORE_OK;
    }
    difference = slots[0].record.generation - slots[1].record.generation;
    if (difference == 0) {
        if (memcmp(slots[0].body, slots[1].body, JOURNAL_BODY_SIZE) != 0) {
            return OTA_STORE_CORRUPT;
        }
        *selected = 0;
    } else if (difference == 0x80000000u) {
        return OTA_STORE_CORRUPT;
    } else {
        *selected = difference < 0x80000000u ? 0u : 1u;
    }
    return OTA_STORE_OK;
}

/** 功能：将记录编码为固定小端槽体。参数：输入记录、新代数和输出槽体。返回：无。 */
static void encode_record(const ota_record_t *record, uint32_t generation, uint8_t body[JOURNAL_BODY_SIZE]) {
    memset(body, 0, JOURNAL_BODY_SIZE);
    memcpy(body, "OTAJ", 4);
    body[4] = 1;
    body[6] = 128;
    write_u32(body + 8, JOURNAL_BODY_SIZE);
    write_u32(body + 12, generation);
    write_u32(body + 20, (uint32_t)record->state);
    write_u32(body + 24, record->attempts);
    write_u32(body + 28, record->error);
    memcpy(body + 128, record->manifest, 256);
    if (!bytes_are(record->manifest, 256, 0)) {
        memcpy(body + 32, record->manifest + 96, 16);
    }
    write_u32(body + 16, body_crc(body));
}

/** 功能：写入后立即回读验证一页。参数：访问接口、地址、数据和长度。返回：写应答及全部回读一致时为真。 */
static bool write_verified(const ota_journal_io_t *io, uint32_t address, const uint8_t *data, size_t length) {
    uint8_t verify[JOURNAL_PAGE_SIZE];
    if (!io->write(io->context, address, data, length)) {
        return false;
    }
    return io->read(io->context, address, verify, length) && memcmp(data, verify, length) == 0;
}

/** 功能：读取最新可信记录，失败不修改输出。参数：访问接口和输出记录，事务锁由调用者持有。返回：明确的存储状态。 */
ota_store_result_t ota_journal_load(const ota_journal_io_t *io, ota_record_t *out) {
    journal_slot_t slots[2];
    ota_store_result_t result;
    unsigned selected = 0;
    if (io == NULL || io->read == NULL || out == NULL) {
        return OTA_STORE_INVALID;
    }
    result = scan_journal(io, slots, &selected);
    if (result == OTA_STORE_OK) {
        *out = slots[selected].record;
    }
    return result;
}

/** 功能：提交到另一槽并重扫确认，任何写应答丢失均拒绝成功。参数：访问接口和记录，调用者完整持有事务锁。返回：完全持久化后为成功，成功时才更新代数。 */
ota_store_result_t ota_journal_save(const ota_journal_io_t *io, ota_record_t *record) {
    journal_slot_t slots[2];
    uint8_t body[JOURNAL_BODY_SIZE];
    uint8_t commit[JOURNAL_PAGE_SIZE];
    ota_store_result_t result;
    uint32_t generation;
    uint32_t address;
    uint32_t offset;
    unsigned selected = 0;
    unsigned target;
    if (io == NULL || io->read == NULL || io->write == NULL || record == NULL || (unsigned)record->state > OTA_STATE_FAILED) {
        return OTA_STORE_INVALID;
    }
    result = scan_journal(io, slots, &selected);
    if (result != OTA_STORE_OK && result != OTA_STORE_EMPTY) {
        return result;
    }
    target = result == OTA_STORE_EMPTY ? 0u : 1u - selected;
    generation = result == OTA_STORE_EMPTY ? 1u : slots[selected].record.generation + 1u;
    address = target == 0 ? JOURNAL_SLOT_A : JOURNAL_SLOT_B;
    encode_record(record, generation, body);
    memset(commit, 0, sizeof(commit));
    if (!write_verified(io, address + JOURNAL_COMMIT_OFFSET, commit, sizeof(commit))) {
        return OTA_STORE_IO;
    }
    for (offset = 0; offset < JOURNAL_BODY_SIZE; offset += JOURNAL_PAGE_SIZE) {
        if (!write_verified(io, address + offset, body + offset, JOURNAL_PAGE_SIZE)) {
            return OTA_STORE_IO;
        }
    }
    memcpy(commit, "OTAC", 4);
    write_u32(commit + 4, generation);
    write_u32(commit + 8, read_u32(body + 16));
    write_u32(commit + 12, ~generation);
    if (!write_verified(io, address + JOURNAL_COMMIT_OFFSET, commit, sizeof(commit))) {
        return OTA_STORE_IO;
    }
    result = scan_journal(io, slots, &selected);
    if (result != OTA_STORE_OK) {
        return result;
    }
    if (selected != target || memcmp(slots[selected].body, body, sizeof(body)) != 0) {
        return OTA_STORE_IO;
    }
    *record = slots[selected].record;
    return OTA_STORE_OK;
}
