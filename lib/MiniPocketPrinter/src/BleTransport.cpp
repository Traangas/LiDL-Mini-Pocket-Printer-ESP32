#include "BleTransport.h"

uint8_t BleTransport::rx_[BleTransport::kRxSize];
volatile size_t BleTransport::rxHead_ = 0;
volatile size_t BleTransport::rxTail_ = 0;
portMUX_TYPE BleTransport::rxMux_ = portMUX_INITIALIZER_UNLOCKED;
bool BleTransport::debugRx_ = false;

static void printHex(const char* prefix, const uint8_t* d, size_t n) {
  Serial.print(prefix);
  for (size_t i = 0; i < n && i < 32; i++) Serial.printf("%02x ", d[i]);
  if (n > 32) Serial.printf("... (%u bytes)", (unsigned)n);
  Serial.println();
}

void BleTransport::push(const uint8_t* data, size_t len) {
  portENTER_CRITICAL(&rxMux_);
  for (size_t i = 0; i < len; i++) {
    size_t next = (rxHead_ + 1) % kRxSize;
    if (next == rxTail_) break;  // full: drop
    rx_[rxHead_] = data[i];
    rxHead_ = next;
  }
  portEXIT_CRITICAL(&rxMux_);
}

void BleTransport::onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (debugRx_) printHex("[ble] << ", data, len);
  push(data, len);
}

bool BleTransport::connect(const char* mac, const char* name, const Endpoint& ep, uint32_t scanMs) {
  debugRx_ = debug;
  if (!NimBLEDevice::isInitialized()) {
    NimBLEDevice::init("");
    NimBLEDevice::setMTU(247);
  }
  disconnect();

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  NimBLEScanResults results = scan->getResults(scanMs, false);

  const NimBLEAdvertisedDevice* target = nullptr;
  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    bool macMatch = mac && *mac && strcasecmp(d->getAddress().toString().c_str(), mac) == 0;
    bool nameMatch = name && *name && d->getName().rfind(name, 0) == 0;
    if (macMatch || nameMatch) { target = d; break; }
  }
  if (!target) {
    if (debug) Serial.println("[ble] printer not found in scan");
    return false;
  }

  if (!client_) client_ = NimBLEDevice::createClient();
  client_->setConnectTimeout(8000);
  if (debug) Serial.printf("[ble] connecting to %s\n", target->getAddress().toString().c_str());
  if (!client_->connect(target)) {
    if (debug) Serial.println("[ble] connect failed (is the phone app connected?)");
    return false;
  }

  NimBLERemoteService* svc = client_->getService(ep.service);
  if (svc) {
    writeChr_ = svc->getCharacteristic(ep.write);
    notifyChr_ = svc->getCharacteristic(ep.notify);
  }
  if (!writeChr_ || !notifyChr_) {
    if (debug) Serial.println("[ble] expected service/characteristics not found");
    disconnect();
    return false;
  }
  if (!notifyChr_->subscribe(true, onNotify)) {
    if (debug) Serial.println("[ble] subscribe failed");
    disconnect();
    return false;
  }
  clearReplies();
  if (debug) Serial.printf("[ble] connected, MTU %u\n", client_->getMTU());
  return true;
}

void BleTransport::disconnect() {
  if (client_ && client_->isConnected()) client_->disconnect();
  writeChr_ = nullptr;
  notifyChr_ = nullptr;
}

bool BleTransport::isConnected() const {
  return client_ && client_->isConnected() && writeChr_;
}

bool BleTransport::write(const uint8_t* data, size_t len) {
  if (!isConnected()) return false;
  size_t mtuPayload = client_->getMTU() > 3 ? client_->getMTU() - 3 : 20;
  size_t chunk = chunkSize < mtuPayload ? chunkSize : mtuPayload;
  bool withResponse = !writeChr_->canWriteNoResponse();
  if (debug) printHex("[ble] >> ", data, len);
  for (size_t off = 0; off < len; off += chunk) {
    size_t n = (len - off) < chunk ? (len - off) : chunk;
    bool ok = false;
    for (int attempt = 0; attempt < 5 && !ok; attempt++) {
      ok = writeChr_->writeValue(data + off, n, withResponse);
      if (!ok) delay(20);
    }
    if (!ok) return false;
    if (chunkDelayMs && off + n < len) delay(chunkDelayMs);
  }
  return true;
}

void BleTransport::clearReplies() {
  portENTER_CRITICAL(&rxMux_);
  rxHead_ = rxTail_ = 0;
  portEXIT_CRITICAL(&rxMux_);
}

size_t BleTransport::available() const {
  portENTER_CRITICAL(&rxMux_);
  size_t n = (rxHead_ + kRxSize - rxTail_) % kRxSize;
  portEXIT_CRITICAL(&rxMux_);
  return n;
}

size_t BleTransport::waitReply(uint8_t* out, size_t cap, uint32_t timeoutMs, size_t minBytes) {
  uint32_t t0 = millis();
  while (available() < minBytes && millis() - t0 < timeoutMs) delay(5);
  // let the rest of a split notification arrive
  if (available()) delay(20);
  size_t n = 0;
  portENTER_CRITICAL(&rxMux_);
  while (rxTail_ != rxHead_ && n < cap) {
    out[n++] = rx_[rxTail_];
    rxTail_ = (rxTail_ + 1) % kRxSize;
  }
  portEXIT_CRITICAL(&rxMux_);
  return n;
}
