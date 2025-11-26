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

ESPNowDMX_Sender::ESPNowDMX_Sender()
  : seqNumber(0), lastSendTime(0), espNowInitialized(false), universeId(0) {
  memset(currentUniverse, 0, DMX_UNIVERSE_SIZE);
  memset(prevUniverse, 0, DMX_UNIVERSE_SIZE);
}

bool ESPNowDMX_Sender::begin(bool registerInternalEspNow) {
  if (registerInternalEspNow) {
    WiFi.mode(WIFI_STA);
  }
  if (registerInternalEspNow && !espNowInitialized) {
    if (esp_now_init() != ESP_OK) {
      return false;
    }
    esp_now_register_send_cb(ESPNowDMX_Sender::onDataSent);
    espNowInitialized = true;
  }

  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6);
  peer.channel = 0;
  peer.encrypt = false;
  
  esp_err_t result = esp_now_add_peer(&peer);
  if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
    return false;
  }

  return true;
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

  if (!anyChange) {
    if (now - lastSendTime < slowInterval) return;
    minChanged = 0;
    maxChanged = DMX_UNIVERSE_SIZE - 1;
  } else {
    if (now - lastSendTime < rapidInterval) return;
  }

  lastSendTime = now;

  uint16_t totalLen = maxChanged - minChanged + 1;
  uint16_t processed = 0;

  while (processed < totalLen) {
    uint16_t remaining = totalLen - processed;
    uint16_t chunkLen = remaining > MAX_DMX_CHUNK_SIZE ? MAX_DMX_CHUNK_SIZE : remaining;
    uint16_t chunkOffset = minChanged + processed;

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
  packet[2] = (seqNumber >> 8) & 0xFF;
  packet[3] = seqNumber & 0xFF;
  packet[4] = (offset >> 8) & 0xFF;
  packet[5] = offset & 0xFF;

  size_t payloadSize = length;

#if ESPNOW_DMX_ENABLE_COMPRESSION
  // Try heatshrink compression when explicitly enabled
  size_t compressedSize = compressData(currentUniverse + offset, length, compBuffer, sizeof(compBuffer));
  if (compressedSize > 0 && compressedSize < length) {
    packet[6] = COMPRESSION_HEATSHRINK;
    memcpy(packet + PACKET_HEADER_SIZE, compBuffer, compressedSize);
    payloadSize = compressedSize;
  } else {
    packet[6] = COMPRESSION_NONE;
    memcpy(packet + PACKET_HEADER_SIZE, currentUniverse + offset, length);
    payloadSize = length;
  }
#else
  (void)compBuffer;
  packet[6] = COMPRESSION_NONE;
  memcpy(packet + PACKET_HEADER_SIZE, currentUniverse + offset, length);
#endif

  size_t sendLength = PACKET_HEADER_SIZE + payloadSize;
  esp_err_t err = esp_now_send(broadcastAddr, packet, sendLength);

#if ESPNOW_DMX_DEBUG
  static unsigned long lastLog = 0;
  unsigned long nowMs = millis();
  if (nowMs - lastLog >= 500) {
    lastLog = nowMs;
    ESPNOW_DMX_LOG("[TX] seq=%u offset=%u len=%u comp=%s err=%d", seqNumber, offset,
                   payloadSize, packet[6] == COMPRESSION_HEATSHRINK ? "HS" : "RAW", err);
  }
#endif

  seqNumber++;
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void ESPNowDMX_Sender::onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  (void)info;
  (void)status;
  // Optional callback for send status
}
#else
void ESPNowDMX_Sender::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  (void)mac_addr;
  (void)status;
  // Optional callback for send status
}
#endif
