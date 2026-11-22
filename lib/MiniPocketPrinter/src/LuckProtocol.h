// LuckPrinter "normal" protocol encoder (model A2Y, Lidl TRONIC Mini Pocket Printer).
// Pure byte builders: no transport knowledge. See PROTOCOL.md.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace LuckProtocol {

static const uint16_t kWidthDots = 384;
static const uint16_t kWidthBytes = kWidthDots / 8;

// Queries (replies arrive on the notify characteristic)
static const uint8_t kGetModel[] = {0x10, 0xFF, 0x20, 0xF0};
static const uint8_t kGetFirmware[] = {0x10, 0xFF, 0x20, 0xF1};
static const uint8_t kGetSerial[] = {0x10, 0xFF, 0x20, 0xF2};
static const uint8_t kGetBattery[] = {0x10, 0xFF, 0x50, 0xF1};  // reply: [status, percent]
static const uint8_t kGetStatus[] = {0x10, 0xFF, 0x40};         // reply: 1-byte bitmask

// Print job framing
static const uint8_t kEnable[] = {0x10, 0xFF, 0xF1, 0x03};
static const uint8_t kFinalize[] = {0x10, 0xFF, 0xF1, 0x45};   // reply: "OK" or 0xAA
static const uint8_t kWakeup[12] = {0};

// Status bits (10 FF 40)
enum StatusBits : uint8_t {
  kStatusPrinting = 0x01,
  kStatusCoverOpen = 0x02,
  kStatusNoPaper = 0x04,
  kStatusLowBattery = 0x08,
  kStatusOverheatAlt = 0x10,
  kStatusCharging = 0x20,
  kStatusOverheat = 0x40,
};
// Bits that block a new job (charging does not).
static const uint8_t kStatusErrorMask = 0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x40;

// 10 FF 10 00 nn -> "OK". 0 = light, 1 = medium, 2 = dark.
inline size_t setDensity(uint8_t* out, uint8_t level) {
  out[0] = 0x10; out[1] = 0xFF; out[2] = 0x10; out[3] = 0x00; out[4] = level;
  return 5;
}

// ESC J n: feed n dots (max 255 per command).
inline size_t feedDots(uint8_t* out, uint8_t dots) {
  out[0] = 0x1B; out[1] = 0x4A; out[2] = dots;
  return 3;
}

// GS v 0 raster header: width in bytes and height in rows, little-endian.
// Row data follows: MSB-first, 1 = black.
inline size_t rasterHeader(uint8_t* out, uint16_t widthBytes, uint16_t rows) {
  out[0] = 0x1D; out[1] = 0x76; out[2] = 0x30; out[3] = 0x00;
  out[4] = widthBytes & 0xFF; out[5] = widthBytes >> 8;
  out[6] = rows & 0xFF; out[7] = rows >> 8;
  return 8;
}

}  // namespace LuckProtocol
