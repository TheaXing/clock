# Wi-Fi Provisioning Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development. Track results here; continue through tests and builds without another approval round (user explicitly authorized implementation).

**Goal:** Boot connection failure exposes phone QR provisioning and persists verified Wi-Fi across power cycles, including OTA recovery.

**Architecture:** One network owner manages provisioning, candidate verification, and web callbacks. Shared pure C EEPROM storage serves App and Boot. Existing UI/alarm display arbitration gains a provisioning page mode.

**Tech Stack:** STM32F407 C11/FreeRTOS, ESP32-C3 ESP-AT 4.1.1, BL24C512, MSVC /W4 /WX, ARM GCC/CMake.

**Spec:** `docs/superpowers/specs/2026-11-14-wifi-provisioning-design.md` (approved 2026-11-15).

## Global Constraints

- Keep existing checkout/branch and user change to `docs/OTA_ADMIN_TESTS.md`; no automatic commit, push or board flashing. Current checkout contains required local build configuration.
- Follow AGENTS.md and Chinese C comments, snake_case, four spaces. Do not display private configuration values.
- EEPROM Wi-Fi range `[0x1000,0x1200)`; two 256-byte slots with independent 128-byte commit pages.
- SSID 1–32 UTF-8 bytes; password empty or 8–63 printable ASCII bytes. No secrets in logs/status.
- Join deadline 20 seconds; result grace 15 seconds; STA disconnect must preserve AP HTTP service.
- Phone hotspot QR then URL QR; URL `http://192.168.4.1/setup`; no captive portal promise.

## Review Focus

1. Interrupted EEPROM writes and commit ACK loss cannot destroy previous credentials or neighboring records.
2. AT timeout/reset, AP+STA channel changes, failed DHCP and wrong SSID cannot become false saved success.
3. Fully percent-encoded UTF-8/special characters, duplicate fields and connection-ID reuse must stay bounded.
4. Alarm takeover and queued display changes must not corrupt QR or suppress local alarm operation.
5. OTA recovery reads the same selected credentials and healthy offline setup still completes image confirmation.

### Task 1: Shared storage and Boot consumption

Files: new `BSP/WiFi/wifi_store.[ch]`, `BSP/WiFi/tests/test_wifi_store.c`, storage runner; modify EEPROM lock, alarm transaction guards, Core init and `bootloader/main.c`, `cmake/ota.cmake`.

Interfaces: `wifi_credentials_t { char ssid[33]; char password[64]; }`; `wifi_store_io_t` read/write callbacks matching alarm callbacks; `wifi_store_result_t` OK/EMPTY/CORRUPT/IO_ERROR/INVALID; `wifi_credentials_valid(const wifi_credentials_t *)`; `wifi_store_load(const wifi_store_io_t *, wifi_credentials_t *)`; `wifi_store_save(const wifi_store_io_t *, const wifi_credentials_t *)`. Stateless load/save, explicit CRC/sequence/commit format and strict update scan. App wraps in EEPROM transaction lock; Boot uses bare-metal callbacks.

- [x] Write and run failing record/validation/power-cut tests.
- [x] Implement shared dual-slot storage, Boot selection, unconditional EEPROM transaction serialization (ordinary and OTA).
- [x] Test boundary/power-cut/ACK-loss/sequence wrap/invalid UTF-8, Boot saved vs default selection, lock behavior both modes.

### Task 2: ESP AP transport

Files: `BSP/ESP_AT/esp_at.[ch]`, `esp_at_parse.[ch]`, tracked `BSP/ESP_AT/tests/`.

Interfaces: `esp_at_setup_start(const char *ssid,const char *password)`, `esp_at_setup_stop(void)`, `esp_at_setup_has_client(bool *connected)`; server_start selects AP IP while setup enabled; `esp_at_wifi_init` explicitly SYSSTORE=0 and CWMODE=1,0. Reset clears AP flag; STA disconnect only invalidates normal STA server. Stop closes server/connections before returning to STA-only mode. Keep same sole AT owner.

- [x] Bring transport regression fixtures into tracked BSP tests; demonstrate missing AP behavior.
- [x] Implement bounded AP+STA/DHCP/IP/client queries and service invalidation semantics.
- [x] Run transport/parser tests including init recovery, unsolicited STA disconnect and AP address without STA.

### Task 3: HTTP and phone page

Files: new `app/web/wifi_form.[ch]`, `wifi_setup_page.[ch]`; modify `clock_web.[ch]`; tracked BSP/WiFi web tests.

Interfaces: `bool wifi_form_parse(const char *body,uint32_t expected_session,char ssid[33],char password[64])`; web stores callbacks registered using `clock_web_set_wifi_handlers(int (*submit)(const char *,uint32_t),void (*status)(char *,size_t,uint32_t))`; `clock_web_setup(bool enabled,uint32_t session)` toggles setup routing/session without changing normal session token. Setup routes / or /setup, /api/wifi/status, /api/wifi/config; only setup routes allowed while enabled. Callback submit returns 202/400/403/409/503; status fills bounded JSON. POST response completes before main network loop runs submitted action. `clock_web_network` receives local transport availability (AP or STA) from root.

- [x] Test valid and invalid form encoding, exact byte limits, split HTTP, disabled routes and header/content-type requirements before implementation.
- [x] Implement self-contained Chinese phone UI with password toggle, session/status fetch, submit, reconnect polling; never echo password.
- [x] Run web regressions and existing OTA web tests, adapting only mocks/link lists when required.

### Task 4: Coordinator and QR UI

Files: `app/wifi.[ch]`, `app/app.c`, `app/page/wifi_page.c`, new `BSP/QRCode/*`, root CMake/test runners.

Interfaces: retain `wifi_init`, `wifi_ready`, `wifi_mark_unready`, `wifi_wait_connect`; add `wifi_poll(uint32_t now_ms)`, `wifi_setup_active(void)`, `wifi_setup_available(void)`, `wifi_get_view(wifi_view_t *)`, `wifi_setup_submit(const char *,uint32_t)`, `wifi_setup_status(char *,size_t,uint32_t)`; network task alone mutates state, critical sections copy display snapshot. `wifi_view_t` holds active/ready/client/state, AP name/password and generation for display only.

- [x] Write state machine tests first (missing/compiled/saved credentials, failed join, candidate/target query, response ordering, save failure/success, grace/ESP reset, no re-provision after ordinary runtime drop).
- [x] Implement explicit state machine and runtime saved credential priority; use hardware RNG for AP password and session.
- [x] Integrate web AP availability, pause cloud/weather/OTA discovery during setup, continue OTA health poll.
- [x] Add mature fixed-budget QR encoder with license, independent decode tests, black run drawing through existing UI queue; integrate display mode under existing display mutex with alarm priority.
- [x] Test page restoration and QR payload/dimensions; update CMake and tracked test driver.

### Task 5: Review and verification

- [x] Independent code review of all changes and approved spec; resolve concrete findings with regression coverage.
- [x] Run `scripts/test.ps1` plus new tracked Wi-Fi/ESP/QR suites.
- [x] Build Debug, Release, OtaDebug, OtaRelease using scripts/build.ps1; report memory budgets and warnings.
- [x] Add `docs/WIFI_PROVISIONING_TESTS.md`, screen/page preview artifacts where practical, clearly separate host evidence from pending hardware acceptance.

## Execution ledger

- 2026-11-15: User approved design and implementation/tests/builds. Existing dirty OTA_ADMIN_TESTS preserved. Continue in current feature checkout; no new approval/worktree/commit flow needed.

- Task 1 complete: storage, unconditional EEPROM transactions and real Boot selection; 770 power-cut cases plus malformed/read/ACK cases; seven lock scenarios in both OTA modes; six Boot entry scenarios.
- Task 2 complete: tracked ESP/parser suites in both reset modes; explicit AP/STA, fixed DHCP and bounded join; failed candidate cleanup closes all server connections and disconnects STA before accepting another request.
- Task 3 complete: bounded form parser, setup-only HTTP routing, Chinese phone page and browser behavior. HTTP callbacks only enqueue candidates.
- Task 4 complete: state coordinator, hardware identity adapter, real app task integration, QR encoder and two-step screen. Independent pixel references and ZXing decode verified the rendered page images.
- Final review: independent foundation and application/QR reviews complete. Fixed three P2 findings with RED→GREEN evidence: Boot DHCP after legacy NVS reset; runtime join failure miscounted as transport failure; writable STA exposure after failed candidate. Cleanup also invalidates old TCP generations and HTTP fragments.
- Final verification: scripts/test.ps1 exit 0; additional failed-candidate retry tests pass; Debug, Release, OtaDebug and OtaRelease build exit 0 with no compiler warnings/errors. Evidence in build/wifi-validation/. QR preview visually inspected. User OTA_ADMIN_TESTS change preserved; no private configuration printed or changed; no commit, push or flashing.
- Build environment resolution: OtaRelease cache referenced an expired temporary header; explicit -OtaConfig app/ota_config_local.h restored the existing repository configuration without editing credentials.
- Hardware acceptance remains pending as documented in docs/WIFI_PROVISIONING_TESTS.md; software tests are not claimed as board evidence.
