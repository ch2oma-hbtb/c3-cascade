# C3-Cascade — Agent Instructions

## Project Overview

Wireless split keyboard firmware: ESP-NOW inter-half communication, BLE HID to host, deep sleep power management. Targets Seeed Studio XIAO boards (ESP32-C3 primary, ESP32-C6 and nRF52840 as ports).

## Build & Flash

```bash
# Build (PlatformIO)
pio run -e xiao_esp32c3          # Master (left half)
pio run -e xiao_esp32c3_slave    # Slave (right half)
pio run -e xiao_esp32c6          # C6 master (experimental)

# Flash
pio run -e xiao_esp32c3 -t upload

# Serial monitor
pio device monitor -b 115200
```

## Architecture

- **Master (left half):** Scans local matrix → receives slave matrix via ESP-NOW → merges → resolves keycodes → sends BLE HID report
- **Slave (right half):** Scans local matrix → sends MATRIX_REPORT to master on change → processes heartbeats from master
- **HAL abstraction:** `namespace hal` in `include/hal/hal.h` provides GPIO/timing/power/system API; MCU-specific implementations in `src/hal/hal_esp32.cpp` (complete) and `src/hal/hal_nrf52.cpp` (skeleton/stubs)

### Data Flow (per loop)

1. `matrix_scan()` — 1kHz, 5ms debounce
2. `split_link_update()` — ESP-NOW receive/send
3. `build_hid_report()` — merge matrices, resolve keycodes, fill 6KRO report
4. `ble_hid_send_report()` — 8ms rate limit
5. `power_check_sleep()` — deep sleep after 60s idle

## Key Conventions

- **Row-driven matrix:** ROW pins driven LOW one at a time; COL pins read as INPUT_PULLUP. Diodes: 1N4148, cathode towards column.
- **Modifier encoding:** `MOD_KEY(mod)` = `0xF000 | mod`, `IS_MOD_KEY(code)` checks upper byte, `GET_MOD_BIT(code)` extracts bit. Defined in `keymap.h`.
- **Matrix state:** `matrix_state_t` = `uint8_t rows[5]`, each byte is a row, bits are columns.
- **ESP-NOW packets:** `split_packet_t` (~11 bytes). Types: MATRIX_REPORT (0x01), HEARTBEAT (0x02), SYNC (0x03), ACK (0x04).
- **Feature flags** in `config.h`: `ENABLE_BLE_HID`, `ENABLE_SPLIT_LINK`, `ENABLE_DEEPSLEEP`, `ENABLE_BATTERY_REPORT` (off), `ENABLE_RGB` (off).
- **Board-specific pins** in `pins.h` — always use these macros, never raw GPIO numbers.

## Pitfalls & TODOs

- **MAC addresses are placeholders:** `MASTER_MAC`, `SLAVE_MAC_1`, `SLAVE_MAC_2` in `config.h` are `{0xFF,...}` — must be set to actual board MACs before split link works.
- **ESP32-C6 deep sleep:** Only GPIO 0,1,2 are LP-capable; columns 2–5 (GPIO 21–23) cannot wake from deep sleep.
- **nRF52840 is skeleton:** HAL, BLE, and split link are all stubs. `enter_deep_sleep()` halts, `woke_from_deep_sleep()` returns false.
- **Layer 1 (FN) is empty:** All keys are `HID_KEY_NONE` — needs key assignments.
- **Slave uses master keymap:** `build_hid_report()` has TODO for `KEYMAP_SECONDARY`.
- **BLE PnP IDs are placeholders:** Vendor `0x05AC` (Apple), Product `0x820A` — should be changed.

## Documentation

- [Firmware Guide](docs/firmware_guide.md) — comprehensive guide covering wiring, building, BLE, ESP-NOW, deep sleep, adding MCUs, keymap customization, and troubleshooting.