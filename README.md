# ESPNowDMX

ESPNowDMX is a library for ESP32 devices to transmit and receive DMX lighting control data over ESP-NOW.

## Features

- Separate Sender and Receiver classes
- Single DMX universe (512 channels)
- Adaptive update rate depending on data variation
- Chunked packets with sequence and offset indexing
- Sequence number wrap-around handling (16-bit counter)
- **Heatshrink compression** with automatic raw fallback
- Broadcast by default, or up to 16 unicast destination addresses
- Flexible ESP-NOW integration (standalone or external)
- Receiver callback with full universe DMX data
- Per-packet RSSI (signal strength) reporting on the receiver
- Error handling for ESP-NOW operations

## Installation

### Arduino IDE
1. Download this repository as ZIP
2. In Arduino IDE: Sketch → Include Library → Add .ZIP Library
3. Select the downloaded ZIP file

### PlatformIO
Add to your `platformio.ini`:
```ini
lib_deps = 
    https://github.com/Hemisphere-Project/ESPNowDMX.git
```

## Usage

### Quick Wrapper
`ESPNowDMX` bundles the sender/receiver helpers and lets you configure the universe ID before initialization:

```cpp
#include "ESPNowDMX.h"

ESPNowDMX dmx;

void setup() {
    dmx.setUniverseId(3);      // optional, defaults to 0 on both ends
    dmx.begin(ESPNOW_DMX_MODE_SENDER);
}

void loop() {
    dmx.setChannel(1, 255);
    dmx.loop();
}
```

### Standalone Mode (Default)
The library handles ESP-NOW initialization internally:

```cpp
#include "ESPNowDMX_Sender.h"

ESPNowDMX_Sender sender;

void setup() {
    sender.begin();  // Initializes ESP-NOW internally
}
```

### External ESP-NOW Integration
For projects already using ESP-NOW, use `begin(false)`:

```cpp
#include "ESPNowDMX_Receiver.h"

ESPNowDMX_Receiver receiver;

void onEspNowReceive(const uint8_t *mac, const uint8_t *data, int len) {
    // Route DMX packets to receiver
    if (receiver.handleReceive(mac, data, len)) {
        return;  // Was a DMX packet
    }
    // Handle your own custom ESP-NOW messages here
}

void setup() {
    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_register_recv_cb(onEspNowReceive);
    
    receiver.begin(false);  // false = external ESP-NOW management
    receiver.setDMXReceiveCallback(myCallback);
}
```

### Unicast Mode
By default the sender broadcasts to all ESP-NOW peers on the channel. To
target specific receivers instead, register up to 16 unicast MAC addresses
before (or after) `begin()`. Once at least one unicast peer is registered,
every DMX packet is sent directly to each registered address instead of
being broadcast:

```cpp
ESPNowDMX_Sender sender;

uint8_t receiver1[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
uint8_t receiver2[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x44};

void setup() {
    sender.addUnicastPeer(receiver1);
    sender.addUnicastPeer(receiver2);
    sender.begin();
}
```

Call `clearUnicastPeers()` to drop back to broadcasting. This also works
through the `ESPNowDMX` wrapper (`dmx.addUnicastPeer(mac)`, etc.).

### Signal Strength (RSSI)
The receiver tracks the RSSI (dBm) of the most recently accepted DMX packet
for its universe, so you can evaluate link quality:

```cpp
void dmxCallback(uint8_t universe, const uint8_t* data) {
    int8_t rssi = receiver.getLastRssi();   // e.g. -42 dBm; closer to 0 is stronger
    Serial.printf("universe %d, rssi %d dBm\n", universe, rssi);
}
```

`getLastRssi()` returns `RSSI_UNKNOWN` (defined in `ESPNowDMX_Common.h`) before
anything has been received. RSSI is captured automatically in standalone mode
(`begin(true)`, the default). In external ESP-NOW mode, pass it through
yourself if your platform's receive callback supplies it - on ESP-IDF 5+ that's
`info->rx_ctrl->rssi`:

```cpp
void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    receiver.handleReceive(info->src_addr, data, len, info->rx_ctrl->rssi);
}
```

Omit the last argument and it defaults to `RSSI_UNKNOWN`.

## API Reference

### ESPNowDMX_Sender

**`bool begin(bool registerInternalEspNow = true)`**
- Initialize the sender
- `registerInternalEspNow`: Set to `false` if managing ESP-NOW externally
- Returns `true` on success

**`void setUniverse(const uint8_t* dmxData)`**
- Update the entire DMX universe buffer (512 bytes)

**`void setChannel(uint16_t address, uint8_t value)`**
- Set a specific DMX channel value
- `address`: DMX channel number (1-512, inclusive)
- `value`: DMX value (0-255)
- Recommended flow: push a complete frame with `setUniverse()` first so receivers have a baseline, then issue incremental `setChannel()` updates.
- Sender and receiver default to universe 0. If you change universes, call `setUniverseId()` on both devices before `begin()` (or immediately after).

**`void loop()`**
- Call frequently in `loop()` to send adaptive updates

**`bool addUnicastPeer(const uint8_t mac[6])`**
- Register a unicast destination MAC address (up to 16)
- Once one or more unicast peers are registered, DMX packets are sent to each of them instead of being broadcast
- Safe to call before or after `begin()`
- Returns `false` if the address is invalid, the list is full, or peer registration with ESP-NOW fails
- Adding the same address twice is a no-op that returns `true`

**`bool removeUnicastPeer(const uint8_t mac[6])`**
- Remove a previously registered unicast peer
- Returns `false` if the address was not registered

**`void clearUnicastPeers()`**
- Remove all unicast peers, reverting to broadcast sends

**`uint8_t getUnicastPeerCount() const`**
- Number of unicast peers currently registered (`0` means broadcasting)

### ESPNowDMX_Receiver

**`bool begin(bool registerInternalEspNow = true)`**
- Initialize the receiver
- `registerInternalEspNow`: Set to `false` if managing ESP-NOW externally
- Returns `true` on success

**`void setDMXReceiveCallback(DMXReceiveCallback cb)`**
- Register callback for received DMX data
- Callback signature: `void callback(uint8_t universe, const uint8_t* dmxData)`

**`bool handleReceive(const uint8_t *mac, const uint8_t *data, int len, int8_t rssi = RSSI_UNKNOWN)`**
- Process incoming ESP-NOW packet
- `rssi`: signal strength (dBm) of this packet, if your platform's callback supplies it; defaults to `RSSI_UNKNOWN`
- Returns `true` if packet was a DMX packet, `false` otherwise
- Use this when managing ESP-NOW externally

**`int8_t getLastRssi() const`**
- RSSI (dBm) of the most recently accepted DMX packet for the current universe
- `RSSI_UNKNOWN` if nothing has been received yet, or the caller didn't supply it to `handleReceive()`

## Examples

### SenderExample
Demonstrates both DMX sending methods with explicit universe seeding:
- **Mode 1** (default): Seed a universe once with `setUniverse()`, then animate individual channels via `setChannel()` (RGB fade)
- **Mode 2**: Bulk universe update with `setUniverse()` - channel sweep

Switch modes by editing the `#define` at the top of the sketch.

### ReceiverExample  
Basic DMX receiver that prints received values to serial.

See `examples/` folder for complete code.

## Technical Details

- **Packet Structure**: Type (1B) + Universe (1B) + Sequence (2B) + Offset (2B) + Compression Flag (1B) + Data
- **Max Chunk Size**: 243 bytes per packet
- **Adaptive Rate**: 33ms when data changes, 100ms when stable
- **Sequence Handling**: Proper 16-bit wrap-around detection
- **Compression**: Heatshrink (LZSS) with automatic raw fallback
  - **Window**: 2^8 (256 bytes)
  - **Lookahead**: 2^4 (16 bytes)
  - **Memory**: ~800 bytes RAM (static allocation)
  - **Best case**: Static scenes compress 40-60%
  - **Worst case**: Random/gradient data sent raw (0% overhead)
  - **Latency**: ~0.5-1ms compression + transmission

## License

GPL-3.0

## Credits
- **ESPNowDMX** by Hemisphere-Project
- **heatshrink** compression library by Scott Vokes / Atomic Object (ISC License)
  - https://github.com/atomicobject/heatshrink
