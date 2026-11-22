# LiDL Mini Pocket Printer ESP32

ESP32 BLE driver (`MiniPocketPrinter` library) for the Lidl TRONIC **Mini Pocket Printer** (IAN 508705, model 5890),
a 57 mm BLE thermal printer. Internally it is a LuckPrinter **A2Y** (Bluetooth module likely by AiYin). No phone app
needed.

Protocol details: [PROTOCOL.md](PROTOCOL.md).

## Hardware

- Heltec WiFi LoRa 32 V4 (ESP32-S3), or any ESP32 with BLE (S3, C3, C6, classic).
- No wiring: BLE only.

## Build and flash (PlatformIO)

```bash
pio run -t upload
pio device monitor
```

`platformio.ini` targets the Heltec V4 on `/dev/cu.usbmodem1101` and builds
`lib/MiniPocketPrinter/examples/PrintButton/PrintButton.ino`:

- **OLED** shows connection state, model/firmware, battery %, and the result of the last job (or the error,
  e.g. "Err: cover open").
- **PRG button** (GPIO 0): short press prints the test page, hold for more than 1 s feeds 10 mm.
  If the printer is not connected, a press scans and connects first.
- **Serial** (115200): `t` test page, `p <text>` print text, `f` feed, `b` battery/status,
  `d <0-2>` density, `L [rows]` long-print test (default 1600 rows).

`examples/TestPage` is a board-independent version without OLED or button (any ESP32).

Dependencies (installed automatically by PlatformIO):
- `h2zero/NimBLE-Arduino` 2.x
- `adafruit/Adafruit GFX Library`
- `ricmoo/QRCode` (only for the example's QR code)
- `olikraus/U8g2` (only for the PrintButton example's OLED)

### Arduino IDE

Copy `lib/MiniPocketPrinter` into your `Arduino/libraries` folder, install the three libraries above from the
Library Manager, then open File → Examples → MiniPocketPrinter → TestPage.

## Usage

```cpp
#include <MiniPocketPrinter.h>

MiniPocketPrinter printer;

void setup() {
  printer.setDensity(1);                 // 0 light, 1 medium, 2 dark
  if (!printer.begin()) {                // finds "Mini Pocket Printer..." by name
    Serial.println(printer.lastError());
    return;
  }
  printer.printText("Hello world", 2);   // built-in 6x8 font, scaled x2, word-wrapped
  printer.feed(80);                      // 8 dots = 1 mm

  GFXcanvas1 c(MiniPocketPrinter::kWidth, 100);   // 384 dots wide
  c.drawCircle(192, 50, 40, 1);
  printer.printCanvas(c);
}
```

| Method | Notes |
|---|---|
| `begin(mac = nullptr, name = "Mini Pocket Printer")` | Scans and connects. Pass the BLE MAC to skip name matching. |
| `isConnected()` / `end()` | Reconnects automatically on the next call after a drop. |
| `setDensity(0..2)` | Sent once per connection before the next job. |
| `printBitmap(bits, w, h)` | 1 bpp, MSB-first, row-major, 1 = black. Rows are padded/cropped to 384 dots. |
| `printCanvas(GFXcanvas1&)` | Any Adafruit GFX drawing: fonts, lines, bitmaps, QR codes. |
| `printText(text, size)` | Word-wrapped text. |
| `feed(dots)` | Paper feed. |
| `batteryPercent()`, `status()`, `model()`, `firmware()` | Queries. |
| `lastError()` | Reason for the last failure (cover open, out of paper, …). |

### Setting the MAC

The BLE address is **not** the `55:55:…` address in the self-test QR code (that is the Bluetooth Classic MAC).
The BLE MAC is the same with the first byte changed, e.g. `5E:55:XX:XX:XX:XX`. It is also returned by the
all-info query (`10 FF 70`). Set `PRINTER_MAC` in the sketch, or leave it `nullptr` to match by name.

## Using the library in another project

### PlatformIO

Reference the library folder from the new project's `platformio.ini` (path relative to that project):

```ini
lib_deps =
  symlink://../mini-pocket-printer/lib/MiniPocketPrinter
  h2zero/NimBLE-Arduino@^2.1.0
  adafruit/Adafruit GFX Library@^1.11.9
```

`symlink://` uses the live folder, so fixes here reach every project. Use `file://` instead to copy a snapshot.
Without a local checkout: `git clone https://github.com/Traangas/LiDL-Mini-Pocket-Printer-ESP32.git` next to
your project and point `symlink://` at its `lib/MiniPocketPrinter`, or copy that folder into your project's `lib/`.

For an ESP32-S3 Heltec V4, copy the `board`, `board_build.*` and `build_flags` lines from this repo's
`platformio.ini`.

### Minimal sketch

```cpp
#include <MiniPocketPrinter.h>
MiniPocketPrinter printer;

void setup() {
  Serial.begin(115200);
  if (!printer.begin()) Serial.println(printer.lastError());
}

void printReading(float temp) {
  char buf[48];
  snprintf(buf, sizeof(buf), "Temp: %.1f C", temp);
  if (!printer.printText(buf, 2) || !printer.feed(80)) Serial.println(printer.lastError());
}
```

## Integration notes

- **Calls block.** `printText`/`printCanvas` return only after the printer acknowledges the job. Measured: the test
  page (440 rows) takes a few seconds, 1600 rows (76.8 KB) took 15.3 s (about 5 KB/s, about 105 rows/s).
  If your project must keep running meanwhile (display refresh, sensor timing, web server), print from a separate
  FreeRTOS task and do not call the printer from two tasks at once.
- **Memory.** A `GFXcanvas1` costs 48 bytes per printed row (384 dots / 8). 1000 rows = 48 KB. The Heltec V4 has
  PSRAM so large canvases are fine; on boards without PSRAM, print long content in several smaller canvases
  (each call is a separate job, with no gap between them on paper apart from feed).
- **Printable width** is 384 dots (`MiniPocketPrinter::kWidth`), about 48 mm at 203 dpi (8 dots per mm).
  Text size 1 is 6×8 dots: 64 characters per line; size 2: 32; size 3: 21.
- **BLE sharing.** The library uses NimBLE-Arduino and calls `NimBLEDevice::init("")` only if nothing has
  initialised NimBLE yet. If your project also uses BLE, init NimBLE yourself first (with your device name) and
  use NimBLE everywhere: the ESP32 Arduino `BLEDevice` (Bluedroid) library cannot run alongside it.
- **Wi-Fi + BLE** work together on the ESP32/ESP32-S3 (they share the radio), but expect slower prints while
  Wi-Fi is busy.
- **Reconnects** happen automatically on the next call after the printer turns off or drops the link; the call
  that reconnects takes a few extra seconds (scan + connect). `isConnected()` lets you show state.
- **Errors** are reported through the `bool` return value plus `lastError()`: `cover open`, `out of paper`,
  `battery low`, `print head overheated`, `printer not found or connect failed`, `no status reply (latch open
  or printer busy?)`.
- **Auto power-off.** Like most of these printers it probably switches itself off when idle (time not measured).
  A print call then fails with `printer not found…` until the printer is switched on again.

## Known limitations

- Only one BLE connection at a time: close the phone app (and any other ESP32 or the Python tools) first.
- If the paper latch is not fully closed the printer ignores queries and holds print data, then prints it when the
  latch is closed. `printBitmap` refuses to start when the status query gets no reply.
- The printer reports "busy" (status `0x01`) briefly after a density change or a job; the library waits up to 5 s.
- Image data is paced with a fixed 15 ms delay between 100-byte writes. Tested up to 1600 rows in one job.
  Increase `printer.transport().chunkDelayMs` if very long prints come out garbled.
- Density 0, 1 and 2 have each been used for a completed print job; how much darker each level looks still has to
  be judged on paper.
- No grayscale or compressed raster modes; 1-bit only.

## Tools (macOS/Linux, Python + Bleak)

Used to reverse-engineer the protocol. Run from Terminal.app (macOS needs Bluetooth permission).

- `tools/gatt_probe.py`: dump the GATT table and send raw hex interactively.
- `tools/luck_test.py --skip-probe --dialect luck`: print a test pattern from the computer.

## Credits

The protocol was identified with help from these open-source projects (no code copied):

- [Dejniel/TiMini-Print](https://github.com/Dejniel/TiMini-Print): LuckPrinter "normal" family and the `luck_a2`
  profile, which matched this printer.
- [0xMH/fichero-printer](https://github.com/0xMH/fichero-printer): AiYin / LuckPrinter command reference for the
  Karsten-imported Fichero printer, which shares this printer's BLE layout.
- [rbaron/catprinter](https://github.com/rbaron/catprinter) and
  [NaitLee/Cat-Printer](https://github.com/NaitLee/Cat-Printer): background on cheap BLE thermal printers.

## License

MIT, see [LICENSE](LICENSE). Not affiliated with Lidl, TRONIC, Karsten International or LuckPrinter.
