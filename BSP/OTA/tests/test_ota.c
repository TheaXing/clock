#include "ota_crypto.h"
#include "ota_image.h"
#include "ota_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); exit(1); } } while (0)

/** 保存完整 EEPROM 和单向 Flash 写入的断电模型。 */
typedef struct {
    uint8_t eeprom[65536]; // 模拟设备全部存储，保留闹钟区域
    uint8_t flash[524288]; // 模拟真实五百一十二千字节内部存储
    uint8_t package[256 + OTA_IMAGE_MAX]; // 模拟服务器冻结包
    size_t package_size; // 包快照长度
    unsigned events; // 已发生的有副作用操作数
    unsigned fail_event; // 指定断电操作，零表示禁用
    unsigned failure_mode; // 零为写前断电，一为半写，二为应答丢失
    unsigned erases; // 发生的应用擦除数
    unsigned sectors[4]; // 成功安装时的扇区擦除顺序
    unsigned reads; // EEPROM 已完成读取调用数
    unsigned fail_read; // 指定失败的读取调用，零表示禁用
    bool dead; // 断电后所有访问都失败
    bool read_failure; // 强制 EEPROM 读取失败
    bool corrupt_flash_read; // 最后完整摘要读取时破坏返回内容
    bool bad_erase; // 模拟擦除回调虚假成功
    bool fetch_failure; // 模拟服务器正文中断
    uint32_t fail_fetch_offset; // 指定失败的完整包偏移，零表示禁用
    unsigned flash_reads; // Flash 已发生读取调用数
    unsigned fail_flash_read; // 指定失败的 Flash 读取调用，零表示禁用
    unsigned corrupt_flash_read_event; // 指定返回首字节损坏的 Flash 读取调用，零表示禁用
} model_t;

static model_t model;
static model_t baseline;
static unsigned checks;

/** 功能：写入小端整数。参数：目标地址和值。返回：无。 */
static void put32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

/** 功能：解码固定十六进制样本。参数：样本、输出和长度。返回：无。 */
static void unhex(const char *hex, uint8_t *output, size_t length) {
    size_t index;
    for (index = 0; index < length; ++index) {
        unsigned high = (unsigned)(hex[index * 2] <= '9' ? hex[index * 2] - '0' : hex[index * 2] - 'a' + 10);
        unsigned low = (unsigned)(hex[index * 2 + 1] <= '9' ? hex[index * 2 + 1] - '0' : hex[index * 2 + 1] - 'a' + 10);
        CHECK(high < 16 && low < 16);
        output[index] = (uint8_t)(high * 16 + low);
    }
}

/** 功能：产生公开测试密钥。参数：输出。返回：无。 */
static void test_key(uint8_t key[32]) {
    unsigned index;
    for (index = 0; index < 32; ++index) {
        key[index] = (uint8_t)index;
    }
}

/** 功能：装载由 Node.js 标准加密库独立生成的包头。参数：输出包头和镜像。返回：无。 */
static void fixture(uint8_t header[256], uint8_t image[513]) {
    static const char hex[] =
        "434c4b4f54413100010000000001000000000108010200000700000000000000"
        "436c6f636b5f437562654d582d53544d33324634303756450000000000000000"
        "3ee1e35e230827235d0bff25bed50900408293cd8ff6b428b879c5aff0d8e10e"
        "6d175afdb0a1361bf8628cc71f0f4ed6"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000"
        "c87565df299d4a34566ab8c81651425ddd00ddcb6ffb5ebd37d10883bf3aea88"
        "00000000000000000000000000000000000000000000000000000000ffffffff";
    size_t index;
    CHECK(strlen(hex) == 512);
    unhex(hex, header, 256);
    for (index = 0; index < 513; ++index) {
        image[index] = (uint8_t)(index % 251);
    }
    put32(image, 0x20020000u);
    put32(image + 4, 0x08010189u);
}

/** 功能：验证标准 SHA/HMAC 向量及流式分块。参数：无。返回：无。 */
static void test_crypto(void) {
    uint8_t result[32];
    uint8_t expected[32];
    uint8_t key[32] = {0};
    uint8_t data[1000];
    ota_sha256_t context;
    unsigned index;
    unhex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", expected, 32);
    ota_sha256(NULL, 0, result);
    CHECK(ota_constant_equal(result, expected, 32));
    unhex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", expected, 32);
    ota_sha256("abc", 3, result);
    CHECK(ota_constant_equal(result, expected, 32));
    unhex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", expected, 32);
    ota_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, result);
    CHECK(ota_constant_equal(result, expected, 32));
    memset(data, 'a', sizeof(data));
    ota_sha256_init(&context);
    for (index = 0; index < 1000; ++index) {
        ota_sha256_update(&context, data, sizeof(data));
    }
    ota_sha256_final(&context, result);
    unhex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", expected, 32);
    CHECK(ota_constant_equal(result, expected, 32));
    memset(key, 0x0b, 20);
    ota_hmac_sha256(key, "Hi There", 8, result);
    unhex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", expected, 32);
    CHECK(ota_constant_equal(result, expected, 32));
    expected[31] ^= 1;
    CHECK(!ota_constant_equal(result, expected, 32));
    ++checks;
}

/** 功能：重新认证测试变异包头。参数：包头。返回：无。 */
static void sign_header(uint8_t header[256]) {
    uint8_t key[32];
    uint8_t digest[32];
    test_key(key);
    ota_sha256(header, 96, digest);
    memcpy(header + 96, digest, 16);
    ota_hmac_sha256(key, header, 192, header + 192);
}

/** 功能：验证包头严格格式和完整镜像精确边界。参数：无。返回：无。 */
static void test_image(void) {
    uint8_t header[256];
    uint8_t changed[256];
    uint8_t image[513];
    uint8_t key[32];
    ota_manifest_t manifest;
    unsigned index;
    fixture(header, image);
    test_key(key);
    CHECK(ota_manifest_parse(header, key, false, &manifest));
    CHECK(manifest.image_size == 513 && manifest.version == 7);
    CHECK(ota_image_verify(&manifest, image));
    CHECK(!ota_manifest_parse(header, key, true, &manifest));
    memcpy(changed, header, 256);
    put32(changed + 252, OTA_FOOTER_COMMIT);
    CHECK(ota_manifest_parse(changed, key, true, &manifest));
    CHECK(!ota_manifest_parse(changed, key, false, &manifest));
    for (index = 0; index < 256; ++index) {
        memcpy(changed, header, 256);
        changed[index] ^= 1;
        CHECK(!ota_manifest_parse(changed, key, false, &manifest));
    }
    memcpy(changed, header, 256);
    put32(changed + 24, 0);
    sign_header(changed);
    CHECK(!ota_manifest_parse(changed, key, false, &manifest));
    memcpy(changed, header, 256);
    put32(changed + 16, 0x08000000u);
    sign_header(changed);
    CHECK(!ota_manifest_parse(changed, key, false, &manifest));
    put32(changed + 16, OTA_APP_BASE);
    put32(changed + 20, 0xFFFFFFFFu);
    sign_header(changed);
    CHECK(!ota_manifest_parse(changed, key, false, &manifest));
    put32(changed + 20, 0x187u);
    sign_header(changed);
    CHECK(!ota_manifest_parse(changed, key, false, &manifest));
    put32(changed + 20, OTA_IMAGE_MAX);
    sign_header(changed);
    CHECK(ota_manifest_parse(changed, key, false, &manifest));
    CHECK(ota_manifest_parse(header, key, false, &manifest));
    image[512] ^= 1;
    CHECK(!ota_image_verify(&manifest, image));
    image[512] ^= 1;
    CHECK(ota_image_vectors_valid(image, 513));
    put32(image, 0x20000000u);
    CHECK(!ota_image_vectors_valid(image, 513));
    put32(image, 0x20020008u);
    CHECK(!ota_image_vectors_valid(image, 513));
    put32(image, 0x2001FFFCu);
    CHECK(!ota_image_vectors_valid(image, 513));
    put32(image, 0x20020000u);
    put32(image + 4, OTA_APP_BASE + 0x187u);
    CHECK(!ota_image_vectors_valid(image, 513));
    put32(image + 4, OTA_APP_BASE + 0x189u);
    CHECK(ota_image_vectors_valid(image, 513));
    put32(image + 4, OTA_APP_BASE + 513u);
    CHECK(ota_image_vectors_valid(image, 513));
    put32(image + 4, OTA_APP_BASE + 515u);
    CHECK(!ota_image_vectors_valid(image, 513));
    put32(image + 4, OTA_APP_BASE + 0x188u);
    CHECK(!ota_image_vectors_valid(image, 513));
    ++checks;
}

/** 功能：验证运行中应用的元数据读取不能代替发布者认证。参数：无。返回：无。 */
static void test_trusted_metadata(void) {
    static const size_t malformed_bytes[] = {0, 8, 12, 16, 28, 32, 63, 96, 112, 191, 224, 251, 252};
    uint8_t header[256];
    uint8_t changed[256];
    uint8_t image[513];
    uint8_t key[32];
    ota_manifest_t output;
    ota_manifest_t original;
    size_t index;
    fixture(header, image);
    test_key(key);
    CHECK(ota_manifest_read_trusted(header, false, &output));
    put32(header + 252, OTA_FOOTER_COMMIT);
    CHECK(ota_manifest_read_trusted(header, true, &output));
    CHECK(output.image_size == 513 && output.version == 7);
    CHECK(memcmp(output.raw, header, 256) == 0);
    CHECK(!ota_manifest_read_trusted(header, false, &output));
    memcpy(changed, header, 256);
    changed[192] ^= 1;
    CHECK(ota_manifest_read_trusted(changed, true, &output));
    CHECK(output.image_size == 513 && output.version == 7);
    original = output;
    CHECK(!ota_manifest_parse(changed, key, true, &output));
    CHECK(memcmp(&output, &original, sizeof(output)) == 0);
    for (index = 0; index < sizeof(malformed_bytes) / sizeof(malformed_bytes[0]); ++index) {
        memcpy(changed, header, 256);
        changed[malformed_bytes[index]] ^= 1;
        CHECK(!ota_manifest_read_trusted(changed, true, &output));
        CHECK(memcmp(&output, &original, sizeof(output)) == 0);
        CHECK(!ota_manifest_parse(changed, key, true, &output));
        CHECK(memcmp(&output, &original, sizeof(output)) == 0);
    }
    memcpy(changed, header, 256);
    put32(changed + 20, 0x187u);
    sign_header(changed);
    CHECK(!ota_manifest_read_trusted(changed, true, &output));
    CHECK(!ota_manifest_parse(changed, key, true, &output));
    put32(changed + 20, OTA_IMAGE_MAX + 1);
    sign_header(changed);
    CHECK(!ota_manifest_read_trusted(changed, true, &output));
    CHECK(!ota_manifest_parse(changed, key, true, &output));
    memcpy(changed, header, 256);
    put32(changed + 24, 0);
    sign_header(changed);
    CHECK(!ota_manifest_read_trusted(changed, true, &output));
    CHECK(!ota_manifest_parse(changed, key, true, &output));
    CHECK(!ota_manifest_read_trusted(NULL, true, &output));
    CHECK(!ota_manifest_read_trusted(header, true, NULL));
    ++checks;
}

/** 功能：读取模拟 EEPROM。参数：模型、地址、输出和字节数。返回：访问成功标志。 */
static bool memory_read(void *context, uint32_t address, void *data, size_t length) {
    model_t *memory = context;
    ++memory->reads;
    if (memory->dead || memory->read_failure || memory->fail_read == memory->reads || address > sizeof(memory->eeprom) || length > sizeof(memory->eeprom) - address) {
        return false;
    }
    memcpy(data, memory->eeprom + address, length);
    return true;
}

/** 功能：确定一次副作用是否遭遇断电。参数：模型。返回：当前操作是否为故障点。 */
static bool event_fails(model_t *memory) {
    ++memory->events;
    return memory->fail_event == memory->events;
}

/** 功能：模拟 EEPROM 单页写和应答丢失。参数：模型、地址、输入和字节数。返回：持久化应答。 */
static bool memory_write(void *context, uint32_t address, const void *data, size_t length) {
    model_t *memory = context;
    bool fail;
    CHECK(address >= 0x0800u && address + length <= 0x1000u);
    CHECK(length > 0 && length <= 128 && address / 128 == (address + (uint32_t)length - 1) / 128);
    if (memory->dead) {
        return false;
    }
    fail = event_fails(memory);
    if (!fail || memory->failure_mode == 2) {
        memcpy(memory->eeprom + address, data, length);
    } else if (memory->failure_mode == 1) {
        memcpy(memory->eeprom + address, data, length / 2);
    }
    if (fail) {
        memory->dead = true;
    }
    return !fail;
}

/** 功能：构造日志访问接口。参数：模型。返回：回调集合。 */
static ota_journal_io_t journal_io(model_t *memory) {
    ota_journal_io_t io;
    io.context = memory;
    io.read = memory_read;
    io.write = memory_write;
    return io;
}

/** 功能：清空设备并保留可检查的闹钟哨兵。参数：无。返回：无。 */
static void reset_model(void) {
    memset(&model, 0, sizeof(model));
    memset(model.eeprom, 0xFF, sizeof(model.eeprom));
    memset(model.flash, 0xFF, sizeof(model.flash));
    memset(model.eeprom, 0xA5, 0x800);
}

/** 功能：独立计算日志样本所需的标准 CRC。参数：输入和长度。返回：CRC。 */
static uint32_t fixture_crc(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    size_t index;
    unsigned bit;
    for (index = 0; index < length; ++index) {
        if (index >= 16 && index < 20) {
            continue;
        }
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

/** 功能：手工制作给定代数的独立日志槽。参数：槽地址、代数和错误编号。返回：无。 */
static void journal_fixture(uint32_t address, uint32_t generation, uint32_t error) {
    uint8_t *body = model.eeprom + address;
    uint8_t *commit = body + 0x380;
    uint32_t crc;
    memset(body, 0, 384);
    memcpy(body, "OTAJ", 4);
    body[4] = 1;
    body[6] = 128;
    put32(body + 8, 384);
    put32(body + 12, generation);
    put32(body + 20, OTA_STATE_REQUESTED);
    put32(body + 28, error);
    crc = fixture_crc(body, 384);
    put32(body + 16, crc);
    memset(commit, 0, 128);
    memcpy(commit, "OTAC", 4);
    put32(commit + 4, generation);
    put32(commit + 8, crc);
    put32(commit + 12, ~generation);
}

/** 功能：验证严格空白识别、代数冲突与所有页写断电。参数：无。返回：无。 */
static void test_journal(void) {
    ota_record_t record = {0};
    ota_record_t loaded;
    ota_record_t unchanged;
    ota_journal_io_t io = journal_io(&model);
    unsigned event;
    unsigned mode;
    unsigned read_count;
    reset_model();
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_EMPTY);
    model.eeprom[0xB00] = 0;
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_CORRUPT);
    CHECK(ota_journal_save(&io, &record) == OTA_STORE_CORRUPT && model.events == 0);
    model.read_failure = true;
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_IO);
    reset_model();
    record.state = OTA_STATE_REQUESTED;
    CHECK(ota_journal_save(&io, &record) == OTA_STORE_OK);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK);
    CHECK(loaded.state == OTA_STATE_REQUESTED);
    baseline = model;
    model.reads = 0;
    loaded = record;
    loaded.error = 99;
    CHECK(ota_journal_save(&io, &loaded) == OTA_STORE_OK);
    read_count = model.reads;
    for (event = 1; event <= read_count; ++event) {
        model = baseline;
        model.reads = 0;
        model.fail_read = event;
        unchanged = record;
        unchanged.error = 99;
        loaded = unchanged;
        CHECK(ota_journal_save(&io, &loaded) == OTA_STORE_IO);
        CHECK(memcmp(&loaded, &unchanged, sizeof(loaded)) == 0);
        model.fail_read = 0;
        CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK);
        CHECK(loaded.error == 0 || loaded.error == 99);
    }
    for (mode = 0; mode < 3; ++mode) {
        for (event = 1; event <= 5; ++event) {
            model = baseline;
            model.events = 0;
            model.fail_event = event;
            model.failure_mode = mode;
            unchanged = record;
            unchanged.error = 99;
            loaded = unchanged;
            CHECK(ota_journal_save(&io, &loaded) == OTA_STORE_IO);
            CHECK(memcmp(&loaded, &unchanged, sizeof(loaded)) == 0);
            model.dead = false;
            CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK);
            CHECK(loaded.error == 0 || (mode != 0 && event == 5 && loaded.error == 99));
            CHECK(model.eeprom[0] == 0xA5 && model.eeprom[0x7FF] == 0xA5);
        }
    }
    reset_model();
    journal_fixture(0x800, 0xFFFFFFFFu, 1);
    journal_fixture(0xC00, 0, 2);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK && loaded.error == 2);
    journal_fixture(0x800, 0, 1);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_CORRUPT);
    journal_fixture(0x800, 0, 2);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK && loaded.error == 2);
    journal_fixture(0x800, 0x80000000u, 1);
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_CORRUPT);
    journal_fixture(0x800, 1, 1);
    model.eeprom[0xC00 + 0x380] ^= 1;
    CHECK(ota_journal_load(&io, &loaded) == OTA_STORE_OK && loaded.error == 1);
    printf("journal read failures: %u; write boundaries: 5 x 3\n", read_count);
    ++checks;
}

/** 功能：模拟受限应用扇区擦除。参数：模型和扇区号。返回：擦除应答。 */
static bool erase_sector(void *context, unsigned sector) {
    model_t *memory = context;
    static const uint32_t offsets[] = {0x10000, 0x20000, 0x40000, 0x60000, 0x80000};
    ota_journal_io_t io = journal_io(memory);
    ota_record_t record;
    bool fail;
    CHECK(sector >= 4 && sector <= 7);
    CHECK(ota_journal_load(&io, &record) == OTA_STORE_OK && record.state == OTA_STATE_INSTALLING);
    if (memory->erases == 0) {
        CHECK(sector == 7);
    } else {
        size_t index;
        for (index = 0x7FF00; index < 0x80000; ++index) {
            CHECK(memory->flash[index] == 0xFF);
        }
    }
    memory->sectors[memory->erases++] = sector;
    fail = event_fails(memory);
    if (!memory->bad_erase && (!fail || memory->failure_mode != 0)) {
        memset(memory->flash + offsets[sector - 4], 0xFF, offsets[sector - 3] - offsets[sector - 4]);
    }
    if (fail) {
        memory->dead = true;
    }
    return !fail;
}

/** 功能：模拟 Flash 单向四字节编程。参数：模型、地址、输入及长度。返回：编程应答。 */
static bool program_flash(void *context, uint32_t address, const void *data, size_t length) {
    model_t *memory = context;
    const uint8_t *input = data;
    size_t count = length;
    size_t index;
    bool fail;
    CHECK(address >= OTA_APP_BASE && address <= 0x08080000u && length <= 0x08080000u - address);
    CHECK((address & 3u) == 0 && (length & 3u) == 0);
    CHECK(address >= OTA_FOOTER_BASE || length <= OTA_FOOTER_BASE - address);
    CHECK(memory->erases == 4);
    if (address >= OTA_FOOTER_BASE) {
        CHECK(memcmp(memory->flash + 0x10000, memory->package + 256, memory->package_size - 256) == 0);
    }
    if (address == OTA_FOOTER_BASE + 252) {
        CHECK(memcmp(memory->flash + 0x7FF00, memory->package, 252) == 0);
    }
    if (memory->dead) {
        return false;
    }
    fail = event_fails(memory);
    if (fail) {
        count = memory->failure_mode == 0 ? 0 : (memory->failure_mode == 1 ? length / 2 : length);
        memory->dead = true;
    }
    for (index = 0; index < count; ++index) {
        CHECK((memory->flash[address - 0x08000000u + index] & input[index]) == input[index]);
        memory->flash[address - 0x08000000u + index] &= input[index];
    }
    return !fail;
}

/** 功能：读取模拟内部 Flash。参数：模型、地址、输出及长度。返回：读取结果。 */
static bool read_flash(void *context, uint32_t address, void *data, size_t length) {
    model_t *memory = context;
    ++memory->flash_reads;
    if (memory->dead || memory->fail_flash_read == memory->flash_reads || address < 0x08000000u || address > 0x08080000u || length > 0x08080000u - address) {
        return false;
    }
    memcpy(data, memory->flash + address - 0x08000000u, length);
    if (memory->corrupt_flash_read && address == OTA_APP_BASE && length > 256) {
        ((uint8_t *)data)[length - 1] ^= 1;
    }
    if (memory->corrupt_flash_read_event == memory->flash_reads) {
        ((uint8_t *)data)[0] ^= 1;
    }
    return true;
}

/** 功能：模拟冻结包的有界 Range 下载。参数：模型、包偏移、输出及长度。返回：完整读取结果。 */
static bool fetch_package(void *context, uint32_t offset, void *data, size_t length) {
    model_t *memory = context;
    CHECK(length > 0 && length <= 4096 && offset >= 256);
    if (memory->dead || memory->fetch_failure || memory->fail_fetch_offset == offset || offset > memory->package_size || length > memory->package_size - offset) {
        return false;
    }
    memcpy(data, memory->package + offset, length);
    return true;
}

/** 功能：构造安装回调集合。参数：无。返回：回调集合。 */
static ota_install_io_t install_io(void) {
    ota_install_io_t io;
    io.context = &model;
    io.journal = journal_io(&model);
    io.erase_sector = erase_sector;
    io.program = program_flash;
    io.read_flash = read_flash;
    io.fetch = fetch_package;
    return io;
}

/** 功能：装入旧的可信镜像及较大的新包。参数：无。返回：无。 */
static void installation_fixture(void) {
    uint8_t header[256];
    uint8_t image[513];
    size_t index;
    reset_model();
    fixture(header, image);
    memcpy(model.flash + 0x10000, image, sizeof(image));
    put32(header + 252, OTA_FOOTER_COMMIT);
    memcpy(model.flash + 0x7FF00, header, sizeof(header));
    put32(header + 252, 0xFFFFFFFFu);
    put32(header + 20, 8193);
    put32(header + 24, 8);
    for (index = 0; index < 8193; ++index) {
        model.package[256 + index] = (uint8_t)(index % 239);
    }
    put32(model.package + 256, 0x20020000u);
    put32(model.package + 260, OTA_APP_BASE + 0x189u);
    ota_sha256(model.package + 256, 8193, header + 64);
    sign_header(header);
    memcpy(model.package, header, 256);
    model.package_size = 256 + 8193;
}

/** 功能：判断模拟 Flash 当前是否可独立认证启动。参数：无。返回：镜像可信标志。 */
static bool trusted_flash(void) {
    uint8_t key[32];
    ota_manifest_t manifest;
    test_key(key);
    return ota_manifest_parse(model.flash + 0x7FF00, key, true, &manifest) && ota_image_verify(&manifest, model.flash + 0x10000);
}

/** 功能：核对安装失败对外提供的阶段及定位信息。参数：预期阶段、镜像偏移、Flash 地址和日志结果。返回：无。 */
static void check_install_diagnostic(ota_install_stage_t stage, uint32_t offset, uint32_t address, ota_store_result_t store_result) {
    ota_install_diagnostic_t diagnostic = ota_install_diagnostic();
    CHECK(diagnostic.stage == stage);
    CHECK(diagnostic.image_offset == offset);
    CHECK(diagnostic.address == address);
    CHECK(diagnostic.store_result == store_result);
}

/** 功能：验证注入故障能定位到真实失败阶段，且下次安装清除上次故障。参数：无。返回：无。 */
static void test_installer_diagnostics(void) {
    /** 保存指定 Flash 读取故障及其独立预期位置。 */
    static const struct {
        unsigned read; // 从一开始的 Flash 读取调用编号
        bool corrupt; // 为真时返回损坏字节，否则直接读取失败
        ota_install_stage_t stage; // 期望对外报告的失败阶段
        uint32_t offset; // 期望的镜像正文偏移
        uint32_t address; // 期望的 Flash 绝对地址
    } cases[] = {
        {1, false, OTA_INSTALL_STAGE_ERASE_FOOTER_READBACK, 0, 0x0807FF00u},
        {1, true, OTA_INSTALL_STAGE_ERASE_FOOTER_READBACK, 0, 0x0807FF00u},
        {2, false, OTA_INSTALL_STAGE_PROGRAM_READBACK, 0, 0x08010000u},
        {2, true, OTA_INSTALL_STAGE_PROGRAM_READBACK, 0, 0x08010000u},
        {19, false, OTA_INSTALL_STAGE_PROGRAM_READBACK, 4352, 0x08011100u},
        {35, false, OTA_INSTALL_STAGE_IMAGE_READ, 0, 0x08010000u},
        {35, true, OTA_INSTALL_STAGE_IMAGE_VECTOR, 0, 0x08010000u},
        {36, false, OTA_INSTALL_STAGE_IMAGE_READ, 4096, 0x08011000u},
        {38, false, OTA_INSTALL_STAGE_FOOTER_PROGRAM_READBACK, 0, 0x0807FF00u},
        {39, false, OTA_INSTALL_STAGE_FOOTER_COMMIT_READBACK, 0, 0x0807FFFCu},
        {40, false, OTA_INSTALL_STAGE_FOOTER_READ, 0, 0x0807FF00u},
        {40, true, OTA_INSTALL_STAGE_FOOTER_PARSE, 0, 0x0807FF00u},
        {41, false, OTA_INSTALL_STAGE_IMAGE_READ, 0, 0x08010000u},
        {42, true, OTA_INSTALL_STAGE_IMAGE_HASH, 8193, 0x08012001u}
    };
    ota_install_io_t io = install_io();
    ota_record_t record = {0};
    uint8_t key[32];
    size_t index;
    test_key(key);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        installation_fixture();
        if (cases[index].corrupt) {
            model.corrupt_flash_read_event = cases[index].read;
        } else {
            model.fail_flash_read = cases[index].read;
        }
        CHECK(!ota_install(&io, model.package, key, &record));
        check_install_diagnostic(cases[index].stage, cases[index].offset, cases[index].address, OTA_STORE_OK);
    }
    installation_fixture();
    model.fail_fetch_offset = 4352;
    CHECK(!ota_install(&io, model.package, key, &record));
    check_install_diagnostic(OTA_INSTALL_STAGE_FETCH, 4096, 0x08011000u, OTA_STORE_OK);
    installation_fixture();
    model.package[192] ^= 1;
    CHECK(!ota_install(&io, model.package, key, &record));
    check_install_diagnostic(OTA_INSTALL_STAGE_AUTHENTICATE, 0, 0, OTA_STORE_OK);
    CHECK(model.erases == 0 && model.events == 0 && trusted_flash());
    installation_fixture();
    model.read_failure = true;
    CHECK(!ota_install(&io, model.package, key, &record));
    check_install_diagnostic(OTA_INSTALL_STAGE_PERMIT_JOURNAL, 0, 0, OTA_STORE_IO);
    CHECK(model.erases == 0 && trusted_flash());
    installation_fixture();
    model.package[256 + 4000] ^= 1;
    CHECK(!ota_install(&io, model.package, key, &record));
    check_install_diagnostic(OTA_INSTALL_STAGE_IMAGE_HASH, 8193, 0x08012001u, OTA_STORE_OK);
    installation_fixture();
    put32(model.package + 260, OTA_APP_BASE + 0x188u);
    ota_sha256(model.package + 256, 8193, model.package + 64);
    sign_header(model.package);
    CHECK(!ota_install(&io, model.package, key, &record));
    check_install_diagnostic(OTA_INSTALL_STAGE_IMAGE_VECTOR, 0, 0x08010000u, OTA_STORE_OK);
    installation_fixture();
    CHECK(ota_install(&io, model.package, key, &record));
    check_install_diagnostic(OTA_INSTALL_STAGE_NONE, 0, 0, OTA_STORE_OK);
    CHECK(!ota_install(NULL, model.package, key, &record));
    check_install_diagnostic(OTA_INSTALL_STAGE_AUTHENTICATE, 0, 0, OTA_STORE_OK);
    ++checks;
}

/** 功能：验证安装顺序、所有副作用边界及完整 Flash 摘要。参数：无。返回：无。 */
static void test_installer(void) {
    static const ota_install_stage_t failure_stages[] = {
        OTA_INSTALL_STAGE_NONE,
        OTA_INSTALL_STAGE_PERMIT_JOURNAL, OTA_INSTALL_STAGE_PERMIT_JOURNAL,
        OTA_INSTALL_STAGE_PERMIT_JOURNAL, OTA_INSTALL_STAGE_PERMIT_JOURNAL,
        OTA_INSTALL_STAGE_PERMIT_JOURNAL, OTA_INSTALL_STAGE_ERASE_FOOTER,
        OTA_INSTALL_STAGE_ERASE_APPLICATION, OTA_INSTALL_STAGE_ERASE_APPLICATION,
        OTA_INSTALL_STAGE_ERASE_APPLICATION, OTA_INSTALL_STAGE_PROGRAM,
        OTA_INSTALL_STAGE_PROGRAM, OTA_INSTALL_STAGE_PROGRAM,
        OTA_INSTALL_STAGE_FOOTER_PROGRAM, OTA_INSTALL_STAGE_FOOTER_COMMIT,
        OTA_INSTALL_STAGE_TRIAL_JOURNAL, OTA_INSTALL_STAGE_TRIAL_JOURNAL,
        OTA_INSTALL_STAGE_TRIAL_JOURNAL, OTA_INSTALL_STAGE_TRIAL_JOURNAL,
        OTA_INSTALL_STAGE_TRIAL_JOURNAL
    };
    ota_install_io_t io = install_io();
    ota_record_t record = {0};
    uint8_t key[32];
    unsigned total;
    unsigned event;
    unsigned mode;
    size_t index;
    test_key(key);
    installation_fixture();
    baseline = model;
    CHECK(ota_install(&io, model.package, key, &record));
    CHECK(trusted_flash());
    CHECK(record.state == OTA_STATE_TRIAL && record.attempts == 0);
    CHECK(model.erases == 4 && model.sectors[0] == 7 && model.sectors[1] == 4 && model.sectors[2] == 5 && model.sectors[3] == 6);
    CHECK(model.flash[0x10000 + 8193] == 0xFF);
    total = model.events;
    CHECK(total + 1 == sizeof(failure_stages) / sizeof(failure_stages[0]));
    for (mode = 0; mode < 3; ++mode) {
        for (event = 1; event <= total; ++event) {
            model = baseline;
            memset(&record, 0, sizeof(record));
            model.fail_event = event;
            model.failure_mode = mode;
            CHECK(!ota_install(&io, model.package, key, &record));
            CHECK(ota_install_diagnostic().stage == failure_stages[event]);
            CHECK(ota_install_diagnostic().store_result == ((event <= 5 || event >= 15) ? OTA_STORE_IO : OTA_STORE_OK));
            model.dead = false;
            if (event <= 5) {
                CHECK(model.erases == 0 && trusted_flash());
            }
            if (trusted_flash()) {
                CHECK(memcmp(model.flash, baseline.flash, sizeof(model.flash)) == 0 ||
                    memcmp(model.flash + 0x10000, model.package + 256, 8193) == 0);
            }
            CHECK(model.eeprom[0] == 0xA5 && model.flash[0] == 0xFF && model.flash[0xFFFF] == 0xFF);
        }
    }
    model = baseline;
    model.corrupt_flash_read = true;
    CHECK(!ota_install(&io, model.package, key, &record));
    CHECK(!trusted_flash());
    model = baseline;
    model.bad_erase = true;
    CHECK(!ota_install(&io, model.package, key, &record));
    CHECK(model.erases == 1 && trusted_flash());
    model = baseline;
    model.read_failure = true;
    CHECK(!ota_install(&io, model.package, key, &record));
    CHECK(model.erases == 0 && trusted_flash());
    model = baseline;
    model.package[192] ^= 1;
    CHECK(!ota_install(&io, model.package, key, &record));
    CHECK(model.events == 0 && trusted_flash());
    model = baseline;
    model.fetch_failure = true;
    CHECK(!ota_install(&io, model.package, key, &record));
    CHECK(model.erases == 4 && !trusted_flash());
    model = baseline;
    model.package[256 + 4000] ^= 1;
    CHECK(!ota_install(&io, model.package, key, &record));
    CHECK(!trusted_flash());
    model = baseline;
    put32(model.package + 260, OTA_APP_BASE + 0x188u);
    ota_sha256(model.package + 256, 8193, model.package + 64);
    sign_header(model.package);
    CHECK(!ota_install(&io, model.package, key, &record));
    CHECK(!trusted_flash());
    model = baseline;
    for (index = 256 + 8193; index < sizeof(model.package); ++index) {
        model.package[index] = (uint8_t)(index % 233);
    }
    model.package_size = sizeof(model.package);
    put32(model.package + 20, OTA_IMAGE_MAX);
    ota_sha256(model.package + 256, OTA_IMAGE_MAX, model.package + 64);
    sign_header(model.package);
    CHECK(ota_install(&io, model.package, key, &record));
    CHECK(trusted_flash());
    CHECK(model.flash[0x7FEFF] == model.package[sizeof(model.package) - 1]);
    printf("installer fault boundaries: %u x 3\n", total);
    ++checks;
}

/** 功能：运行所有公共 OTA 回归测试。参数：无。返回：进程结果。 */
int main(void) {
    test_crypto();
    test_image();
    test_trusted_metadata();
    test_journal();
    test_installer_diagnostics();
    test_installer();
    printf("OTA core tests passed: %u groups\n", checks);
    return 0;
}
