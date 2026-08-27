#include "ESPNowDMX.h"

#include <cstring>

ESPNowDMX* ESPNowDMX::s_activeReceiver = nullptr;

ESPNowDMX::ESPNowDMX()
  : _mode(ESPNOW_DMX_MODE_SENDER), _initialized(false), _pendingUniverse(0) {}

bool ESPNowDMX::begin(ESPNowDMXMode mode, bool registerInternalEspNow) {
  _mode = mode;
  bool ok = false;

  if (mode == ESPNOW_DMX_MODE_SENDER) {
    if (s_activeReceiver == this) {
      s_activeReceiver = nullptr;
    }
    ok = _sender.begin(registerInternalEspNow);
    if (ok) {
      _sender.setUniverseId(_pendingUniverse);
    }
  } else {
    ok = _receiver.begin(registerInternalEspNow);
    if (ok) {
      _receiver.setUniverseId(_pendingUniverse);
      s_activeReceiver = this;
    }
  }

  _initialized = ok;
  return ok;
}

void ESPNowDMX::loop() {
  if (!_initialized) {
    return;
  }

  if (_mode == ESPNOW_DMX_MODE_SENDER) {
    _sender.loop();
  }
}

void ESPNowDMX::update() {
  loop();
}

void ESPNowDMX::setUniverseId(uint8_t universe) {
  _pendingUniverse = universe;

  if (!_initialized) {
    return;
  }

  if (_mode == ESPNOW_DMX_MODE_SENDER) {
    _sender.setUniverseId(_pendingUniverse);
  } else {
    _receiver.setUniverseId(_pendingUniverse);
  }
}

void ESPNowDMX::setChannel(uint16_t address, uint8_t value) {
  if (_initialized && _mode == ESPNOW_DMX_MODE_SENDER) {
    _sender.setChannel(address, value);
  }
}

void ESPNowDMX::sendDMXFrame(const uint8_t* data, size_t length) {
  if (!_initialized || _mode != ESPNOW_DMX_MODE_SENDER || data == nullptr) {
    return;
  }

  if (length >= DMX_UNIVERSE_SIZE) {
    _sender.setUniverse(data);
  } else {
    uint8_t buffer[DMX_UNIVERSE_SIZE];
    size_t copyLen = length;
    memcpy(buffer, data, copyLen);
    if (copyLen < DMX_UNIVERSE_SIZE) {
      memset(buffer + copyLen, 0, DMX_UNIVERSE_SIZE - copyLen);
    }
    _sender.setUniverse(buffer);
  }

  _sender.loop();
}

void ESPNowDMX::setReceiveCallback(DMXReceiveCallback cb) {
  if (_mode == ESPNOW_DMX_MODE_RECEIVER) {
    _receiver.setDMXReceiveCallback(cb);
  }
}

void ESPNowDMX::setDMXReceiveCallback(DMXReceiveCallback cb) {
  setReceiveCallback(cb);
}

bool ESPNowDMX::handleIncoming(const uint8_t* mac, const uint8_t* data, int len) {
  if (!_initialized || _mode != ESPNOW_DMX_MODE_RECEIVER) {
    return false;
  }
  return _receiver.handleReceive(mac, data, len);
}

void ESPNowDMX::forwardPacket(const uint8_t* mac, const uint8_t* data, int len) {
  if (s_activeReceiver) {
    s_activeReceiver->handleIncoming(mac, data, len);
  }
}

bool ESPNowDMX::addUnicastPeer(const uint8_t mac[6]) {
  if (_mode != ESPNOW_DMX_MODE_SENDER) return false;
  return _sender.addUnicastPeer(mac);
}

bool ESPNowDMX::removeUnicastPeer(const uint8_t mac[6]) {
  if (_mode != ESPNOW_DMX_MODE_SENDER) return false;
  return _sender.removeUnicastPeer(mac);
}

void ESPNowDMX::clearUnicastPeers() {
  if (_mode == ESPNOW_DMX_MODE_SENDER) {
    _sender.clearUnicastPeers();
  }
}

uint8_t ESPNowDMX::getUnicastPeerCount() const {
  return _mode == ESPNOW_DMX_MODE_SENDER ? _sender.getUnicastPeerCount() : 0;
}
