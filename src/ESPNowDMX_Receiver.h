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

#pragma once

#include "ESPNowDMX_Common.h"
#include "ESPNowDMX_Utils.h"

class ESPNowDMX_Receiver {
public:
  ESPNowDMX_Receiver();
  bool begin(bool registerInternalEspNow = true);

  void setDMXReceiveCallback(DMXReceiveCallback cb);
  void setUniverseId(uint8_t universe);

  // rssi: signal strength (dBm) of this packet, if known. Defaults to
  // RSSI_UNKNOWN for callers that can't recover it (e.g. external
  // ESP-NOW integrations on an ESP-IDF version that doesn't expose it).
  bool handleReceive(const uint8_t *mac, const uint8_t *data, int len, int8_t rssi = RSSI_UNKNOWN);

  // RSSI (dBm, closer to 0 = stronger) of the most recently accepted DMX
  // packet for the current universe. RSSI_UNKNOWN if nothing has been
  // received yet, or if it wasn't supplied to handleReceive().
  int8_t getLastRssi() const { return lastRssi; }

private:
  uint8_t dmxBuffer[DMX_UNIVERSE_SIZE];
  uint8_t lastSessionId;
  uint16_t lastSequence;
  bool hasLastSessionId;
  bool hasLastSequence;
  DMXReceiveCallback userCallback;
  bool espNowInitialized;
  uint8_t universeId;
  int8_t lastRssi;

  void processPacket(const uint8_t *data, int len, int8_t rssi);

  static ESPNowDMX_Receiver* instance;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  static void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len);
#else
  static void onDataReceived(const uint8_t *mac, const uint8_t *data, int len);
#endif
};
