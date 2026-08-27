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

class ESPNowDMX_Sender {
public:
  ESPNowDMX_Sender();
  bool begin(bool registerInternalEspNow = true);
  void setUniverse(const uint8_t* dmxData);
  void setUniverseId(uint8_t universe);
  void setChannel(uint16_t address, uint8_t value);
  void loop();

  // Periodic full-universe broadcast. Acts as a recovery point so receivers
  // that missed a delta packet eventually resync, even under continuous CC
  // streams that would otherwise prevent the idle-refresh path from firing.
  // 0 disables periodic full refresh. Default 200 ms.
  void setFullRefreshInterval(unsigned long intervalMs) { fullRefreshIntervalMs = intervalMs; }

  // Snapshot of the most recent ESP-NOW send-callback statistics.
  // consecutiveFailures is reset to 0 on every successful broadcast;
  // when it exceeds a host-defined threshold the host should consider
  // the radio wedged and call ESP.restart(). totalSent / totalFailed
  // are monotonic counters across the lifetime of the sender.
  struct SendStats {
    uint32_t totalSent = 0;
    uint32_t totalFailed = 0;
    uint16_t consecutiveFailures = 0;
  };
  static SendStats getSendStats();

  // Maximum number of unicast peer addresses that can be registered.
  static constexpr uint8_t MAX_UNICAST_PEERS = 16;

  // Register a unicast destination MAC address (up to MAX_UNICAST_PEERS).
  // Once one or more unicast peers are registered, DMX chunks are sent
  // directly to each of them instead of being broadcast. Safe to call
  // before or after begin() - addresses added before begin() are
  // registered with ESP-NOW as soon as it becomes available.
  // Returns false if the address is invalid/all-zero, the list is
  // already full, or (when ESP-NOW is already up) esp_now_add_peer fails.
  // Adding the same address twice is a no-op that returns true.
  bool addUnicastPeer(const uint8_t mac[6]);

  // Remove a previously registered unicast peer. Returns false if the
  // address was not registered.
  bool removeUnicastPeer(const uint8_t mac[6]);

  // Remove all unicast peers. Once the list is empty, sends fall back
  // to broadcasting again.
  void clearUnicastPeers();

  // Number of unicast peers currently registered (0 = broadcasting).
  uint8_t getUnicastPeerCount() const { return unicastCount; }

private:
  uint8_t currentUniverse[DMX_UNIVERSE_SIZE];
  uint8_t prevUniverse[DMX_UNIVERSE_SIZE];
  uint8_t sessionId;
  uint16_t seqNumber;
  unsigned long lastSendTime;
  unsigned long lastFullSendTime;
  unsigned long fullRefreshIntervalMs;
  bool espNowInitialized;
  bool forceFullRefreshOnNextLoop;
  uint8_t universeId;
  uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  // Unicast peers. When unicastCount > 0, sendChunk() targets these
  // addresses instead of broadcastAddr.
  uint8_t unicastAddrs[MAX_UNICAST_PEERS][6];
  uint8_t unicastCount;
  // True once begin() has successfully brought ESP-NOW up, i.e. once
  // esp_now_add_peer() calls are safe to issue immediately.
  bool peersReady;

  bool registerEspNowPeer(const uint8_t mac[6]);
  void sendChunk(uint16_t offset, uint16_t length);
  void sendRange(uint16_t offset, uint16_t length);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  static void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status);
#else
  static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
#endif
};
