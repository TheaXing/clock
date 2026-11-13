# Clock migration implementation plan

**Goal:** Restore all clock features compiled by `11-1FreeRTOS - keil` in `Clock_CubeMX`, keeping the working HAL ST7789 transport and GCC FreeRTOS port.

**Spec:** The user's approved migration proposal in this chat. Source: `D:/code/Clock Project/11-1FreeRTOS - keil/mdk/stm32f407.uvprojx`.

**Architecture:** One LCD owner task, independent clock and sensor tasks, and one network worker owning all ESP transactions. Periodic work uses task delays/deadlines, so no software timer callback can block on a queue. HAL keeps TIM7; FreeRTOS keeps SysTick/PendSV/SVC.

**Constraints:** STM32F407VE, 512 KiB Flash, 128 KiB SRAM plus 64 KiB CCM; retain current screen SPI framing/byte order. Do not copy SPL, duplicate startup, old main/board/hooks, or old RTOS ports. Preserve existing local screen fixes. Use GBK only for imported GBK files, not globally. Preserve legacy credentials in an ignored local configuration file without printing them. Compile and link Debug and Release; hardware behavior remains a board test.

## Tasks

- [x] HAL peripherals and adapters: USART2 PA2/PA3 115200 IRQ5; I2C2 PB10/PB11 100 kHz; RTC/LSE with finite failure and backup retention; USART1 logging. Update IOC and HAL modules.
- [x] ESP transport: bounded independent buffers, prepare RX before TX, mutex/ACK handling, continuously rearmed RX/errors, preserved WiFi/SNTP/HTTP APIs. Test parsing and receive behavior with host fixtures.
- [x] UI: active pages, 8 fonts, 13 images; queue-owned copied strings; safe GBK termination and drawing boundaries. Test actual renderer with captured HAL writes.
- [x] Application: startup/connect/error/main flows; time every second, WiFi every 5 seconds, AHT20 every 3 seconds, weather every minute, SNTP hourly and retry. Offline operation and reconnect. Nonblocking diagnostic workqueue submission. Test malformed weather payloads and wraparound deadlines.
- [x] Integration: explicit CMake source list, selected GBK compilation flags, nano float formatting, build helper, ELF/HEX/BIN outputs.
- [x] Verify: host tests, clean Debug/Release builds, linker memory use, fresh independent review, board test instructions.

## Review focus

RX interrupts while commands time out; disconnected ESP with local clock/sensor still running; RTC calendar retention across reset; malformed/truncated weather data; multilingual resource bytes and exact screen edges; queue pressure and task creation/memory failure.

## Historical implementation record

The entries below describe the original migration session. Memory figures and verification results are historical; the split migration snapshot requires fresh host/Debug/Release/RGB verification and new map/size measurements.

- Baseline Debug rebuilt successfully before integration: RAM 96824 bytes, Flash 23960 bytes.
- Working branch: `codex/migrate-weather-clock`; work remains in the requested directory to retain the user's uncommitted, board-verified main/ST7789 baseline. No commits or flashing requested.
- Peripheral, ESP, and UI work delegated with disjoint file ownership; root owns application/build integration. The approved chat proposal is the implementation authority.
- Final independent review findings fixed and regression-tested: concatenated segmented HTTP bodies; whole-stdio-call locking; three-character measurements; offline startup placeholders; ready-only soft-reset transport synchronization.
- Final host suite: `tests/run_host_tests.ps1` exit 0, including five real-application task scenarios and all peripheral/renderer/transport suites.
- Final Debug: Flash 340332 bytes, RAM 113536 bytes. Final Release: Flash 318296 bytes, RAM 113536 bytes. Both link without warnings; ELF/HEX/BIN generated. RGB smoke target also builds separately in `build/RgbSmoke`.
- Physical board verification remains in `docs/BOARD_TESTS.md`; no flashing, commit, merge, or push performed.
