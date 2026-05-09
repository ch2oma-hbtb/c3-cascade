# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

PlatformIO project under `firmware/`. All commands run from `firmware/`.

```bash
# Build
pio run -e xiao_esp32c3          # Master (left) — ESP32-C3
pio run -e xiao_esp32c3_slave    # Slave (right) — ESP32-C3
pio run -e xiao_esp32c6          # C6 master
pio run -e pico2w                 # Master — Pico 2W (RP2350)
pio run -e pico2w_slave           # Slave — Pico 2W
pio run -e pico_w                 # Master — Pico W (RP2040)
pio run -e pico_w_slave           # Slave — Pico W

# Flash
pio run -e xiao_esp32c3 -t upload

# Serial monitor
pio device monitor -b 115200
```

No test suite exists. Debug output uses `Serial.printf` via `DBG_PRINT` macros controlled by `DEBUG_SERIAL` / `DEBUG_MATRIX` / `DEBUG_BLE` / `DEBUG_ESPNOW` flags in `config.h`.

## Architecture

Wireless split keyboard. Master (left half) scans its matrix, receives slave matrix over wireless, merges both, and sends a BLE HID report to the host. Slave (right half) scans its matrix and sends changes to the master.

### HAL abstraction

`namespace hal` in `include/hal/hal.h` provides GPIO/timing/power/LED API. MCU-specific implementations in `src/hal/hal_esp32.cpp`, `src/hal/hal_rp2040.cpp`, `src/hal/hal_nrf52.cpp` (skeleton). Board selection via PlatformIO build flags (`-DBOARD_XIAO_ESP32C3`, `-DBOARD_PICO2_W`, etc.) determines which HAL is compiled.

### Split transport

`split_link.cpp` handles inter-half communication. Two transports, auto-selected by board capabilities:

| Board | Master | Slave |
|-------|--------|-------|
| ESP32-C3/C6 | Dual mode (ESP-NOW + WiFi UDP) | ESP-NOW |
| Pico W / Pico 2W | WiFi UDP | WiFi UDP |
| nRF52840 | None (stub) | None |

ESP32 masters support dual mode — they can accept both an ESP32-C3 slave (via ESP-NOW) and a Pico W slave (via WiFi UDP) simultaneously. Transport flags `SPLIT_TRANSPORT_ESPNOW` and `SPLIT_TRANSPORT_WIFI_UDP` are auto-detected in `config.h` based on `HAS_ESPNOW` / `HAS_WIFI`, overridable with `-DFORCE_WIFI_UDP`.

### Auto-discovery

No hardcoded MAC addresses. Slave broadcasts DISCOVER, master responds with DISCOVER_ACK including an assigned ID. Handshake is identical across transports.

### Main loop flow

1. `matrix_scan()` — 1kHz, 5ms debounce
2. `check_pairing_shortcuts()` — ESC+SPACE (5s) for BLE PC pairing, ESC+SHIFT (5s) for slave search (master); 7+SPACE (5s) on slave
3. `split_link_update()` — auto-discovery + send/receive
4. `build_hid_report()` — merge matrices, resolve keycodes from keymap layers, fill 6KRO report (keys suppressed during pairing)
5. `ble_hid_send_report()` — 8ms rate limit
6. `update_led()` — blink during pairing/search, solid when connected
7. `power_check_sleep()` — deep sleep after 60s idle (inhibited during pairing/discovery)

### Key conventions

- **Row-driven matrix:** ROW pins LOW one at a time; COL pins INPUT_PULLUP. Diodes cathode towards column.
- **Modifier encoding:** `MOD_KEY(mod)` = `0xF000 | mod`, checked with `IS_MOD_KEY(code)`, extracted with `GET_MOD_BIT(code)` — defined in `keymap.h`.
- **Matrix state:** `matrix_state_t` = `uint8_t rows[5]`, bit-per-column.
- **Keymap layers:** `KEYMAP_LAYERS_PRIMARY[]` for left half, `KEYMAP_LAYERS_SECONDARY[]` for right half. Layer 0 = base, Layer 1 = FN (currently sparse). `HID_KEY_FN` (0xF1) triggers layer switch.
- **Board pins:** Always use `pins.h` macros (`ROW_PINS[]`, `COL_PINS[]`, `LED_PIN`), never raw GPIO numbers.
- **Feature flags:** `ENABLE_BLE_HID`, `ENABLE_SPLIT_LINK`, `ENABLE_DEEPSLEEP`, `ENABLE_LED` in `config.h`.
- **Build flags define role:** `-DSPLIT_ROLE_MASTER` or `-DSPLIT_ROLE_SLAVE` — `IS_MASTER` / `IS_SLAVE` macros control conditional compilation throughout.

### Split packets

`split_packet_t` (~18 bytes). Types: `MATRIX_REPORT` (0x01), `HEARTBEAT` (0x02), `SYNC` (0x03), `ACK` (0x04), `DISCOVER` (0x10), `DISCOVER_ACK` (0x11).

## Known issues & TODOs

- **ESP32-C6 deep sleep:** Only GPIO 0-2 are LP-capable; columns 2-5 (GPIO 21-23) cannot wake from deep sleep.
- **nRF52840 is skeleton:** HAL, BLE, and split link are stubs.
- **Pico 2W deep sleep:** Uses WFI + watchdog reboot (~2mA). Full dormant mode (~150µA) is a TODO.
- **Pico 2W BLE HID:** Uses `PicoBluetoothBLEHID` from arduino-pico core — pairing mode not supported on BTstack.
- **FN layer (Layer 1) is mostly empty** — needs key assignments.
- **BLE PnP IDs are placeholders:** Vendor `0x05AC` (Apple), Product `0x820A`.
- **XIAO LED:** WS2812 RGB LED, not simple on/off — `LED_PIN=0xFF` makes LED functions no-ops on XIAO boards.
- **Pico W LED requires WiFi init:** CYW43 LED only works after `split_link_init()` initializes the WiFi chip.

## Hardware

- Both halves use identical PCBs — same board flipped in assembly
- 29 Cherry MX switches per half, staggered column layout (5 rows)
- Battery powered (footprints for battery pads/connectors)
- XIAO controllers use castellated pads or header sockets
- Switch diodes: BAT85 (Schottky)
- KiCad projects in `hardware/left-half/`, right half is TBD
- CAD reference in `cad/c3.f3d` (Fusion 360)
- Gerber output to `../gerber/`
- Layout text files at repo root: `layout - primary.txt`, `layout - slave.txt`