#ifndef ZEDMD_TRANSPORT_H
#define ZEDMD_TRANSPORT_H

#include <Arduino.h>

#include <cstdint>

class Transport {
 public:
  enum { USB = 0, WIFI_UDP = 1, WIFI_TCP = 2, SPI = 3 };

  Transport() = default;

  virtual ~Transport() = default;

  virtual bool init() { return true; }

  virtual bool deinit() { return true; }

  virtual bool isActive() { return m_active; }

  virtual uint8_t getType() { return m_type; }

  virtual void setType(const uint8_t type) { m_type = type; }

  virtual const char* getTypeString() {
    return m_type == USB        ? "USB     "
           : m_type == WIFI_UDP ? "WiFi UDP"
           : m_type == WIFI_TCP ? "WiFi TCP"
                                : "SPI     ";
  }

  virtual bool loadConfig() { return true; }

  virtual bool saveConfig() { return true; }

  virtual uint8_t getDelay() { return m_delay; }

  virtual void setDelay(const uint8_t delay) { m_delay = delay; }

  virtual bool loadDelay() { return true; }

  virtual bool saveDelay() { return true; }

  bool isUsb() const { return m_type == USB; }

  bool isWifi() const { return m_type == WIFI_UDP || m_type == WIFI_TCP; }

  bool isWifiAndActive() const {
    return m_active && (m_type == WIFI_UDP || m_type == WIFI_TCP);
  }

  bool isSpi() const { return m_type == SPI; }

 protected:
  uint8_t m_type = USB;
  bool m_active = false;
  uint8_t m_delay = 5;
};

#endif  // ZEDMD_TRANSPORT_H