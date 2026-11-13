# OneNET MQTT Console Control Implementation Plan

> **For agentic workers:** Use subagent-driven-development for independent transport and OneJSON codec work, and executing-plans for root integration. Track observed checks below.

**Goal:** Continue the user-approved OneNET console-first goal from PREP to real TLS MQTT connection, telemetry and eight persistent alarm commands.

**Architecture:** ESP-AT4.1.1 (MINI-1, confirmed by user) owns MQTT/TLS. The existing single network worker serializes AT transactions and runs a cloud state machine; ISR length framing separates MQTT messages from HTTP/TCP replies. OneJSON commands reuse alarm owner and the sole EEPROM worker. Cloud success follows verified durable configuration apply.

**Tech Stack:** STM32F407 HAL/FreeRTOS/C11, ESP32C3 AT4.1.1, MQTT3.1.1/OneJSON, BL24C512B, ARM GCC and native MSVC.

**Spec/authority:** Existing user request to develop OneNET remote control, console-first choice, and firmware information supplied after PREP. Product4lh76atTpR/deviceclock001, TLS domain in local/public config. Continue in requested checkout, preserve uncommitted LAN webpage and credentials; no Git commit, cloud authentication from PC, firmware flashing or board wiring changes.

## Constraints and reference decisions

- TLS scheme3 with dedicated official OneNET CA and ESP SNTP first; never silently switch to scheme2/plaintext. Revised after exact4.1.1 library audit and public TLS probe: OneNET explicitly omits SNI and DNS-name matching because the current official selfsigned CA/server certificate lacks the product DNS name. CA-chain/date checks remain; this policy is explicit in code/logs/docs, not an automatic downgrade. Generic driver NULL/default still uses the host SNI. See v1.4.2 follow-up below.
- Long password command avoids 64-byte inline-token limit; raw publish uses prompt then +MQTTPUB:OK. No credentials in logs or test vectors.
- MQTT RX topics<=128B/payload<=1536B, fixed bounded queue in explicitly initialized CCM. Every wait bounded, packet overflow and serial desync fail loudly.
- OneNET identifiers: temperature,humidity,sensor_valid,config_revision,ringing_mask,alarm1..alarm8. Each alarm is a struct with enabled/hour/minute/repeat_mask/year/month/day/ring_seconds/snooze_seconds. Partial single-slot edits merge onto a fresh snapshot; reject multi-slot or invalid commands without partial writes.
- Same cloud requestID+same patch reuses pending/final result in a bounded cache; ID collision with another patch rejects. Dedup is session-local, no unsupported claim of cross-power-loss exactly-once processing.
- OneNET property set/get deadline is5s. Prioritize command replies, process messages between bounded web response chunks, use HTTPCGET with short network timeout. DNS/TLS/trickle networking cannot have an end-to-end5s guarantee on this AT firmware; MCU watchdog invalidates uncertain transactions and recovers rather than applying queued stale commands. Weather and local features stay available; fault limits must be documented.

## Tasks

- [x] Transport: esp_mqtt.h + ESP AT implementation and existing ESP tests. CA compare/readback, long-token, SNI/config/connect/status, subscribe/publish, length-framed unsolicited receive, bounded weather GET and data-mode recovery; RED→GREEN native wire tests.
- [x] OneJSON codec: app/onenet/onenet_model.c/.h + native tests. Strict bounded parser, numeric-ID extraction, single-slot patch/date checks, raw GET vs wrapped POST serialization, size limits and malformed inputs.
- [x] Cloud integration: onenet_service.c/.h, public CA const, app/main/CMake/test integration. Connect/subscription/backoff, telemetry, actual8-slot boot/change reports, dedup, deferred EEPROM result acknowledgements, cloud get, deadline handling.
- [x] Shared network scheduling: preserve sensor/weather/LAN behavior, publish sensor snapshots without AT from sensor task, short weather mode while configured, MQTT maintenance hook between web chunks, regression tests.
- [x] Fresh independent review: ISR/data-mode boundaries, secret handling, ACK ordering, reconnection/cache, CCM init/memory, telemetry/platform status semantics.
- [x] Verify and document: full native suite, fresh Debug/full Release, RGB/active build compatibility, exact flash/RAM, cloud field setup and UART-led board tests. No actual OneNET online claim without board observation.

## Review focus

MQTT payload containing OK/ERROR/newlines during HTTP; oversized/split frames and queue overflow; partial token/CA/publish writes and MCU-only reset; duplicate IDs vs changed patch; local edit racing cloud submit; EEPROMfailure and lost cloud ACK; OneNET POST reply non200; expiredtoken/missingtime/cert failures; slow weather/webpage with command arrival; same-slot edits and all8 GET payload budget.

## Verification log

v1.4.3 follow-up: realboardlogv1.4.2 CONNECTOK followed STATEqueryOK/error6; SNTP, AHT20, weather and EEPROMboothealthy. Exacttaggedlibrary queryformatter quotes numericport; oldparseronlyacceptedunquotedport. AddedexactfixtureRED(oldconnectfalse), narrowoptionalquotednumericportfix, GREEN parserandbothESPresetmodefulltransporttests. New3statepositive/7rejectioncases keependpoint/scheme/state/disconnectchecks. No service/credentials/securitypolicy changes. Independentreadonlyreview found no issues. Debug308012/116600/15900, Release259204/116592/15900 bytesFlash/RAM/CCM, bothbuildexit0/no warnings. Evidence state-port-{red,green,Debug,Release}.log. Needflashv1.4.3 thenverifyactualonline/subscriptions/reportaccepted; noactualboardflashorMQTTauthfromPCorGitcommit.

v1.4.2 follow-up: user now reaches MQTTCONN with bareERROR/ESPcode0. Exact official AT4.1.1 library does not expose runtime MQTT_EVENT_ERROR details viaSYSLOG; it maps to genericERROR. The same library maps nonemptySNI tocommon_name with namechecking enabled, absentSNI skipsnamecheck. Public no-auth TLS1.2/1.3 handshake validates currentofficialCA successfully withoutnamecheck, while producthostnameverification fails. Fresh officialdownload matches linkedCA. This is a confirmed configuration incompatibility; board cause/online state still require observation. OneNET service explicitly chooses emptytls_server_name to omitSNI, retainsmandatoryCLEAN/scheme3/SNTP/CA/date checks, logspolicy. Generic NULL preserveshostSNI, explicitDNSname/invalidinput tests added. RED olddriver/service observed, GREEN ESPbothresetvariants andservice. Independentreview found no codeblockers; staleSNI docs corrected. Debug307844/116600/15900, Release259140/116592/15900 Flash/RAM/CCMbytes, both exit0/no warnings. Evidence sni-{red,service-red,green,service-green,Debug,Release}.log. UserconfirmedDEVICEkeysource; productdevstage allowsdebug per officialworkflow, stateunactivated doesnotrequirepublication. No credentials read, MQTT login fromPC, boardflash, Gitcommit or branch changes.

v1.4.1 follow-up: user board reports TLS/credential configuration rejection3. Official ESP32-C3 AT4.1.1 tagged library confirms fresh/clean state causes CLEAN to return ERROR/0x010a6003. Native reproduction failed before fix. Setup now enables SYSLOG and treats only that exact synchronized CLEAN error as idempotent, while preserving all other failures. A fixed-label/ACK/numeric-code diagnostic API supplies actionable logs without command arguments, raw replies or Token data. Stage-specific and negative cases pass with both ESP reset variants; service diagnostics/secret-exclusion assertion passes; independent read-only review found no concrete issues. Debug307668/116600/15900, Release259028/116592/15900 bytes Flash/RAM/CCM; both builds exit0 with no warnings/errors. Evidence `cleanup-{red,green,service,debug,release}.log`. Actual failed board step and cloud connection still require flashing v1.4.1 and observing logs; no credentials or board access used.

2026-11-13: root full native runner exit0. OneJSON204 checks, application11 scenarios, alarm engine297 checks, EEPROM937 scenarios, alarm service41 scenarios, ESP parser and both reset modes, HTTP/web/session/renderer/RTC/AHT20/console and hardware substitutes passed. Final service runner also passes after final formatting-only warning fix.

Independent review fixes include fresh per-message age checks after blocking sends; strict-framing recovery without dropping nested JSON fragments or leap-year patches; explicit cloud field masks through durable merge; retaining eight owner results; offline pending ACK scheduling; backoff and weather retry cadence across ESP recovery. Regression tests observed failing before fixes and passing after.

ARM GCC14.3.1: final Debug306464B Flash/116592B RAM/15900B CCM; Release258096/116584/15900. Independent RGB51720/100472/5028 and active-buzzer303608/116512/15900 also passed. Final firmware logs contain no compiler warnings/errors. Default build directories retain full clock/PWM options OFF/OFF. ELF inspection confirms ARM and NOLOAD CCM buffers. Original Debug ELF denies deletion while open elsewhere, so its objects were fully rebuilt and a fresh independent `build/OneNetDebugVerify` Debug configure/build also passed without this cleanup issue. No debugger was interrupted.

Evidence: `build/onenet-validation/host-tests.log`, `service-final.log`, `debug-rebuild.log`, `debug-fresh.log`, `release-final.log`, `rgb-final.log`, `active-buzzer-final.log`. Public certificate provenance is in onenet_ca.c; private local headers are excluded from tracked source. No real cloud authentication, board flashing, physical tests or Git commit performed. Board tests remain required per `docs/ONENET_MQTT_TESTS.md`.
