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

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif

#ifndef ESP_IDF_VERSION_MAJOR
#define ESP_IDF_VERSION_MAJOR 3
#define ESP_IDF_VERSION_MINOR 0
#define ESP_IDF_VERSION_PATCH 0
#endif

#ifndef ESP_IDF_VERSION_VAL
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#endif

#ifndef ESP_IDF_VERSION
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH)
#endif

// DMX universe size
constexpr uint16_t DMX_UNIVERSE_SIZE = 512;

// Max payload size for ESP-NOW
constexpr uint16_t ESP_NOW_MAX_PAYLOAD = 250;

// Packet types
enum PacketType : uint8_t {
  PACKET_TYPE_DATA_CHUNK = 0x01,
};

// Compression flags (low nibble of header byte 6).
enum CompressionType : uint8_t {
  COMPRESSION_NONE = 0x00,
  COMPRESSION_HEATSHRINK = 0x01,
};

// Wire-format version (high nibble of header byte 7).
// Lets future protocol changes (additional channels, per-node addressing,
// frame counters, etc.) reject mismatched peers gracefully instead of
// silently corrupting state. Bump this when the on-wire format changes.
//
// Receivers running an older version see an unknown high nibble and
// drop the packet via the existing "unknown compression type" branch —
// no garbled state, no reboot loop. After upgrading, reflash everything.
constexpr uint8_t PROTOCOL_VERSION = 0x02;
constexpr uint8_t PROTOCOL_VERSION_MASK = 0xF0;
constexpr uint8_t COMPRESSION_MASK = 0x0F;

// DMX data chunk packet header size
constexpr uint8_t PACKET_HEADER_SIZE = 8; // type(1) + universe(1) + session(1) + seq(2) + offset(2) + version|compression(1)

// Max DMX data chunk per packet
constexpr uint16_t MAX_DMX_CHUNK_SIZE = ESP_NOW_MAX_PAYLOAD - PACKET_HEADER_SIZE;

// Type for DMX data buffer
using DMXUniverseBuffer = uint8_t[DMX_UNIVERSE_SIZE];

// User callback type for receiver to receive full buffer
using DMXReceiveCallback = void (*)(uint8_t universe, const uint8_t *dmxData);

// Sentinel RSSI (dBm) value meaning "not available" - real Wi-Fi RSSI
// readings fall well within a couple hundred of 0, so this is unambiguous.
// Returned/passed when the underlying ESP-IDF version or an external
// ESP-NOW integration can't supply per-packet signal strength.
constexpr int8_t RSSI_UNKNOWN = -128;

// Feature toggles (override via build_flags)
#ifndef ESPNOW_DMX_ENABLE_COMPRESSION
#define ESPNOW_DMX_ENABLE_COMPRESSION 1
#endif

#ifndef ESPNOW_DMX_DEBUG
#define ESPNOW_DMX_DEBUG 0
#endif

#if ESPNOW_DMX_DEBUG
#define ESPNOW_DMX_LOG(fmt, ...) Serial.printf("[ESPNowDMX] " fmt "\n", ##__VA_ARGS__)
#else
#define ESPNOW_DMX_LOG(...) do { } while (0)
#endif
