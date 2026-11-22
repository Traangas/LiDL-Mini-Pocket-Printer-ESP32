// Mini Pocket Printer test page
// Board: Heltec WiFi LoRa 32 V4 (ESP32-S3), or any ESP32 with BLE.
// Libraries: NimBLE-Arduino 2.x, Adafruit GFX, QRCode (ricmoo)
//
// Turn the printer on, close the phone app, open Serial Monitor at 115200.
// Commands over serial:
//   t           print the test page again
//   p <text>    print text
//   f           feed 10 mm
//   b           battery and status

#include <MiniPocketPrinter.h>
#include <qrcode.h>

// Leave nullptr to find the printer by name. The BLE address of the
// test printer is 5E:55:XX:XX:XX:XX (not the 55:55:... in the QR code).
static const char* PRINTER_MAC = nullptr;

MiniPocketPrinter printer;

static void drawQr(GFXcanvas1& c, const char* text, int16_t x0, int16_t y0, uint8_t scale) {
  QRCode qr;
  uint8_t data[qrcode_getBufferSize(3)];
  qrcode_initText(&qr, data, 3, ECC_LOW, text);
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) c.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, 1);
}

static bool printTestPage() {
  const int16_t W = MiniPocketPrinter::kWidth;
  const int16_t H = 440;
  GFXcanvas1 c(W, H);
  if (!c.getBuffer()) {
    Serial.println("Out of memory for canvas");
    return false;
  }
  c.fillScreen(0);
  c.setTextColor(1);

  // 1-pixel border
  c.drawRect(0, 0, W, H, 1);

  // Title
  c.setTextSize(3);
  c.setCursor(12, 12);
  c.print("ESP32 PRINT");

  // Body text
  c.setTextSize(1);
  c.setCursor(12, 50);
  c.printf("Model %s  FW %s\n", printer.model().c_str(), printer.firmware().c_str());
  c.setCursor(12, 62);
  c.printf("Battery %d%%   width %d dots\n", printer.batteryPercent(), W);
  c.setTextSize(2);
  c.setCursor(12, 82);
  c.print("Hello from the");
  c.setCursor(12, 102);
  c.print("Heltec V4!");

  // Checkerboard strip, 8x8 squares
  for (int16_t y = 130; y < 162; y += 8)
    for (int16_t x = 8; x < W - 8; x += 8)
      if (((x / 8) + (y / 8)) % 2 == 0) c.fillRect(x, y, 8, 8, 1);

  // Gray-ish ramp made of line spacing
  for (int16_t i = 0; i < 6; i++) c.drawFastHLine(8, 172 + i * (i + 1), W - 16, 1);

  // QR code, centred
  const uint8_t scale = 6;
  const int16_t qrSize = 29 * scale;  // version 3 = 29 modules
  drawQr(c, "https://github.com/h2zero/NimBLE-Arduino", (W - qrSize) / 2, 220, scale);

  c.setTextSize(1);
  c.setCursor(12, H - 14);
  c.print("Mini Pocket Printer / LuckPrinter A2Y");

  bool ok = printer.printCanvas(c);
  if (ok) printer.feed(80);
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Mini Pocket Printer test ===");

  printer.debug = false;
  printer.setDensity(1);
  Serial.println("Connecting to the printer...");
  if (!printer.begin(PRINTER_MAC)) {
    Serial.printf("Connect failed: %s\n", printer.lastError());
    return;
  }
  Serial.printf("Connected. Model %s, firmware %s, battery %d%%\n", printer.model().c_str(),
                printer.firmware().c_str(), printer.batteryPercent());

  Serial.println(printTestPage() ? "Test page sent." : printer.lastError());
  Serial.println("Commands: t | p <text> | f | b");
}

void loop() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') {
      line += ch;
      continue;
    }
    line.trim();
    if (line.isEmpty()) continue;
    bool ok = true;
    if (line == "t") ok = printTestPage();
    else if (line.startsWith("p ")) ok = printer.printText(line.substring(2).c_str(), 2) && printer.feed(80);
    else if (line == "f") ok = printer.feed(80);
    else if (line == "b") Serial.printf("Battery %d%%, status 0x%02x\n", printer.batteryPercent(), printer.status());
    else Serial.println("?");
    if (!ok) Serial.printf("Failed: %s\n", printer.lastError());
    line = "";
  }
  delay(10);
}
