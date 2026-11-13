# Local alarm stage 1 implementation plan

**Goal:** Add schematic-correct buzzer, three keys, eight editable local alarms, and EEPROM power-loss configuration recovery, preserving clock/weather/AHT20.

**Authority:** User explicitly requested direct implementation of eight local alarms. Schematic `D:/study/天气时钟2025/4、文档资料/原理图 V1.2.6.pdf`, pages 1/2/5/6/7/9, visually inspected.

**Hardware:** PC7 buzzer through NPN, prefer TIM3 CH2 PWM about 2.73kHz with GPIO active-buzzer build option. PA0/PC4/PC5 external pull-ups, LOW pressed. H1 keeps the user-verified working soldered state; investigate the shared ESP wake signal only if interference appears. EEPROM I2C1 PB6/PB7, 100kHz, address 0x50, A0/A1/A2/WP grounded; B part per user, A marked schematic. AHT20 remains I2C2. Keep HAL TIM7/FreeRTOS exception ownership and proven SPI transport.

**Architecture:** Portable alarm engine with Gregorian dates, weekly masks and once dates; monotonic ringing/snooze deadlines; one local alarm service owns engine and UI editing; EEPROM worker persists snapshots then reports completion. Configuration switches only after durable save. Startup restores and starts local service before any WiFi wait. UI producers serialize full updates so menus are not overwritten by normal clock/weather updates. Main functions continue sampling/networking during menus.

**Behavior defaults:** Eight disabled entries; 60s ring, 5min snooze; no catch-up on boot or large wall-clock jumps; exact FIFO history of the last 32 fired dates per unchanged entry prevents repeats without suppressing unseen earlier dates after time correction. Volatile snooze/ringing/history are not restored after reset. Missing/invalid time inhibits scheduled triggers, while monotonic 10s diagnostic alarm remains available. Missing/corrupt storage leaves schedules disabled; failed saves never report success. Readable empty/corrupt storage can be initialized by an explicit user save. Three-key editor supports enabled/time/date/weekday/snooze/duration and manual RTC setting for offline first use. All local pages use existing ASCII fonts.

## Tasks
- [x] Hardware: I2C1, PC7 TIM3 CH2, active-low debounced short/long key events, BL24C512B page-correct bounded HAL reads/writes, IOC alignment; native hardware boundary tests.
- [x] Engine: validate/calendar/defaults/simultaneous/weekly/once/stop/snooze/time changes/tick wrap; test-first portable native suite.
- [x] Store: explicit encoding, two 1024B slots, independent commit marker page, generation + CRC + readback, preserve old slot; fault-injection tests for power loss and corruption.
- [x] Service/UI: asynchronous save, button configuration, manual clock, test alarm, display ownership and restore current normal values; service tests.
- [x] Integrate: start offline service before networking, keep weather/AHT20, update CMake/IOC/options and host test runner.
- [x] Verify: host suite, Debug/Release/RGB builds, memory, fresh independent review, schematic-specific board test guide.

## Review focus
Power interruption at every EEPROM write boundary; save fails while another alarm rings; invalid RTC and network timeout; short/long-key disambiguation; simultaneous alarms and snooze around tick wrap; display ownership and bounded task execution.

## Progress
Work stays in the requested checkout/branch, preserving all existing uncommitted migration fixes; no commit, push, or flash requested. Shared contracts are `app/alarm/alarm_types.h`, `alarm_engine.h`, `alarm_store.h`. Delegated work has disjoint ownership; root owns service/UI/build/startup and test runner.

Final review findings fixed and re-reviewed: recovery after failed/stale boot reads now uses strict verified baseline + changed-field merge before durable save; contradictory selected-slot rereads fail without writes. Exact-date history fixes future-year correction. Snooze has modal visible page consistent with keys. Every bounded EEPROM HAL transfer protects scheduling individually, with interrupts enabled and pages yielding, preventing UI preemption from consuming polling timeouts. Diagnostic and all eight scheduled IDs remain visible together.

Final verification 2026-11-13: `tests/run_host_tests.ps1` exit 0 (engine297/store937/service21 plus all original suites and new menu/page/hardware). Clean Debug and Release exit 0, no warnings: RAM115272B, Flash243556/210460B. Independent RGB (RAM99984B/Flash47232B) and active-buzzer Release (RAM115200B/Flash209012B) builds pass. Default Debug/Release cache remains RGB=OFF/ACTIVE=OFF. Logs `build/stage1-validation/`; board steps `docs/ALARM_STAGE1_TESTS.md`. No hardware execution claimed.
