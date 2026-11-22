#include "MiniPocketPrinter.h"

using namespace LuckProtocol;

static const BleTransport::Endpoint kEndpoint = {
    "000018f0-0000-1000-8000-00805f9b34fb",  // service
    "00002af1-0000-1000-8000-00805f9b34fb",  // write
    "00002af0-0000-1000-8000-00805f9b34fb",  // notify
};

bool MiniPocketPrinter::begin(const char* mac, const char* name) {
  mac_ = mac ? mac : "";
  name_ = name ? name : "";
  return ensureConnected();
}

bool MiniPocketPrinter::isConnected() { return ble_.isConnected(); }

void MiniPocketPrinter::end() { ble_.disconnect(); }

bool MiniPocketPrinter::ensureConnected() {
  if (ble_.isConnected()) return true;
  ble_.debug = debug;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (ble_.connect(mac_.c_str(), name_.c_str(), kEndpoint)) {
      densityDirty_ = true;
      return true;
    }
    delay(500);
  }
  lastError_ = "printer not found or connect failed";
  return false;
}

bool MiniPocketPrinter::sendRaw(const uint8_t* d, size_t n) {
  if (!ble_.write(d, n)) {
    lastError_ = "BLE write failed";
    return false;
  }
  return true;
}

size_t MiniPocketPrinter::query(const uint8_t* cmd, size_t n, uint8_t* out, size_t cap, uint32_t timeoutMs) {
  if (!ensureConnected()) return 0;
  ble_.clearReplies();
  if (!sendRaw(cmd, n)) return 0;
  return ble_.waitReply(out, cap, timeoutMs);
}

String MiniPocketPrinter::queryString(const uint8_t* cmd, size_t n) {
  uint8_t buf[64];
  size_t len = query(cmd, n, buf, sizeof(buf) - 1);
  buf[len] = 0;
  return String((const char*)buf);
}

void MiniPocketPrinter::setDensity(uint8_t level) {
  density_ = level > 2 ? 2 : level;
  densityDirty_ = true;
}

int MiniPocketPrinter::status() {
  uint8_t r[8];
  size_t n = query(kGetStatus, sizeof(kGetStatus), r, sizeof(r));
  return n ? r[0] : -1;
}

int MiniPocketPrinter::batteryPercent() {
  uint8_t r[8];
  size_t n = query(kGetBattery, sizeof(kGetBattery), r, sizeof(r), 1500);
  return n >= 2 ? r[1] : -1;
}

String MiniPocketPrinter::model() { return queryString(kGetModel, sizeof(kGetModel)); }
String MiniPocketPrinter::firmware() { return queryString(kGetFirmware, sizeof(kGetFirmware)); }

bool MiniPocketPrinter::checkReady() {
  int s = status();
  // The printer reports "busy" briefly after a setting change or a previous job: wait it out.
  uint32_t t0 = millis();
  while (s >= 0 && (s & kStatusErrorMask) == kStatusPrinting && millis() - t0 < 5000) {
    delay(250);
    s = status();
  }
  if (s < 0) {
    // A printer with the latch not fully closed ignores queries and buffers jobs.
    lastError_ = "no status reply (latch open or printer busy?)";
    return false;
  }
  if (s & kStatusErrorMask) {
    if (s & kStatusCoverOpen) lastError_ = "cover open";
    else if (s & kStatusNoPaper) lastError_ = "out of paper";
    else if (s & kStatusLowBattery) lastError_ = "battery low";
    else if (s & (kStatusOverheat | kStatusOverheatAlt)) lastError_ = "print head overheated";
    else lastError_ = "printer busy";
    return false;
  }
  return true;
}

bool MiniPocketPrinter::startJob() {
  if (!ensureConnected()) return false;
  if (densityDirty_) {
    uint8_t cmd[5], r[8];
    size_t n = LuckProtocol::setDensity(cmd, density_);
    query(cmd, n, r, sizeof(r), 1000);  // replies "OK"; not fatal if missing
    densityDirty_ = false;
  }
  if (!checkReady()) return false;
  ble_.clearReplies();
  return sendRaw(kEnable, sizeof(kEnable)) && sendRaw(kWakeup, sizeof(kWakeup));
}

bool MiniPocketPrinter::endJob(uint32_t rows) {
  uint8_t cmd[3];
  if (autoFeedDots) {
    uint16_t left = autoFeedDots;
    while (left) {
      uint8_t n = left > 255 ? 255 : left;
      if (!sendRaw(cmd, feedDots(cmd, n))) return false;
      left -= n;
    }
  }
  ble_.clearReplies();
  if (!sendRaw(kFinalize, sizeof(kFinalize))) return false;
  // Printer answers "OK" or 0xAA when done. Allow ~10 ms per row plus margin.
  uint8_t r[16];
  size_t n = ble_.waitReply(r, sizeof(r), 5000 + rows * 10);
  if (n == 0) {
    lastError_ = "no finalize reply (print may still have succeeded)";
    return true;
  }
  return true;
}

bool MiniPocketPrinter::printBitmap(const uint8_t* bits, uint16_t widthPx, uint16_t heightPx) {
  if (!bits || !widthPx || !heightPx) return false;
  if (!startJob()) return false;

  uint8_t hdr[8];
  if (!sendRaw(hdr, rasterHeader(hdr, kWidthBytes, heightPx))) return false;

  const uint16_t srcStride = (widthPx + 7) / 8;
  if (widthPx == kWidth) {
    // Stream straight from the caller's buffer.
    if (!sendRaw(bits, (size_t)srcStride * heightPx)) return false;
  } else {
    // Pad or crop each row to 48 bytes; send a few rows per write.
    const uint16_t rowsPerBlock = 8;
    uint8_t block[kWidthBytes * rowsPerBlock];
    uint16_t copy = srcStride < kWidthBytes ? srcStride : kWidthBytes;
    for (uint16_t y = 0; y < heightPx; y += rowsPerBlock) {
      uint16_t rows = (heightPx - y) < rowsPerBlock ? (heightPx - y) : rowsPerBlock;
      memset(block, 0, sizeof(block));
      for (uint16_t r = 0; r < rows; r++) {
        uint8_t* dst = block + r * kWidthBytes;
        memcpy(dst, bits + (size_t)(y + r) * srcStride, copy);
        // clear stray bits past widthPx in the last partial byte
        if (widthPx < kWidth && (widthPx & 7)) dst[widthPx / 8] &= (uint8_t)(0xFF << (8 - (widthPx & 7)));
      }
      if (!sendRaw(block, (size_t)rows * kWidthBytes)) return false;
    }
  }
  return endJob(heightPx);
}

bool MiniPocketPrinter::printCanvas(GFXcanvas1& canvas) {
  return printBitmap(canvas.getBuffer(), canvas.width(), canvas.height());
}

bool MiniPocketPrinter::printText(const char* text, uint8_t size) {
  if (!text || !*text) return false;
  if (size < 1) size = 1;
  // Measure with wrapping on a 1-row canvas, then render.
  GFXcanvas1 probe(kWidth, 1);
  probe.setTextSize(size);
  probe.setTextWrap(true);
  int16_t x1, y1;
  uint16_t w, h;
  probe.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  uint16_t height = h + 4;
  GFXcanvas1 canvas(kWidth, height);
  if (!canvas.getBuffer()) {
    lastError_ = "out of memory for text canvas";
    return false;
  }
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setTextSize(size);
  canvas.setTextWrap(true);
  canvas.setCursor(0, 2);
  canvas.print(text);
  return printCanvas(canvas);
}

bool MiniPocketPrinter::feed(uint16_t dots) {
  if (!startJob()) return false;
  uint8_t cmd[3];
  while (dots) {
    uint8_t n = dots > 255 ? 255 : dots;
    if (!sendRaw(cmd, feedDots(cmd, n))) return false;
    dots -= n;
  }
  ble_.clearReplies();
  if (!sendRaw(kFinalize, sizeof(kFinalize))) return false;
  uint8_t r[16];
  ble_.waitReply(r, sizeof(r), 3000);
  return true;
}
