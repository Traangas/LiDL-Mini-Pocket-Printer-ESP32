# CLAUDE.md

Guidance for Claude (and other coding agents) helping someone print from an ESP32 to the
Lidl TRONIC Mini Pocket Printer with this repo.

## What this repo is

- `PROTOCOL.md`: the confirmed BLE protocol. Read it before changing any byte sequence.
- `lib/MiniPocketPrinter/`: the Arduino/PlatformIO library (`MiniPocketPrinter.h` is the API).
  - `LuckProtocol.h`: command bytes only. `BleTransport.*`: NimBLE connect/write/notify only. Keep them separate.
- `lib/MiniPocketPrinter/examples/PrintButton`: Heltec WiFi LoRa 32 V4 (ESP32-S3) sketch with OLED + PRG button.
  This is what `platformio.ini` builds.
- `lib/MiniPocketPrinter/examples/TestPage`: board-independent sketch (any ESP32 with BLE).
- `tools/`: Python + Bleak scripts for probing the printer from a computer.

## Start here

1. Ask which ESP32 board the user has. Any BLE-capable ESP32 works (classic, S3, C3, C6).
   For a board other than the Heltec V4, change `board`, `board_build.*`, `build_flags` and `upload_port`
   in `platformio.ini`, and build `examples/TestPage` (set `src_dir`) unless they want the OLED/button sketch.
2. Have the user switch the printer on and **close the phone app** (the printer accepts one BLE connection).
3. `pio run -t upload`, then read serial at 115200. Expect `Connected`, then `Ready`.
4. To use the printer in their own project, follow "Using the library in another project" in `README.md`.

## Confirmed facts (do not re-derive)

- Write to characteristic `2af1` (service `18f0`), replies arrive on `2af0`.
- Print job: status `10 FF 40` → enable `10 FF F1 03` → 12 × `00` → `1D 76 30 00 30 00 hL hH` + rows
  → feed `1B 4A n` → finalize `10 FF F1 45` (reply `OK` or `AA`).
- 384 dots per line (48 bytes), MSB-first, 1 = black.
- The model reports `A2Y`, firmware `V1.06LY`. Protocol = LuckPrinter "normal" (TiMini-Print `luck_a2`).

## Gotchas that cost hours the first time

- **AiYin commands do not print.** `10 FF FE 01` / `10 FF FE 45` (from the Fichero docs) are accepted silently but
  nothing prints. Use the Luck pair `F1 03` / `F1 45`.
- **ESC/POS, cat-printer (`51 78 …`) and NIIMBOT (`55 55 …`) commands do nothing.** The extra services
  (`ff00`, `e7810a71…`, ISSC) come from the Bluetooth module and are red herrings.
- **Paper latch not fully closed** = printer ignores all queries and silently buffers print data, then prints it
  when the latch is reopened and closed. No reply to `10 FF 40` means: ask the user to check the latch.
- **Status `0x01` (busy) is normal briefly** after a density change or a job; the library waits up to 5 s.
- **The MAC in the self-test QR code (`55:55:…`) is the Bluetooth Classic MAC.** The BLE MAC starts with `5E:55:…`.
  Prefer matching by name (`Mini Pocket Printer`), which is the library default.
- **macOS + Python tools:** run `tools/*.py` from the user's own Terminal.app. Launched from an agent's shell,
  Python usually lacks Bluetooth permission and dies with exit code 134 (SIGABRT) and no output.
- **ESP32 BLE stack:** the library uses NimBLE-Arduino 2.x. Do not mix with the Arduino `BLEDevice` (Bluedroid) library.
- **Arduino `.ino` preprocessor:** declare enums/types at the top of a sketch, before any function, or the generated
  prototypes fail to compile.

## If the printer behaves differently

Only one unit (firmware `V1.06LY`) has been tested. If a user's printer does not print:

1. Run `.venv/bin/python tools/luck_test.py --skip-probe --dialect luck` from Terminal (create the venv and
   `pip install bleak` first). It queries model/firmware/status and prints a test pattern.
2. Compare the model and firmware replies with `PROTOCOL.md`. Different values may mean a different command dialect.
3. `tools/gatt_probe.py` dumps the GATT table and lets you send raw hex interactively.
4. Record anything newly confirmed in `PROTOCOL.md`, marked **confirmed** vs *hypothesis*.

## Working style for this repo

- Mark claims in docs as confirmed on hardware or not; never present untested behaviour as fact.
- Ask the user to report the serial output and what came out on paper. You cannot see the printout.
- Do not commit personal device identifiers (serial numbers, full MAC addresses); mask them as `XX`.
