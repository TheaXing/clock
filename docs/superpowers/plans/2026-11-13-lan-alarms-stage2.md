# LAN Alarm Web Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** View/edit/toggle all eight persistent alarms from a phone on the same WiFi while keeping all validated local functions.

**Architecture:** One ESP-AT command owner with passive TCP server reception; bounded portable HTTP parsing and flash-only webpage; asynchronous alarm-owner requests reuse the single transactional EEPROM worker. Saved acknowledgement follows durable write and runtime apply.

**Tech Stack:** STM32F407 HAL/C11, FreeRTOS, ESP32C3 ESP-AT, BL24C512B, vanilla HTML/CSS/JavaScript, CMake ARM GCC, native MSVC tests.

**Spec:** `docs/superpowers/specs/2026-11-13-lan-alarms-design.md`

## Global Constraints

- Work in the requested dirty checkout; preserve uncommitted first-stage migration, no commit/flash or ESP firmware replacement.
- Keep H1 and all already validated hardware settings unchanged.
- No heap allocation per HTTP request, external webpage resources, extra server or cloud.
- Exactly eight alarm slots; retain engine/calendar validation and strict transactional EEPROM rules.
- ESP commands remain serialized in network worker; bounded buffering and waits; never block alarm owner on HTTP.

## Review Focus

- Fragmented TCP, two browser connections and socket-ID reuse cannot mix requests/responses.
- POST queued/response lost is distinct from EEPROM saved; never acknowledge failed persistence as saved.
- Two phones/local edits use optimistic versions and owner recheck; stale clients cannot silently overwrite.
- HTTP data containing terminal-looking AT lines cannot satisfy a command wait.
- Unsupported server commands, ESP reset, network timeout and malformed requests preserve local operation.

### Task 1: Alarm service API
Files: `app/alarm/alarm_service.c/.h`, existing service native tests/stubs/runner. Expose `alarm_service_snapshot`, `alarm_service_remote_submit`, `alarm_service_remote_result`; snapshots copy config/version/status, submit is nonblocking with owner-side validation, results distinguish pending/saved/failed/unknown.
- [x] Add failing tests for async persistence success/failure, stale version, local editor, owner race, and ringing responsiveness.
- [x] Implement queues and copied snapshots, reuse strict save worker and apply actual durable config only on success.
- [x] Pass targeted `/W4 /WX` service suite and inspect native allocation-failure compatibility.

### Task 2: ESP passive server transport
Files: `BSP/ESP_AT/*`, ESP native tests/stubs/runner. Expose start/ready/is_ready/poll/send/close; event has link, generation, 256-byte payload. Configure port80/passive receive, old/new command compatibility, IPv4 discovery; retain prior HTTPCLIENT parsing.
- [x] Add failing wire tests for passive framing, prompt vs SEND OK, unsolicited lines during other commands, fragmentation, ID reuse and recovery.
- [x] Implement bounded ISR demultiplexing, sequential passive reads and generation-aware send/close.
- [x] Pass old and new ESP native suites; verify all failures remain bounded.

### Task 3: Portable HTTP and mobile UI
Files: `app/web/clock_http.c/.h`, `web_page.c/.h`, new native parser/UI tests. Feed streaming parser to NEED_MORE/READY/error with target64/body384/header1536; static page exports byte array and size.
- [x] Add failing tests for request fragmentation, size limits, duplicate lengths, transfer encoding, method/headers and embedded NUL.
- [x] Implement bounded parser and self-contained Chinese 8-slot editor consuming the spec endpoints.
- [x] Verify parser suite and page async/error/conflict behavior; no external assets.

### Task 4: HTTP backend and network integration
Files: `app/web/clock_web.c/.h`, `app/app.c/.h`, app test stubs, `CMakeLists.txt`, host runner; root owns this task. Backend tracks five connection parsers and generation, reports URL, JSON-encodes snapshots and validates complete numeric forms before submit.
- [x] Write failing backend tests for GET actual snapshot, POST complete validation, stale session/version, pending→saved/failed, segmented requests and reconnect.
- [x] Implement APIs and wire network polling at 100ms with server retry/recovery; keep original network task cadence and data caching.
- [x] Run backend/app suites, integrate all sources and runners; review static RAM and task stack.

### Task 5: Final verification and board instructions
- [x] Independent review of streaming boundaries, concurrency and save acknowledgements; fix concrete findings with regressions.
- [x] Full host suite, clean Debug/Release, independent RGB/active builds; confirm normal outputs are full clock PWM.
- [x] Write Chinese phone/board test guide including URL discovery, 8-slot edits, two-client conflicts, reconnect, EEPROM reset retention and old-function regressions. Update plan with observed results, report no physical tests claimed.

## Progress

Authorized scope and design above derive from user's direct implementation request. Root coordinates disjoint owners; no additional design approval gate is needed. Targeted tests use distinct build directories; root alone runs the full suite after agent work stabilizes.

Final verification (2026-11-13): full host suite exit0; all original suites preserved, engine297/store937/service34, HTTP parser + actual flash JS behavior, backend request/result/conflict/reuse, RNG independent boots, ESP command/data-entry boot modes all pass. Browser390x844 confirmed actual page/edit/save acknowledgement using only a local mock (no board tested). Clean Debug/Release, independent RGB/active builds exit0 with no warnings. Normal Debug/Release retain RGB=OFF/ACTIVE=OFF. Debug/Release RAM115872B, CCM5984B NOLOAD at0x10000000, Flash280472/242392B. Pageconst lies inFlash; BINsizes equalFlashusage. Logs build/stage2-validation; boardguide docs/ALARM_STAGE2_TESTS.md.

Review closed: first-save/retry despite storage warning, exact HTTP error code handling, modal-accessible uncertain refresh, three-way edited-field merge with same-field value display, RNG session independent of RTC (zero disables web), 48MHz PLLQ=7 CPU/APB unchanged, generation recheck before short AT TX (20ms scheduler guard, IRQs enabled), retained/unknown CIPSEND zero-fill recovery including MCU-only NRST. A fresh reviewer found no remaining P1/P2. Original92KiB RTOS heap untouched; HTTP working arrays explicitly initialized in CCM.
