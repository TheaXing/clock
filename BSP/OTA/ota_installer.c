#include "ota_installer.h"
#include "ota_crypto.h"
#include "ota_image.h"
#include <string.h>

#define INSTALL_BLOCK_SIZE 4096u
#define FLASH_LIMIT 0x08080000u

static ota_install_diagnostic_t diagnostic;

/** 功能：读取最近一次安装的故障快照。参数：无。返回：诊断值副本。 */
ota_install_diagnostic_t ota_install_diagnostic(void) {
    return diagnostic;
}

/** 功能：记录即将执行的操作位置。参数：阶段、镜像偏移和 Flash 地址。返回：无。 */
static void set_diagnostic(ota_install_stage_t stage, uint32_t offset, uint32_t address) {
    diagnostic.stage = stage;
    diagnostic.image_offset = offset;
    diagnostic.address = address;
    diagnostic.store_result = OTA_STORE_OK;
}

/** 功能：检查编程请求位于单一应用区域且四字节对齐。参数：地址、长度和是否允许安装描述。返回：范围安全时为真。 */
static bool program_range_valid(uint32_t address, size_t length, bool footer) {
    uint32_t lower = footer ? OTA_FOOTER_BASE : OTA_APP_BASE;
    uint32_t upper = footer ? FLASH_LIMIT : OTA_FOOTER_BASE;
    return length != 0 && (address & 3u) == 0 && (length & 3u) == 0 &&
        address >= lower && address < upper && length <= upper - address;
}

/** 功能：编程并逐块回读检查所有字节。参数：访问接口、地址、输入、长度和区域选择。返回：编程及回读均成功时为真。 */
static bool program_verified(const ota_install_io_t *io, uint32_t address, const uint8_t *data, size_t length, bool footer) {
    uint8_t verify[256];
    size_t offset = 0;
    ota_install_stage_t program_stage = OTA_INSTALL_STAGE_PROGRAM;
    ota_install_stage_t readback_stage = OTA_INSTALL_STAGE_PROGRAM_READBACK;
    uint32_t image_offset = footer ? 0 : address - OTA_APP_BASE;
    if (footer) {
        bool commit = address == OTA_FOOTER_BASE + 252;
        program_stage = commit ? OTA_INSTALL_STAGE_FOOTER_COMMIT : OTA_INSTALL_STAGE_FOOTER_PROGRAM;
        readback_stage = commit ? OTA_INSTALL_STAGE_FOOTER_COMMIT_READBACK : OTA_INSTALL_STAGE_FOOTER_PROGRAM_READBACK;
    }
    set_diagnostic(program_stage, image_offset, address);
    if (!program_range_valid(address, length, footer) || !io->program(io->context, address, data, length)) {
        return false;
    }
    while (offset < length) {
        size_t count = length - offset;
        if (count > sizeof(verify)) {
            count = sizeof(verify);
        }
        set_diagnostic(readback_stage, footer ? 0 : image_offset + (uint32_t)offset, address + (uint32_t)offset);
        if (!io->read_flash(io->context, address + (uint32_t)offset, verify, count) || memcmp(data + offset, verify, count) != 0) {
            return false;
        }
        offset += count;
    }
    return true;
}

/** 功能：重新从内部 Flash 计算完整镜像摘要并检查向量。参数：接口、已认证描述及临时块。返回：完整验证成功时为真。 */
static bool verify_flash_image(const ota_install_io_t *io, const ota_manifest_t *manifest, uint8_t block[INSTALL_BLOCK_SIZE]) {
    ota_sha256_t hash;
    uint8_t digest[32];
    uint32_t offset = 0;
    ota_sha256_init(&hash);
    while (offset < manifest->image_size) {
        uint32_t count = manifest->image_size - offset;
        if (count > INSTALL_BLOCK_SIZE) {
            count = INSTALL_BLOCK_SIZE;
        }
        set_diagnostic(OTA_INSTALL_STAGE_IMAGE_READ, offset, OTA_APP_BASE + offset);
        if (!io->read_flash(io->context, OTA_APP_BASE + offset, block, count)) {
            return false;
        }
        if (offset == 0) {
            set_diagnostic(OTA_INSTALL_STAGE_IMAGE_VECTOR, 0, OTA_APP_BASE);
            if (!ota_image_vectors_valid(block, manifest->image_size)) {
                return false;
            }
        }
        ota_sha256_update(&hash, block, count);
        offset += count;
    }
    ota_sha256_final(&hash, digest);
    set_diagnostic(OTA_INSTALL_STAGE_IMAGE_HASH, offset, OTA_APP_BASE + offset);
    return ota_constant_equal(digest, manifest->digest, 32);
}

/** 功能：先擦除并核实旧描述失效，再擦除其余应用扇区。参数：接口和至少二百五十六字节的临时块。返回：四个扇区均成功时为真。 */
static bool erase_application(const ota_install_io_t *io, uint8_t *block) {
    static const uint32_t addresses[] = {OTA_APP_BASE, 0x08020000u, 0x08040000u};
    unsigned sector;
    unsigned index;
    set_diagnostic(OTA_INSTALL_STAGE_ERASE_FOOTER, 0, 0x08060000u);
    if (!io->erase_sector(io->context, 7)) {
        return false;
    }
    set_diagnostic(OTA_INSTALL_STAGE_ERASE_FOOTER_READBACK, 0, OTA_FOOTER_BASE);
    if (!io->read_flash(io->context, OTA_FOOTER_BASE, block, OTA_HEADER_SIZE)) {
        return false;
    }
    for (index = 0; index < OTA_HEADER_SIZE; ++index) {
        if (block[index] != 0xFF) {
            return false;
        }
    }
    for (sector = 4; sector <= 6; ++sector) {
        set_diagnostic(OTA_INSTALL_STAGE_ERASE_APPLICATION, addresses[sector - 4] - OTA_APP_BASE, addresses[sector - 4]);
        if (!io->erase_sector(io->context, sector)) {
            return false;
        }
    }
    return true;
}

/** 功能：执行认证、日志许可、完整安装及试启动提交。参数：注入接口、线上包头、发布密钥和状态输出。返回：安装描述与试启动日志均持久化时为真。 */
bool ota_install(const ota_install_io_t *io, const uint8_t header[256], const uint8_t key[32], ota_record_t *record) {
    ota_manifest_t manifest;
    ota_manifest_t installed;
    ota_record_t next;
    uint8_t block[INSTALL_BLOCK_SIZE];
    uint8_t footer[OTA_HEADER_SIZE];
    uint8_t commit[4];
    uint32_t offset = 0;
    set_diagnostic(OTA_INSTALL_STAGE_AUTHENTICATE, 0, 0);
    if (io == NULL || record == NULL || io->erase_sector == NULL || io->program == NULL || io->read_flash == NULL || io->fetch == NULL ||
        !ota_manifest_parse(header, key, false, &manifest)) {
        return false;
    }
    memset(&next, 0, sizeof(next));
    next.state = OTA_STATE_INSTALLING;
    memcpy(next.manifest, manifest.raw, OTA_HEADER_SIZE);
    set_diagnostic(OTA_INSTALL_STAGE_PERMIT_JOURNAL, 0, 0);
    diagnostic.store_result = ota_journal_save(&io->journal, &next);
    if (diagnostic.store_result != OTA_STORE_OK) {
        return false;
    }
    *record = next;
    if (!erase_application(io, block)) {
        return false;
    }
    while (offset < manifest.image_size) {
        uint32_t count = manifest.image_size - offset;
        uint32_t padded;
        if (count > INSTALL_BLOCK_SIZE) {
            count = INSTALL_BLOCK_SIZE;
        }
        padded = (count + 3u) & ~3u;
        set_diagnostic(OTA_INSTALL_STAGE_FETCH, offset, OTA_APP_BASE + offset);
        if (!io->fetch(io->context, OTA_HEADER_SIZE + offset, block, count)) {
            return false;
        }
        memset(block + count, 0xFF, padded - count);
        if (!program_verified(io, OTA_APP_BASE + offset, block, padded, false)) {
            return false;
        }
        offset += count;
    }
    if (!verify_flash_image(io, &manifest, block) || !program_verified(io, OTA_FOOTER_BASE, manifest.raw, 252, true)) {
        return false;
    }
    commit[0] = (uint8_t)(OTA_FOOTER_COMMIT & 0xFFu);
    commit[1] = (uint8_t)((OTA_FOOTER_COMMIT >> 8) & 0xFFu);
    commit[2] = (uint8_t)((OTA_FOOTER_COMMIT >> 16) & 0xFFu);
    commit[3] = (uint8_t)((OTA_FOOTER_COMMIT >> 24) & 0xFFu);
    if (!program_verified(io, OTA_FOOTER_BASE + 252, commit, sizeof(commit), true)) {
        return false;
    }
    set_diagnostic(OTA_INSTALL_STAGE_FOOTER_READ, 0, OTA_FOOTER_BASE);
    if (!io->read_flash(io->context, OTA_FOOTER_BASE, footer, sizeof(footer))) {
        return false;
    }
    set_diagnostic(OTA_INSTALL_STAGE_FOOTER_PARSE, 0, OTA_FOOTER_BASE);
    if (!ota_manifest_parse(footer, key, true, &installed) || !ota_constant_equal(installed.id, manifest.id, 16)) {
        return false;
    }
    if (!verify_flash_image(io, &installed, block)) {
        return false;
    }
    next.state = OTA_STATE_TRIAL;
    memcpy(next.manifest, installed.raw, OTA_HEADER_SIZE);
    set_diagnostic(OTA_INSTALL_STAGE_TRIAL_JOURNAL, 0, 0);
    diagnostic.store_result = ota_journal_save(&io->journal, &next);
    if (diagnostic.store_result != OTA_STORE_OK) {
        return false;
    }
    *record = next;
    set_diagnostic(OTA_INSTALL_STAGE_NONE, 0, 0);
    return true;
}
