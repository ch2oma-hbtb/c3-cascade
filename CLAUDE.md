# Split Keyboard Project

Wireless split keyboard with master/slave architecture.

## Architecture

- **Controllers**: Seeed Studio XIAO series (interchangeable per build)
  - ESP32-C3 XIAO (BLE 5, low cost)
  - ESP32-C6 XIAO (BLE 5, WiFi 6)
  - nRF52840 XIAO (BLE 5, best wireless range)
- **Communication**: Wireless (Bluetooth) between halves
- **Topology**: Master/slave — one half runs as central, the other as peripheral
- **Auto-discovery**: No hardcoded MAC addresses — slaves broadcast DISCOVER, master responds with ACK
- **Pairing mode**: ESC+SPACE (5s) for BLE PC pairing, ESC+SHIFT (5s) for slave search; all keys suppressed except ESC/SHIFT/SPACE during pairing; 60s timeout
- **LED status**: Pico W onboard LED blinks during pairing (master) or searching (slave), solid on when connected

## Project Structure

```
hardware/
  left-half/        # KiCad project for left PCB (29 switches, Cherry MX 1u)
  right-half/       # Right half — Fusion 360 API script + KiCad project (TBD)
    fusion360_right_half.py
  lib/              # Shared footprint & 3D model libraries
    keyswitch-kicad-library/
firmware/           # Keyboard firmware
  include/          # config.h, pins.h, keymap.h, hal/*.h
  src/              # main.cpp, matrix, ble_hid, split_link, power, hal/
gerber/             # Generated gerber output
docs/               # Design documentation
  pairing-guide.md  # BLE and split link pairing procedures
  firmware_guide.md # Comprehensive firmware documentation
```

## Key Design Decisions

- Both halves use **identical PCBs** — the same board flipped in assembly
- No physical cable between halves (wireless only)
- Battery powered (design should include battery pad/connector footprints)
- XIAO controllers use castellated pads or header sockets
- Switch diodes: BAT85 (Schottky, currently in schematic)

## Hardware Notes

- 29 Cherry MX switches per half, staggered column layout (5 rows)
- Board outline: ~147mm x ~100mm with angled left edge and notched right side
- Default KiCad design rules: 0.2mm clearance, 0.2mm track width, 0.6mm via diameter
- Gerber output path: `../gerber/`
- Right half designed in Fusion 360 via FusionMCPBridge (board outline + switch plate + bottom case)
- Fusion 360 document has: switch plate (1.5mm, 29 cutouts), bottom case (3mm walls, shelled)
- Still needed: XIAO controller cutout, battery compartment, mounting screw holes

## Switch Layout (Left Half)

```
Row 0: SW1  SW5  SW6  SW7  SW8  SW9  SW10     (y ~31.5mm)
Row 1: SW2  SW15 SW16 SW17 SW18 SW19           (y ~50.5mm)
Row 2: SW3  SW20 SW21 SW22 SW23 SW24           (y ~69.5mm)
Row 3: SW4  SW25 SW26 SW27 SW28 SW29           (y ~88.5mm)
Row 4: SW11 SW12 SW13 SW14                     (y ~107.5mm, thumb cluster)
```