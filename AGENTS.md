# C3-Cascade — Agent Instructions

## Project Overview

Wireless split keyboard firmware: ESP-NOW or WiFi UDP inter-half communication (with auto-discovery), BLE HID to host, deep sleep power management. Targets Seeed Studio XIAO boards (ESP32-C3 primary, ESP32-C6) and Raspberry Pi Pico 2W (RP2350). nRF52840 is a skeleton port.

## Build & Flash

```bash
# Build (PlatformIO)
pio run -e xiao_esp32c3          # Master (left half) — ESP32-C3
pio run -e xiao_esp32c3_slave    # Slave (right half) — ESP32-C3
pio run -e xiao_esp32c6          # C6 master
pio run -e pico2w                # Master — Pico 2W (RP2350)
pio run -e pico2w_slave          # Slave — Pico 2W (RP2350)

# Flash
pio run -e xiao_esp32c3 -t upload
pio run -e pico2w -t upload

# Serial monitor
pio device monitor -b 115200
```

## Architecture

- **Master (left half):** Scans local matrix → receives slave matrix via split link → merges → resolves keycodes → sends BLE HID report
- **Slave (right half):** Scans local matrix → sends MATRIX_REPORT to master on change → processes heartbeats from master
- **HAL abstraction:** `namespace hal` in `include/hal/hal.h` provides GPIO/timing/power/system/LED API; MCU-specific implementations in `src/hal/hal_esp32.cpp`, `src/hal/hal_rp2040.cpp`, `src/hal/hal_nrf52.cpp` (skeleton)
- **Split transports:** Unified `split_link.cpp` handles both ESP-NOW for ESP32 and WiFi UDP for cross-platform (Pico W).
- **Dual Mode (Master):** ESP32 masters (C3/C6) support both ESP-NOW and WiFi UDP simultaneously — they can accept an ESP32-C3 slave and a Pico W slave at the same time.
- **Auto-discovery:** All transports use a DISCOVER/DISCOVER_ACK handshake — no hardcoded MAC addresses needed.

### Split Transport Selection

| Board | Role | Transport Support |
|-------|------|-------------------|
| ESP32-C3/C6 | Master | **Dual Mode** (ESP-NOW + WiFi UDP) |
| ESP32-C3/C6 | Slave | ESP-NOW |
| Pico 2W | Master | WiFi UDP |
| Pico 2W | Slave | WiFi UDP |
| nRF52840 | — | None (stub) |

1. **Master** boots → creates SoftAP (WiFi UDP) AND/OR listens for broadcasts (ESP-NOW)
2. **Slave** boots → scans (WiFi UDP) or broadcasts (ESP-NOW) DISCOVER packets
3. **Master** receives DISCOVER → registers slave (tracks transport type) → replies DISCOVER_ACK with assigned ID
4. **Slave** receives ACK → paired, begins sending MATRIX_REPORT via unicast

1. `matrix_scan()` — 1kHz, 5ms debounce
2. `check_pairing_shortcuts()` — detect ESC+SPACE hold (BLE PC), ESC+SHIFT hold (slave search) on master, or 7+SPACE hold on slave (5s)
3. `check_pairing_mode_timeout()` — exit pairing mode after 60s or on success (master)
4. `split_link_update()` — auto-discovery + receive/send (ESP-NOW or WiFi UDP)
5. `build_hid_report()` — merge matrices, resolve keycodes, fill 6KRO report (key suppression during pairing mode)
6. `ble_hid_send_report()` — 8ms rate limit
7. `update_led()` — blink LED during pairing/searching, solid on when connected
8. `power_check_sleep()` — deep sleep after 60s idle (inhibited during pairing mode)

## Key Conventions

- **Row-driven matrix:** ROW pins driven LOW one at a time; COL pins read as INPUT_PULLUP. Diodes: 1N4148, cathode towards column.
- **Modifier encoding:** `MOD_KEY(mod)` = `0xF000 | mod`, `IS_MOD_KEY(code)` checks upper byte, `GET_MOD_BIT(code)` extracts bit. Defined in `keymap.h`.
- **Matrix state:** `matrix_state_t` = `uint8_t rows[5]`, each byte is a row, bits are columns.
- **Split packets:** `split_packet_t` (~18 bytes). Types: MATRIX_REPORT (0x01), HEARTBEAT (0x02), SYNC (0x03), ACK (0x04), DISCOVER (0x10), DISCOVER_ACK (0x11).
- **Feature flags** in `config.h`: `ENABLE_BLE_HID`, `ENABLE_SPLIT_LINK`, `ENABLE_DEEPSLEEP`, `ENABLE_BATTERY_REPORT` (off), `ENABLE_RGB` (off), `ENABLE_LED` (on).
- **Transport flags** in `config.h`: `SPLIT_TRANSPORT_ESPNOW`, `SPLIT_TRANSPORT_WIFI_UDP`. Master enables both when board supports both. Slave selects one based on board type.
- **Board-specific pins** in `pins.h` — always use these macros, never raw GPIO numbers.
- **LED pin** in `pins.h` — `LED_PIN`: Pico W/Pico 2W use `LED_BUILTIN` (CYW43 onboard LED), other boards use `0xFF` (no LED).
- **Pairing mode**: Master enters pairing mode via ESC+SPACE (BLE PC) or ESC+SHIFT (slave search) held 5s. All keys suppressed except ESC/SHIFT/SPACE during pairing. Auto-exits after 60s or on successful connection.
- **LED blink**: Master blinks during pairing mode; slave blinks while searching for master, solid on when connected. Only on Pico W/Pico 2W.
- **Sleep inhibition**: Deep sleep is blocked during pairing mode (master) or discovery (slave).

## Pitfalls & TODOs

- **ESP32-C6 deep sleep:** Only GPIO 0,1,2 are LP-capable; columns 2–5 (GPIO 21–23) cannot wake from deep sleep.
- **nRF52840 is skeleton:** HAL, BLE, and split link are all stubs. `enter_deep_sleep()` halts, `woke_from_deep_sleep()` returns false.
- **Pico 2W deep sleep:** Uses WFI + watchdog reboot (light sleep ~2mA). Full dormant mode (~150µA) is a TODO.
- **Pico 2W BLE HID:** Uses `PicoBluetoothBLEHID` from arduino-pico core — may need API adjustments per core version. Pairing mode (`ble_hid_enter_pairing_mode()`) is not yet supported on BTstack.
- **Layer 1 (FN) is empty:** All keys are `HID_KEY_NONE` — needs key assignments.
- **BLE PnP IDs are placeholders:** Vendor `0x05AC` (Apple), Product `0x820A` — should be changed.
- **Pico 2W pins are defaults:** GP2–GP13 — update in `pins.h` to match actual wiring.
- **XIAO LED:** XIAO boards have WS2812 RGB LEDs (not simple on/off). `ENABLE_LED` is compiled in but `LED_PIN=0xFF` means LED functions are no-ops on these boards.
- **Pico W LED requires WiFi:** The onboard LED is on the CYW43 WiFi chip. `hal::led_init()` sets the pin mode, but `digitalWrite(LED_BUILTIN, ...)` only works after CYW43 is initialized (happens during `split_link_init()`).

## Documentation

- [Firmware Guide](docs/firmware_guide.md) — comprehensive guide covering wiring, building, BLE, ESP-NOW, WiFi UDP, deep sleep, adding MCUs, keymap customization, and troubleshooting.
- [Pairing Guide](docs/pairing-guide.md) — keyboard shortcuts for BLE and split link pairing, LED status indicators, and troubleshooting.