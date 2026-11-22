// BLE transport for UART-style thermal printers: connect, chunked write, reply buffer.
// Knows nothing about printer opcodes.
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>

class BleTransport {
 public:
  struct Endpoint {
    const char* service;
    const char* write;
    const char* notify;
  };

  // mac may be nullptr/"" to match by name only. name matches as a prefix.
  bool connect(const char* mac, const char* name, const Endpoint& ep, uint32_t scanMs = 6000);
  void disconnect();
  bool isConnected() const;

  // Writes in chunks of chunkSize (capped to the MTU), pausing chunkDelayMs between chunks.
  bool write(const uint8_t* data, size_t len);

  // Reply buffer fed by notifications.
  void clearReplies();
  size_t available() const;
  // Waits until at least minBytes are buffered (or timeout), then copies up to cap bytes out.
  size_t waitReply(uint8_t* out, size_t cap, uint32_t timeoutMs, size_t minBytes = 1);

  uint16_t chunkSize = 100;     // printer announced a 100-byte payload limit
  uint16_t chunkDelayMs = 15;   // pacing between chunks to avoid overflowing its buffer
  bool debug = false;

 private:
  static void onNotify(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify);
  static void push(const uint8_t* data, size_t len);

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* writeChr_ = nullptr;
  NimBLERemoteCharacteristic* notifyChr_ = nullptr;

  static const size_t kRxSize = 512;
  static uint8_t rx_[kRxSize];
  static volatile size_t rxHead_, rxTail_;
  static portMUX_TYPE rxMux_;
  static bool debugRx_;
};
