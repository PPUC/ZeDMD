#ifndef ZEDMD_WIFI_TRANSPORT_H
#define ZEDMD_WIFI_TRANSPORT_H

#ifndef ZEDMD_NO_NETWORKING

#include <AsyncUDP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include "transport.h"

class WifiTransport final : public Transport {
public:
  WifiTransport();

  ~WifiTransport() override;

  bool init() override;

  bool deinit() override;

  bool loadConfig() override;

  bool saveConfig() override;

  bool loadDelay() override;

  bool saveDelay() override;

  String ssid = emptyString;
  String pwd = emptyString;
  uint16_t port = 3333;
  uint8_t ssid_length = 0;
  uint8_t pwd_length = 0;

private:
  void startServer();

  static void HandleUdpPacket(AsyncUDPPacket packet);

  static void HandleTcpData(void *arg, AsyncClient *client, void *data, size_t len);

  static void HandleTcpDisconnect(void *arg, AsyncClient *client);

  static void NewTcpClient(void *arg, AsyncClient *client);

  AsyncWebServer *server = nullptr;
  AsyncServer *tcp = nullptr;
  AsyncUDP *udp = nullptr;
  bool serverRunning = false;
};

#endif  // ZEDMD_NO_NETWORKING
#endif  // ZEDMD_WIFI_TRANSPORT_H
