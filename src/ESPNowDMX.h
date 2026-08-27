#pragma once

#include <cstddef>

#include "ESPNowDMX_Common.h"
#include "ESPNowDMX_Sender.h"
#include "ESPNowDMX_Receiver.h"

enum ESPNowDMXMode : uint8_t {
	ESPNOW_DMX_MODE_SENDER = 0,
	ESPNOW_DMX_MODE_RECEIVER = 1,
};

class ESPNowDMX {
public:
	ESPNowDMX();

	bool begin(ESPNowDMXMode mode, bool registerInternalEspNow = true);
	void loop();
	void update();

	void setUniverseId(uint8_t universe);
	void setChannel(uint16_t address, uint8_t value);
	void sendDMXFrame(const uint8_t* data, size_t length);

	// Sender mode only; no-op in receiver mode. See ESPNowDMX_Sender for
	// details - 0 disables periodic full refresh, default 200 ms.
	void setFullRefreshInterval(unsigned long intervalMs);

	void setReceiveCallback(DMXReceiveCallback cb);
	void setDMXReceiveCallback(DMXReceiveCallback cb);

	bool handleIncoming(const uint8_t* mac, const uint8_t* data, int len, int8_t rssi = RSSI_UNKNOWN);
	static void forwardPacket(const uint8_t* mac, const uint8_t* data, int len, int8_t rssi = RSSI_UNKNOWN);

	// Receiver mode only: RSSI (dBm) of the most recently received DMX
	// packet for the current universe. RSSI_UNKNOWN in sender mode or if
	// nothing has been received yet.
	int8_t getLastRssi() const;

	// Unicast peer management (sender mode only). Once one or more
	// unicast peers are registered, DMX is sent directly to them
	// instead of being broadcast. No-ops (return false) in receiver mode.
	bool addUnicastPeer(const uint8_t mac[6]);
	bool removeUnicastPeer(const uint8_t mac[6]);
	void clearUnicastPeers();
	uint8_t getUnicastPeerCount() const;

private:
	ESPNowDMXMode _mode;
	bool _initialized;
	uint8_t _pendingUniverse;
	ESPNowDMX_Sender _sender;
	ESPNowDMX_Receiver _receiver;

	static ESPNowDMX* s_activeReceiver;
};
