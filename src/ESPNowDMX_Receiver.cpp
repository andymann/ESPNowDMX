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

#include "ESPNowDMX_Receiver.h"

ESPNowDMX_Receiver* ESPNowDMX_Receiver::instance = nullptr;

ESPNowDMX_Receiver::ESPNowDMX_Receiver()
  : lastSessionId(0), lastSequence(0), hasLastSessionId(false), hasLastSequence(false), userCallback(nullptr), espNowInitialized(false), universeId(0), lastRssi(RSSI_UNKNOWN) {
  memset(dmxBuffer, 0, DMX_UNIVERSE_SIZE);
  instance = this;
}

bool ESPNowDMX_Receiver::begin(bool registerInternalEspNow) {
  hasLastSessionId = false;
  hasLastSequence = false;
  lastSessionId = 0;
  lastSequence = 0;
  lastRssi = RSSI_UNKNOWN;
  if (registerInternalEspNow) {
    WiFi.mode(WIFI_STA);
  }
  if (registerInternalEspNow && !espNowInitialized) {
    if (esp_now_init() != ESP_OK) {
      return false;
    }
    esp_now_register_recv_cb(ESPNowDMX_Receiver::onDataReceived);
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

void ESPNowDMX_Receiver::setDMXReceiveCallback(DMXReceiveCallback cb) {
  userCallback = cb;
}

void ESPNowDMX_Receiver::setUniverseId(uint8_t universe) {
  universeId = universe;
}

bool ESPNowDMX_Receiver::handleReceive(const uint8_t *mac, const uint8_t *data, int len, int8_t rssi) {
  if (len < PACKET_HEADER_SIZE) return false;
  if (data[0] != PACKET_TYPE_DATA_CHUNK) return false;
#if ESPNOW_DMX_DEBUG
  static unsigned long lastHeaderLog = 0;
  unsigned long nowMs = millis();
  if (nowMs - lastHeaderLog >= 1000) {
    lastHeaderLog = nowMs;
    ESPNOW_DMX_LOG("[RX] raw len=%d rssi=%d", len, rssi);
  }
#endif
  processPacket(data, len, rssi);
  return true;
}

void ESPNowDMX_Receiver::processPacket(const uint8_t *data, int len, int8_t rssi) {
  uint8_t universe = data[1];
  uint8_t sessionId = data[2];
  uint16_t seq = (data[3] << 8) | data[4];
  uint16_t offset = (data[5] << 8) | data[6];
  // Byte 7: high nibble = wire-format version, low nibble = compression.
  // Mismatched versions are dropped: a peer running a different
  // PROTOCOL_VERSION would have a layout this build can't safely parse.
  uint8_t version = (data[7] & PROTOCOL_VERSION_MASK) >> 4;
  uint8_t compressionType = data[7] & COMPRESSION_MASK;
  if (version != PROTOCOL_VERSION) {
    return;
  }
  const uint8_t *payload = data + PACKET_HEADER_SIZE;
  size_t payloadLen = len - PACKET_HEADER_SIZE;

  if (universe != universeId) {
    return;
  }

  if (!hasLastSessionId || sessionId != lastSessionId) {
    lastSessionId = sessionId;
    hasLastSessionId = true;
    hasLastSequence = false;
    lastSequence = 0;
    memset(dmxBuffer, 0, sizeof(dmxBuffer));
  }

  if (offset >= DMX_UNIVERSE_SIZE) {
    return;
  }

  if (hasLastSequence) {
    uint16_t diff = seq - lastSequence;
    if (diff == 0) {
      return; // duplicate packet
    }
    if (diff > 0x8000) {
      return; // older packet
    }
  } else {
    hasLastSequence = true;
  }
  lastSequence = seq;
  lastRssi = rssi;

  // Decompress or copy raw data based on compression flag
  size_t decompressedLen = 0;
  if (compressionType == COMPRESSION_HEATSHRINK) {
    // Decompress heatshrink data
    size_t maxWritable = DMX_UNIVERSE_SIZE - offset;
    decompressedLen = decompressData(payload, payloadLen, dmxBuffer + offset, maxWritable);
    if (decompressedLen == 0) {
      // Decompression failed, discard packet
      return;
    }
  } else if (compressionType == COMPRESSION_NONE) {
    // Raw uncompressed data
    size_t maxWritable = DMX_UNIVERSE_SIZE - offset;
    if (payloadLen > maxWritable) {
      // Invalid offset/length
      return;
    }
    memcpy(dmxBuffer + offset, payload, payloadLen);
    decompressedLen = payloadLen;
  } else {
    // Unknown compression type, discard
    return;
  }

#if ESPNOW_DMX_DEBUG
  static unsigned long lastPacketLog = 0;
  unsigned long packetNow = millis();
  if (packetNow - lastPacketLog >= 500) {
    lastPacketLog = packetNow;
    ESPNOW_DMX_LOG("[RX] seq=%u offset=%u len=%u comp=%s", seq, offset, decompressedLen,
                   compressionType == COMPRESSION_HEATSHRINK ? "HS" : "RAW");
  }
#endif

  if (userCallback) {
    userCallback(universe, dmxBuffer);
#if ESPNOW_DMX_DEBUG
    static unsigned long lastFrameLog = 0;
    unsigned long frameNow = millis();
    if (frameNow - lastFrameLog >= 1000) {
      lastFrameLog = frameNow;
      ESPNOW_DMX_LOG("[RX] DMX frame delivered (universe %u)", universe);
    }
#endif
  }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void ESPNowDMX_Receiver::onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  const uint8_t* src = (info && info->src_addr) ? info->src_addr : nullptr;
  int8_t rssi = (info && info->rx_ctrl) ? static_cast<int8_t>(info->rx_ctrl->rssi) : RSSI_UNKNOWN;
  if (instance) {
    instance->handleReceive(src, data, len, rssi);
  }
}
#else
void ESPNowDMX_Receiver::onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
  if (instance) {
    instance->handleReceive(mac, data, len);
  }
}
#endif
