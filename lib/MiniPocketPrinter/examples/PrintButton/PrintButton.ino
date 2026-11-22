// Mini Pocket Printer + Heltec WiFi LoRa 32 V4: OLED status and a print button.
//
// PRG button (GPIO 0):
//   short press   print the test page
//   hold > 1 s    feed paper 10 mm
// The OLED shows connection state, printer model/firmware/battery and the last result.
// Serial Monitor (115200) accepts: t | p <text> | f | b | d <0-2> | L [rows]
//
// Libraries: NimBLE-Arduino 2.x, Adafruit GFX, QRCode (ricmoo), U8g2

#include <MiniPocketPrinter.h>
#include <U8g2lib.h>
#include <qrcode.h>

// Heltec V4 onboard 0.96" SSD1306 OLED and PRG button
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define VEXT_CTRL 36  // LOW = OLED power on
#define BUTTON_PIN 0  // PRG, active low

// Leave nullptr to find the printer by name (BLE MAC of the test unit: 5E:55:XX:XX:XX:XX).
static const char* PRINTER_MAC = nullptr;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);
MiniPocketPrinter printer;

static String stateLine = "Starting...";
static String infoLine = "";
static String resultLine = "";
static int battery = -1;

enum ButtonEvent { kNone, kShort, kLong };

// ---------- OLED ----------

static void drawScreen() {
  display.clearBuffer();
  display.setFont(u8g2_font_helvB10_tr);
  display.drawStr(0, 12, "Hack Printer");
  display.drawHLine(0, 15, 128);

  display.setFont(u8g2_font_6x10_tr);
  display.drawStr(0, 27, stateLine.c_str());
  display.drawStr(0, 38, infoLine.c_str());
  display.drawStr(0, 49, resultLine.c_str());

  // battery in the top-right corner
  if (battery >= 0) {
    char b[6];
    snprintf(b, sizeof(b), "%d%%", battery);
    display.drawStr(128 - display.getStrWidth(b), 10, b);
  }

  display.drawStr(0, 63, printer.isConnected() ? "PRG:print  hold:feed" : "PRG: connect + print");
  display.sendBuffer();
}

static void setState(const String& s) {
  stateLine = s;
  Serial.println(s);
  drawScreen();
}

static void setResult(const String& s) {
  resultLine = s;
  Serial.println(s);
  drawScreen();
}

// ---------- printer ----------

static bool connectPrinter() {
  if (printer.isConnected()) return true;
  setState("Scanning...");
  if (!printer.begin(PRINTER_MAC)) {
    setState("Printer not found");
    setResult("Turn it on, close app");
    return false;
  }
  battery = printer.batteryPercent();
  infoLine = printer.model() + "  " + printer.firmware();
  setState("Connected");
  return true;
}

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
  if (!c.getBuffer()) return false;
  c.fillScreen(0);
  c.setTextColor(1);
  c.drawRect(0, 0, W, H, 1);

  c.setTextSize(3);
  c.setCursor(12, 12);
  c.print("ESP32 PRINT");

  c.setTextSize(1);
  c.setCursor(12, 50);
  c.printf("Model %s", infoLine.c_str());
  c.setCursor(12, 62);
  c.printf("Battery %d%%   width %d dots", battery, W);
  c.setTextSize(2);
  c.setCursor(12, 82);
  c.print("Hello from the");
  c.setCursor(12, 102);
  c.print("Heltec V4!");

  for (int16_t y = 130; y < 162; y += 8)
    for (int16_t x = 8; x < W - 8; x += 8)
      if (((x / 8) + (y / 8)) % 2 == 0) c.fillRect(x, y, 8, 8, 1);

  for (int16_t i = 0; i < 6; i++) c.drawFastHLine(8, 172 + i * (i + 1), W - 16, 1);

  const uint8_t scale = 6;
  drawQr(c, "https://github.com/h2zero/NimBLE-Arduino", (W - 29 * scale) / 2, 220, scale);

  c.setTextSize(1);
  c.setCursor(12, H - 14);
  c.print("Mini Pocket Printer / LuckPrinter A2Y");

  return printer.printCanvas(c) && printer.feed(80);
}

static void runJob(const char* label, bool (*job)()) {
  if (!connectPrinter()) return;
  setResult(String(label) + "...");
  bool ok = job();
  battery = printer.batteryPercent();
  setResult(ok ? String(label) + " done" : String("Err: ") + printer.lastError());
}

static bool jobTestPage() { return printTestPage(); }
static bool jobFeed() { return printer.feed(80); }

// ---------- button ----------

static ButtonEvent pollButton() {
  static bool wasDown = false;
  static uint32_t downAt = 0;
  static bool longFired = false;
  bool down = digitalRead(BUTTON_PIN) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) {
    downAt = now;
    longFired = false;
  }
  ButtonEvent ev = kNone;
  if (down && !longFired && now - downAt > 1000) {
    longFired = true;
    ev = kLong;
  }
  if (!down && wasDown && !longFired && now - downAt > 30) ev = kShort;  // 30 ms debounce
  wasDown = down;
  return ev;
}

// ---------- main ----------

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(100);
  display.begin();
  drawScreen();

  printer.setDensity(1);
  connectPrinter();
  if (printer.isConnected()) setResult("Ready");
}

void loop() {
  switch (pollButton()) {
    case kShort: runJob("Printing", jobTestPage); break;
    case kLong: runJob("Feeding", jobFeed); break;
    default: break;
  }

  // Serial commands
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') {
      line += ch;
      continue;
    }
    line.trim();
    if (line == "t") runJob("Printing", jobTestPage);
    else if (line == "f") runJob("Feeding", jobFeed);
    else if (line == "b") {
      battery = printer.batteryPercent();
      Serial.printf("Battery %d%%, status 0x%02x\n", battery, printer.status());
      drawScreen();
    } else if (line.startsWith("d ")) {
      // d <0-2>: set density for following jobs
      printer.setDensity(line.substring(2).toInt());
      Serial.printf("Density set to %d\n", (int)line.substring(2).toInt());
    } else if (line.startsWith("L")) {
      // L [rows]: long print test, numbered bands to spot dropped or garbled data
      int rows = line.length() > 2 ? line.substring(2).toInt() : 1600;
      if (connectPrinter()) {
        GFXcanvas1 c(MiniPocketPrinter::kWidth, rows);
        if (!c.getBuffer()) {
          setResult("Err: no memory");
        } else {
          c.fillScreen(0);
          c.setTextColor(1);
          c.setTextSize(2);
          c.drawFastVLine(0, 0, rows, 1);
          c.drawFastVLine(MiniPocketPrinter::kWidth - 1, 0, rows, 1);
          for (int y = 0; y < rows; y += 40) {
            c.drawFastHLine(0, y, MiniPocketPrinter::kWidth, 1);
            c.setCursor(10, y + 14);
            c.printf("row %d", y);
            c.fillRect(200, y + 8, 8 + (y / 40) % 20 * 8, 24, 1);
          }
          setResult("Long print...");
          uint32_t t0 = millis();
          bool ok = printer.printCanvas(c) && printer.feed(80);
          Serial.printf("Long print %d rows (%d bytes) took %lu ms\n", rows, rows * 48, millis() - t0);
          setResult(ok ? "Long print done" : String("Err: ") + printer.lastError());
        }
      }
    } else if (line.startsWith("p ")) {
      if (connectPrinter()) {
        setResult("Printing text...");
        bool ok = printer.printText(line.substring(2).c_str(), 2) && printer.feed(80);
        setResult(ok ? "Text done" : String("Err: ") + printer.lastError());
      }
    }
    line = "";
  }

  // Notice a dropped link and refresh the battery once a minute.
  static bool wasConnected = false;
  static uint32_t lastBatt = 0;
  bool connected = printer.isConnected();
  if (connected != wasConnected) {
    wasConnected = connected;
    setState(connected ? "Connected" : "Disconnected");
  }
  if (connected && millis() - lastBatt > 60000) {
    lastBatt = millis();
    battery = printer.batteryPercent();
    drawScreen();
  }
  delay(10);
}
