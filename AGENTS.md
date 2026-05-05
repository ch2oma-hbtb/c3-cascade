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
- **HAL abstraction:** `namespace hal` in `include/hal/hal.h` provides GPIO/timing/power/system API; MCU-specific implementations in `src/hal/hal_esp32.cpp`, `src/hal/hal_rp2040.cpp`, `src/hal/hal_nrf52.cpp` (skeleton)
- **Split transports:** ESP-NOW (`split_link.cpp`) for ESP32 boards, WiFi UDP (`split_link_wifi_udp.cpp`) for Pico W and cross-platform setups
- **Auto-discovery:** Both transports use a DISCOVER/DISCOVER_ACK handshake — no hardcoded MAC addresses needed

### Split Transport Selection

| Board | Default Transport | Override |
|-------|-------------------|----------|
| ESP32-C3/C6 | ESP-NOW | Add `-DFORCE_WIFI_UDP=1` for WiFi UDP |
| Pico 2W | WiFi UDP | (only transport available) |
| nRF52840 | None (stub) | — |

### Auto-Discovery Flow

1. **Master** boots → creates SoftAP (WiFi UDP) or listens for broadcasts (ESP-NOW)
2. **Slave** boots → scans/broadcasts DISCOVER packets
3. **Master** receives DISCOVER → registers slave → replies DISCOVER_ACK with assigned ID
4. **Slave** receives ACK → paired, begins sending MATRIX_REPORT via unicast

### Data Flow (per loop)

1. `matrix_scan()` — 1kHz, 5ms debounce
2. `split_link_update()` — auto-discovery + receive/send (ESP-NOW or WiFi UDP)
3. `build_hid_report()` — merge matrices, resolve keycodes, fill 6KRO report
4. `ble_hid_send_report()` — 8ms rate limit
5. `power_check_sleep()` — deep sleep after 60s idle

## Key Conventions

- **Row-driven matrix:** ROW pins driven LOW one at a time; COL pins read as INPUT_PULLUP. Diodes: 1N4148, cathode towards column.
- **Modifier encoding:** `MOD_KEY(mod)` = `0xF000 | mod`, `IS_MOD_KEY(code)` checks upper byte, `GET_MOD_BIT(code)` extracts bit. Defined in `keymap.h`.
- **Matrix state:** `matrix_state_t` = `uint8_t rows[5]`, each byte is a row, bits are columns.
- **Split packets:** `split_packet_t` (~18 bytes). Types: MATRIX_REPORT (0x01), HEARTBEAT (0x02), SYNC (0x03), ACK (0x04), DISCOVER (0x10), DISCOVER_ACK (0x11).
- **Feature flags** in `config.h`: `ENABLE_BLE_HID`, `ENABLE_SPLIT_LINK`, `ENABLE_DEEPSLEEP`, `ENABLE_BATTERY_REPORT` (off), `ENABLE_RGB` (off).
- **Transport flags** in `config.h`: `SPLIT_TRANSPORT_ESPNOW`, `SPLIT_TRANSPORT_WIFI_UDP` — auto-detected from board type.
- **Board-specific pins** in `pins.h` — always use these macros, never raw GPIO numbers.

## Pitfalls & TODOs

- **ESP32-C6 deep sleep:** Only GPIO 0,1,2 are LP-capable; columns 2–5 (GPIO 21–23) cannot wake from deep sleep.
- **nRF52840 is skeleton:** HAL, BLE, and split link are all stubs. `enter_deep_sleep()` halts, `woke_from_deep_sleep()` returns false.
- **Pico 2W deep sleep:** Uses WFI + watchdog reboot (light sleep ~2mA). Full dormant mode (~150µA) is a TODO.
- **Pico 2W BLE HID:** Uses `PicoBluetoothBLEHID` from arduino-pico core — may need API adjustments per core version.
- **Layer 1 (FN) is empty:** All keys are `HID_KEY_NONE` — needs key assignments.
- **BLE PnP IDs are placeholders:** Vendor `0x05AC` (Apple), Product `0x820A` — should be changed.
- **Pico 2W pins are defaults:** GP2–GP13 — update in `pins.h` to match actual wiring.

## Documentation

- [Firmware Guide](docs/firmware_guide.md) — comprehensive guide covering wiring, building, BLE, ESP-NOW, WiFi UDP, deep sleep, adding MCUs, keymap customization, and troubleshooting.