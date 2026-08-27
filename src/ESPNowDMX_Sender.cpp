/*
 * ESPNowDMX - DMX over ESP-NOW for ESP32
 * Copyright (c) 2025 maigre, Hemisphere-Project
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ESPNowDMX_Sender.h"

#include <esp_system.h>

namespace {
// Process-wide send statistics, written from the ESP-NOW send callback
// (which fires on a Wi-Fi internal task) and read from user code on the
// main task. Single producer / single consumer, plain volatile is OK on
// the 32-bit values; consecutiveFailures may race by ±1 but that's fine
// for "should I restart the radio" logic.
volatile uint32_t g_totalSent = 0;
volatile uint32_t g_totalFailed = 0;
volatile uint16_t g_consecutiveFailures = 0;
}

ESPNowDMX_Sender::SendStats ESPNowDMX_Sender::getSendStats() {
  SendStats s;
  s.totalSent = g_totalSent;
  s.totalFailed = g_totalFailed;
  s.consecutiveFailures = g_consecutiveFailures;
  return s;
}

ESPNowDMX_Sender::ESPNowDMX_Sender()
  : sessionId(0),
    seqNumber(0),
    lastSendTime(0),
    lastFullSendTime(0),
    fullRefreshIntervalMs(200),
    espNowInitialized(false),
    forceFullRefreshOnNextLoop(true),
    universeId(0),
    unicastCount(0),
    peersReady(false) {
  memset(currentUniverse, 0, DMX_UNIVERSE_SIZE);
  memset(prevUniverse, 0, DMX_UNIVERSE_SIZE);
  memset(unicastAddrs, 0, sizeof(unicastAddrs));
}

bool ESPNowDMX_Sender::begin(bool registerInternalEspNow) {
  sessionId = static_cast<uint8_t>(esp_random());
  seqNumber = 0;
  lastSendTime = 0;
  lastFullSendTime = 0;
  forceFullRefreshOnNextLoop = true;
  memset(prevUniverse, 0, DMX_UNIVERSE_SIZE);

  if (registerInternalEspNow) {
    WiFi.mode(WIFI_STA);
  }
  if (registerInternalEspNow && !espNowInitialized) {
    if (esp_now_init() != ESP_OK) {
      return false;
    }
    espNowInitialized = true;
  }

  // Register the send callback even when another component (for example
  // MeshClock) owns ESP-NOW initialization. Without this, the sender's
  // health counters never update in shared-stack mode, so DMX sends can
  // fail silently while clock packets continue to flow.
  esp_now_register_send_cb(ESPNowDMX_Sender::onDataSent);

  if (!registerEspNowPeer(broadcastAddr)) {
    return false;
  }

  // Re-register any unicast peers that were added before begin() was
  // called (esp_now_add_peer only works once ESP-NOW is up).
  for (uint8_t i = 0; i < unicastCount; i++) {
    if (!registerEspNowPeer(unicastAddrs[i])) {
      return false;
    }
  }

  peersReady = true;
  return true;
}

bool ESPNowDMX_Sender::registerEspNowPeer(const uint8_t mac[6]) {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;

  esp_err_t result = esp_now_add_peer(&peer);
  return result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST;
}

bool ESPNowDMX_Sender::addUnicastPeer(const uint8_t mac[6]) {
  if (mac == nullptr) return false;

  bool allZero = true;
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0) { allZero = false; break; }
  }
  if (allZero) return false;

  for (uint8_t i = 0; i < unicastCount; i++) {
    if (memcmp(unicastAddrs[i], mac, 6) == 0) {
      return true; // already registered
    }
  }

  if (unicastCount >= MAX_UNICAST_PEERS) return false;

  memcpy(unicastAddrs[unicastCount], mac, 6);
  unicastCount++;

  if (peersReady) {
    if (!registerEspNowPeer(mac)) {
      unicastCount--;
      return false;
    }
  }

  return true;
}

bool ESPNowDMX_Sender::removeUnicastPeer(const uint8_t mac[6]) {
  if (mac == nullptr) return false;

  for (uint8_t i = 0; i < unicastCount; i++) {
    if (memcmp(unicastAddrs[i], mac, 6) == 0) {
      if (peersReady) {
        esp_now_del_peer(unicastAddrs[i]);
      }
      for (uint8_t j = i; j < unicastCount - 1; j++) {
        memcpy(unicastAddrs[j], unicastAddrs[j + 1], 6);
      }
      unicastCount--;
      return true;
    }
  }
  return false;
}

void ESPNowDMX_Sender::clearUnicastPeers() {
  if (peersReady) {
    for (uint8_t i = 0; i < unicastCount; i++) {
      esp_now_del_peer(unicastAddrs[i]);
    }
  }
  unicastCount = 0;
}

void ESPNowDMX_Sender::setUniverse(const uint8_t* dmxData) {
  memcpy(currentUniverse, dmxData, DMX_UNIVERSE_SIZE);
}

void ESPNowDMX_Sender::setUniverseId(uint8_t universe) {
  universeId = universe;
}

void ESPNowDMX_Sender::setChannel(uint16_t address, uint8_t value) {
  if (address == 0 || address > DMX_UNIVERSE_SIZE) {
    return;
  }
  currentUniverse[address - 1] = value;
}

void ESPNowDMX_Sender::loop() {
  unsigned long now = millis();
  const unsigned long rapidInterval = 33;
  const unsigned long slowInterval = 100;

  uint16_t minChanged = DMX_UNIVERSE_SIZE, maxChanged = 0;
  bool anyChange = false;

  for (uint16_t i = 0; i < DMX_UNIVERSE_SIZE; i++) {
    if (currentUniverse[i] != prevUniverse[i]) {
      if (i < minChanged) minChanged = i;
      if (i > maxChanged) maxChanged = i;
      anyChange = true;
    }
  }

  // Periodic full-universe broadcast: every fullRefreshIntervalMs ms we ignore
  // the delta and resend the whole frame. This is the only thing that lets
  // receivers recover from a dropped delta packet while the master keeps
  // streaming changes (e.g. continuous CC automation from a DAW). Without
  // this the idle-resend path below never fires and slaves stay desynced
  // until everything goes quiet.
  bool forceFull = forceFullRefreshOnNextLoop ||
                   (fullRefreshIntervalMs > 0 &&
                    (now - lastFullSendTime) >= fullRefreshIntervalMs);

  if (forceFull) {
    // Skip the rapidInterval coalescing guard here: the full-refresh
    // period (fullRefreshIntervalMs, typically 200 ms) is the rate
    // limiter. Letting the rapid guard delay or cancel a scheduled full
    // refresh breaks the ≤200 ms recovery guarantee under sustained
    // delta traffic (e.g. continuous CC automation from a DAW).
    sendRange(0, DMX_UNIVERSE_SIZE);
    lastSendTime = now;
    lastFullSendTime = now;
    forceFullRefreshOnNextLoop = false;
    return;
  }

  if (!anyChange) {
    if (now - lastSendTime < slowInterval) return;
    sendRange(0, DMX_UNIVERSE_SIZE);
    lastSendTime = now;
    lastFullSendTime = now;
    return;
  }

  if (now - lastSendTime < rapidInterval) return;
  sendRange(minChanged, maxChanged - minChanged + 1);
  lastSendTime = now;
}

void ESPNowDMX_Sender::sendRange(uint16_t offset, uint16_t length) {
  if (length == 0 || offset >= DMX_UNIVERSE_SIZE) return;
  if (offset + length > DMX_UNIVERSE_SIZE) {
    length = DMX_UNIVERSE_SIZE - offset;
  }

  uint16_t processed = 0;
  while (processed < length) {
    uint16_t remaining = length - processed;
    uint16_t chunkLen = remaining > MAX_DMX_CHUNK_SIZE ? MAX_DMX_CHUNK_SIZE : remaining;
    uint16_t chunkOffset = offset + processed;

    sendChunk(chunkOffset, chunkLen);

    memcpy(prevUniverse + chunkOffset, currentUniverse + chunkOffset, chunkLen);
    processed += chunkLen;
  }
}

void ESPNowDMX_Sender::sendChunk(uint16_t offset, uint16_t length) {
  uint8_t packet[ESP_NOW_MAX_PAYLOAD];
  uint8_t compBuffer[ESP_NOW_MAX_PAYLOAD - PACKET_HEADER_SIZE];

  packet[0] = PACKET_TYPE_DATA_CHUNK;
  packet[1] = universeId;
  packet[2] = sessionId;
  packet[3] = (seqNumber >> 8) & 0xFF;
  packet[4] = seqNumber & 0xFF;
  packet[5] = (offset >> 8) & 0xFF;
  packet[6] = offset & 0xFF;

  size_t payloadSize = length;

  // Byte 7 packs PROTOCOL_VERSION in the high nibble and the
  // compression flag in the low nibble. Receivers running a
  // different PROTOCOL_VERSION will read an unknown low nibble
  // (since the version bits look like an "unknown compression
  // type" to them) and drop the packet — no garbled state.
  const uint8_t versionBits = (PROTOCOL_VERSION << 4) & PROTOCOL_VERSION_MASK;

#if ESPNOW_DMX_ENABLE_COMPRESSION
  // Try heatshrink compression when explicitly enabled
  size_t compressedSize = compressData(currentUniverse + offset, length, compBuffer, sizeof(compBuffer));
  if (compressedSize > 0 && compressedSize < length) {
    packet[7] = versionBits | (COMPRESSION_HEATSHRINK & COMPRESSION_MASK);
    memcpy(packet + PACKET_HEADER_SIZE, compBuffer, compressedSize);
    payloadSize = compressedSize;
  } else {
    packet[7] = versionBits | (COMPRESSION_NONE & COMPRESSION_MASK);
    memcpy(packet + PACKET_HEADER_SIZE, currentUniverse + offset, length);
    payloadSize = length;
  }
#else
  (void)compBuffer;
  packet[7] = versionBits | (COMPRESSION_NONE & COMPRESSION_MASK);
  memcpy(packet + PACKET_HEADER_SIZE, currentUniverse + offset, length);
#endif

  size_t sendLength = PACKET_HEADER_SIZE + payloadSize;
  esp_err_t err = ESP_OK;
  if (unicastCount > 0) {
    for (uint8_t i = 0; i < unicastCount; i++) {
      esp_err_t r = esp_now_send(unicastAddrs[i], packet, sendLength);
      if (r != ESP_OK) err = r;
    }
  } else {
    err = esp_now_send(broadcastAddr, packet, sendLength);
  }

#if ESPNOW_DMX_DEBUG
  static unsigned long lastLog = 0;
  unsigned long nowMs = millis();
  if (nowMs - lastLog >= 500) {
    lastLog = nowMs;
    ESPNOW_DMX_LOG("[TX] seq=%u offset=%u len=%u comp=%s err=%d", seqNumber, offset,
                   payloadSize,
                   (packet[7] & COMPRESSION_MASK) == COMPRESSION_HEATSHRINK ? "HS" : "RAW",
                   err);
  }
#endif

  seqNumber++;
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void ESPNowDMX_Sender::onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  (void)info;
  if (status == ESP_NOW_SEND_SUCCESS) {
    g_totalSent++;
    g_consecutiveFailures = 0;
  } else {
    g_totalFailed++;
    if (g_consecutiveFailures < UINT16_MAX) g_consecutiveFailures++;
  }
}
#else
void ESPNowDMX_Sender::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  (void)mac_addr;
  if (status == ESP_NOW_SEND_SUCCESS) {
    g_totalSent++;
    g_consecutiveFailures = 0;
  } else {
    g_totalFailed++;
    if (g_consecutiveFailures < UINT16_MAX) g_consecutiveFailures++;
  }
}
#endif
