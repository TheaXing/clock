#include "wifi_store.h"
#include <string.h>

#define STORE_BASE 0x1000U
#define SLOT_SIZE 256U
#define BODY_SIZE 128U
#define CRC_OFFSET 124U
#define BODY_MAGIC 0x49464957UL
#define COMMIT_MAGIC 0x54494d43UL

/** 保存一次双槽扫描的完整结果。 */
typedef struct {
    uint8_t bytes[2][SLOT_SIZE]; // 两个物理槽的原始内容
    bool readable[2]; // 各槽读取是否成功
    bool valid[2]; // 各槽记录是否有效
    int selected; // 最新有效槽编号，无记录时为负数
} store_scan_t;

/** 功能：读取小端整数；参数：bytes，四字节；返回：整数。 */
static uint32_t get_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8U |
           (uint32_t)bytes[2] << 16U | (uint32_t)bytes[3] << 24U;
}

/** 功能：写入小端整数；参数：bytes，目标；value，整数；返回：无。 */
static void put_u32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8U));
    }
}

/** 功能：计算标准反射 CRC32；参数：bytes，数据；length，字节数；返回：校验值。 */
static uint32_t crc32(const uint8_t *bytes, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320UL : 0U);
        }
    }
    return ~crc;
}

/** 功能：在数组界限内取得字符串长度；参数：text，字符串；capacity，容量；返回：长度或容量。 */
static size_t bounded_length(const char *text, size_t capacity) {
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

/** 功能：验证 UTF-8 标量并拒绝 ASCII 控制字符；参数：text，字节；length，长度；返回：有效标志。 */
static bool valid_utf8(const uint8_t *text, size_t length) {
    size_t i = 0U;
    while (i < length) {
        uint32_t scalar;
        uint32_t minimum;
        unsigned continuation;
        uint8_t first = text[i++];
        if (first < 0x80U) {
            if (first < 0x20U || first == 0x7fU) {
                return false;
            }
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            scalar = first & 0x1fU;
            minimum = 0x80U;
            continuation = 1U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            scalar = first & 0x0fU;
            minimum = 0x800U;
            continuation = 2U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            scalar = first & 0x07U;
            minimum = 0x10000U;
            continuation = 3U;
        } else {
            return false;
        }
        if (length - i < continuation) {
            return false;
        }
        for (unsigned j = 0U; j < continuation; ++j) {
            uint8_t next = text[i++];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            scalar = (scalar << 6U) | (next & 0x3fU);
        }
        if (scalar < minimum || scalar > 0x10ffffU ||
            (scalar >= 0xd800U && scalar <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

/** 功能：验证网络名称和密码的有界编码；参数：credentials，凭据；返回：有效时为真。 */
bool wifi_credentials_valid(const wifi_credentials_t *credentials) {
    size_t ssid_length;
    size_t password_length;
    if (credentials == NULL) {
        return false;
    }
    ssid_length = bounded_length(credentials->ssid, sizeof credentials->ssid);
    password_length = bounded_length(credentials->password, sizeof credentials->password);
    if (ssid_length == 0U || ssid_length > 32U || password_length > 63U ||
        (password_length != 0U && password_length < 8U) ||
        !valid_utf8((const uint8_t *)credentials->ssid, ssid_length)) {
        return false;
    }
    for (size_t i = 0U; i < password_length; ++i) {
        uint8_t byte = (uint8_t)credentials->password[i];
        if (byte < 0x20U || byte > 0x7eU) {
            return false;
        }
    }
    return true;
}

/** 功能：从已校验正文复制凭据；参数：bytes，正文；credentials，输出；返回：无。 */
static void decode(const uint8_t *bytes, wifi_credentials_t *credentials) {
    memset(credentials, 0, sizeof *credentials);
    memcpy(credentials->ssid, bytes + 16U, bytes[12]);
    memcpy(credentials->password, bytes + 48U, bytes[13]);
}

/** 功能：检查正文、版本、提交绑定和凭据；参数：bytes，完整槽；返回：记录有效标志。 */
static bool record_valid(const uint8_t *bytes) {
    wifi_credentials_t credentials;
    uint32_t crc = get_u32(bytes + CRC_OFFSET);
    const uint8_t *commit = bytes + BODY_SIZE;
    if (get_u32(bytes) != BODY_MAGIC || get_u32(bytes + 4U) != 1U ||
        bytes[12] == 0U || bytes[12] > 32U || bytes[13] > 63U ||
        crc32(bytes, CRC_OFFSET) != crc || get_u32(commit) != COMMIT_MAGIC ||
        get_u32(commit + 4U) != get_u32(bytes + 8U) || get_u32(commit + 8U) != crc ||
        get_u32(commit + 12U) != ~crc) {
        return false;
    }
    decode(bytes, &credentials);
    return wifi_credentials_valid(&credentials) &&
           bounded_length(credentials.ssid, sizeof credentials.ssid) == bytes[12] &&
           bounded_length(credentials.password, sizeof credentials.password) == bytes[13];
}

/** 功能：严格读取两槽并按模运算选取最新代数；参数：io，接口；scan，输出；返回：扫描状态。 */
static wifi_store_result_t scan_slots(const wifi_store_io_t *io, store_scan_t *scan) {
    bool empty = true;
    scan->selected = -1;
    for (unsigned slot = 0U; slot < 2U; ++slot) {
        scan->readable[slot] = io->read(io->context, STORE_BASE + slot * SLOT_SIZE,
                                       scan->bytes[slot], SLOT_SIZE);
        scan->valid[slot] = scan->readable[slot] && record_valid(scan->bytes[slot]);
        if (scan->readable[slot]) {
            for (size_t i = 0U; i < SLOT_SIZE; ++i) {
                if (scan->bytes[slot][i] != 0xffU) {
                    empty = false;
                }
            }
        }
        if (scan->valid[slot]) {
            scan->selected = (int)slot;
        }
    }
    if (scan->valid[0] && scan->valid[1]) {
        uint32_t difference = get_u32(scan->bytes[1] + 8U) - get_u32(scan->bytes[0] + 8U);
        if (difference == 0x80000000UL ||
            (difference == 0U && memcmp(scan->bytes[0], scan->bytes[1], BODY_SIZE) != 0)) {
            scan->selected = -1;
            return WIFI_STORE_CORRUPT;
        }
        scan->selected = difference != 0U && difference < 0x80000000UL ? 1 : 0;
    }
    if (!scan->readable[0] || !scan->readable[1]) {
        return WIFI_STORE_IO_ERROR;
    }
    return scan->selected >= 0 ? WIFI_STORE_OK : (empty ? WIFI_STORE_EMPTY : WIFI_STORE_CORRUPT);
}

/** 功能：加载最新有效凭据，允许损坏或不可读槽回退；参数：io，接口；credentials，输出；返回：结果。 */
wifi_store_result_t wifi_store_load(const wifi_store_io_t *io, wifi_credentials_t *credentials) {
    store_scan_t scan;
    wifi_store_result_t result;
    if (io == NULL || io->read == NULL || credentials == NULL) {
        return WIFI_STORE_INVALID;
    }
    result = scan_slots(io, &scan);
    if (scan.selected >= 0) {
        decode(scan.bytes[scan.selected], credentials);
        return WIFI_STORE_OK;
    }
    return result;
}

/** 功能：严格扫描后以独立提交页持久化新凭据并复查；参数：io，接口；credentials，输入；返回：结果。 */
wifi_store_result_t wifi_store_save(const wifi_store_io_t *io, const wifi_credentials_t *credentials) {
    store_scan_t scan;
    uint8_t body[BODY_SIZE] = {0};
    uint8_t commit[BODY_SIZE] = {0};
    uint8_t verify[BODY_SIZE];
    wifi_credentials_t previous;
    wifi_store_result_t result;
    uint32_t generation = 1U;
    unsigned target = 0U;
    uint32_t address;
    if (io == NULL || io->read == NULL || io->write == NULL || !wifi_credentials_valid(credentials)) {
        return WIFI_STORE_INVALID;
    }
    result = scan_slots(io, &scan);
    if (result == WIFI_STORE_IO_ERROR) {
        return result;
    }
    if (scan.valid[0] && scan.valid[1] && scan.selected < 0) {
        return WIFI_STORE_CORRUPT;
    }
    if (scan.selected >= 0) {
        decode(scan.bytes[scan.selected], &previous);
        if (strcmp(previous.ssid, credentials->ssid) == 0 &&
            strcmp(previous.password, credentials->password) == 0) {
            return WIFI_STORE_OK;
        }
        generation = get_u32(scan.bytes[scan.selected] + 8U) + 1U;
        target = (unsigned)scan.selected ^ 1U;
    }
    address = STORE_BASE + target * SLOT_SIZE;
    put_u32(body, BODY_MAGIC);
    put_u32(body + 4U, 1U);
    put_u32(body + 8U, generation);
    body[12] = (uint8_t)bounded_length(credentials->ssid, sizeof credentials->ssid);
    body[13] = (uint8_t)bounded_length(credentials->password, sizeof credentials->password);
    memcpy(body + 16U, credentials->ssid, body[12]);
    memcpy(body + 48U, credentials->password, body[13]);
    put_u32(body + CRC_OFFSET, crc32(body, CRC_OFFSET));
    if (!io->write(io->context, address + BODY_SIZE, commit, sizeof commit) ||
        !io->read(io->context, address + BODY_SIZE, verify, sizeof verify) ||
        memcmp(verify, commit, sizeof commit) != 0 ||
        !io->write(io->context, address, body, sizeof body) ||
        !io->read(io->context, address, verify, sizeof verify) ||
        memcmp(verify, body, sizeof body) != 0) {
        return WIFI_STORE_IO_ERROR;
    }
    put_u32(commit, COMMIT_MAGIC);
    put_u32(commit + 4U, generation);
    put_u32(commit + 8U, get_u32(body + CRC_OFFSET));
    put_u32(commit + 12U, ~get_u32(body + CRC_OFFSET));
    /* 最后写入应答可能丢失；只有严格重扫证实新槽完整且被选中才报告成功。 */
    (void)io->write(io->context, address + BODY_SIZE, commit, sizeof commit);
    result = scan_slots(io, &scan);
    if (result != WIFI_STORE_OK || scan.selected != (int)target ||
        memcmp(scan.bytes[target], body, sizeof body) != 0 ||
        memcmp(scan.bytes[target] + BODY_SIZE, commit, sizeof commit) != 0) {
        return WIFI_STORE_IO_ERROR;
    }
    return WIFI_STORE_OK;
}
