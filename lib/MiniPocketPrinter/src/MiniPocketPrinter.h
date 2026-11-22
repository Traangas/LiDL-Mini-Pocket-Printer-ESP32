// Driver for the Lidl TRONIC Mini Pocket Printer (LuckPrinter A2Y) over BLE.
//
//   MiniPocketPrinter printer;
//   printer.begin();                    // scan by name, connect
//   printer.printText("Hello world", 2);
//   printer.feed(60);
#pragma once
#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "BleTransport.h"
#include "LuckProtocol.h"

class MiniPocketPrinter {
 public:
  static const uint16_t kWidth = LuckProtocol::kWidthDots;  // 384 dots, 203 dpi, ~48 mm

  // mac: BLE address (5E:55:...), or nullptr to match by name prefix only.
  bool begin(const char* mac = nullptr, const char* name = "Mini Pocket Printer");
  bool isConnected();
  void end();

  // 0 = light, 1 = medium (default), 2 = dark. Sent at the start of every job.
  void setDensity(uint8_t level);

  // 1 bpp, MSB-first, row-major, 1 = black. Rows are padded/cropped to 384 dots.
  bool printBitmap(const uint8_t* bits, uint16_t widthPx, uint16_t heightPx);
  // Prints a whole GFXcanvas1 (e.g. one you drew a test page on).
  bool printCanvas(GFXcanvas1& canvas);
  // Renders word-wrapped text with the built-in 6x8 font scaled by size, then prints it.
  bool printText(const char* text, uint8_t size = 1);
  // Feed paper by n dots (8 dots = 1 mm).
  bool feed(uint16_t dots);

  int batteryPercent();       // -1 if unknown
  int status();               // raw status byte (see LuckProtocol::StatusBits), -1 if no reply
  String model();             // e.g. "A2Y"
  String firmware();          // e.g. "V1.06LY"

  // Human-readable reason for the last failure.
  const char* lastError() const { return lastError_; }

  // Extra post-print feed added after every bitmap/text job (dots). Default 0.
  uint16_t autoFeedDots = 0;
  // Print serial debug output (BLE traffic).
  bool debug = false;
  BleTransport& transport() { return ble_; }

 private:
  bool ensureConnected();
  bool sendRaw(const uint8_t* d, size_t n);
  size_t query(const uint8_t* cmd, size_t n, uint8_t* out, size_t cap, uint32_t timeoutMs = 1500);
  bool checkReady();
  bool startJob();
  bool endJob(uint32_t rows);
  String queryString(const uint8_t* cmd, size_t n);

  BleTransport ble_;
  String mac_, name_;
  uint8_t density_ = 1;
  bool densityDirty_ = true;
  const char* lastError_ = "";
};
