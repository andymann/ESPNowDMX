#include <Arduino.h>
#include "ESPNowDMX_Receiver.h"

ESPNowDMX_Receiver receiver;

void dmxCallback(uint8_t universe, const uint8_t* data) {
  // getLastRssi() reports the signal strength (dBm) of the packet that
  // carried this frame; RSSI_UNKNOWN if the platform can't supply it.
  int8_t rssi = receiver.getLastRssi();
  Serial.printf("Received DMX universe %d (rssi=%d dBm) - first 8 values: %d %d %d %d %d %d %d %d\n",
                universe, rssi, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
}

void onEspNowReceive(const uint8_t *mac, const uint8_t *data, int len) {
  receiver.handleReceive(mac, data, len);
}

void setup() {
  Serial.begin(115200);
  receiver.begin();  // true by default = internal ESP-NOW init
  receiver.setDMXReceiveCallback(dmxCallback);

  // Sender pairing is on by default: the first ~10s after the first DMX
  // packet arrives are spent locking onto whichever sender has the
  // strongest signal, so dmxCallback() won't fire until that completes.
  // See README.md ("Sender Pairing") to tune or disable it.
}

void loop() {
  delay(10);
}
