#ifndef ZEDMD_NO_NETWORKING

#include "wifi_transport.h"

#include <LittleFS.h>
#include <WiFi.h>
#ifdef PICO_BUILD
#include <bearssl/bearssl_hash.h>
#include <bearssl/bearssl_hmac.h>
#include <cyw43.h>
#include <lwip/dhcp.h>
#include <lwip/netif.h>
#include <lwip_wrap.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "pico/zedmd_pico.h"

static bool StartUdpProcessing();
#endif

#include "main.h"
#include "panel.h"
#include "utility/clock.h"
#include "version.h"

#ifdef PICO_BUILD
// Calls into the CYW43 driver (cyw43_ioctl(), so also WiFi.RSSI() and
// WiFi.noLowPowerMode()) run in the lwIP thread, serialized with the driver's
// own polling of the chip. lwip_callback() runs a function there and waits.
// They also must not overlap with heavy traffic: a chip call that's waiting
// for the chip's answer while packets flood in can leave the radio
// unresponsive. A stream can start at any time, so ZeDMD only talks to the
// chip during init(), before any traffic, and in deinit() right before a
// restart. Web handlers show the RSSI read in init().
static int32_t rssiAtConnect = 0;
static int32_t channelAtConnect = 0;

static void ReadRssi(void*) {
  rssiAtConnect = WiFi.RSSI();
  channelAtConnect = WiFi.channel();
}

static void DisablePowerSave(void*) { WiFi.noLowPowerMode(); }

// begin() runs in the lwIP thread too. From another task, the core's join loop
// waits for a state the lwIP thread moves past right away, so it blocks for
// its full 15 s timeout. In the lwIP thread it returns as soon as the join is
// sent, and the thread completes the join and DHCP in the background.
struct WifiCredentials {
  const char* ssid;
  const char* password;
  bool ok;
};

static void BeginStation(void* arg) {
  auto* c = static_cast<WifiCredentials*>(arg);
  WiFi.beginNoBlock(c->ssid, c->password);
  // begin() reports the link as up right away, so DHCP starts sending before
  // WiFi is connected, those requests go nowhere, and by the time WiFi does
  // connect its retries have backed off to 8 s. Mark the link down instead:
  // the driver sets it up once WiFi is connected, which makes DHCP start
  // over immediately.
  for (struct netif* n = netif_list; n; n = n->next) {
    if (netif_dhcp_data(n)) netif_set_link_down(n);
  }
}

// The WPA key (PMK) as 64 hex digits, computed from the passphrase like the
// router does: PBKDF2-HMAC-SHA1(passphrase, SSID, 4096 rounds, 32 bytes).
// Given a passphrase, the CYW43 derives the key itself on its own slow CPU,
// which delayed connecting by about 2 s. Given the 64 hex digits, it uses them
// as the key directly. Takes about 0.3 s on the RP2350.
static String WpaKeyHex(const String& passphrase, const String& ssid) {
  br_hmac_key_context key;
  br_hmac_key_init(&key, &br_sha1_vtable, passphrase.c_str(),
                   passphrase.length());
  uint8_t pmk[40];  // two 20-byte SHA-1 blocks, the first 32 bytes are used
  for (uint8_t block = 1; block <= 2; block++) {
    uint8_t u[20];
    br_hmac_context hmac;
    br_hmac_init(&hmac, &key, 0);
    br_hmac_update(&hmac, ssid.c_str(), ssid.length());
    const uint8_t index[4] = {0, 0, 0, block};
    br_hmac_update(&hmac, index, sizeof(index));
    br_hmac_out(&hmac, u);
    uint8_t* t = &pmk[(block - 1) * 20];
    memcpy(t, u, 20);
    for (uint16_t i = 1; i < 4096; i++) {
      br_hmac_init(&hmac, &key, 0);
      br_hmac_update(&hmac, u, 20);
      br_hmac_out(&hmac, u);
      for (uint8_t j = 0; j < 20; j++) t[j] ^= u[j];
    }
  }
  char hex[65];
  for (uint8_t i = 0; i < 32; i++) sprintf(&hex[i * 2], "%02x", pmk[i]);
  return String(hex);
}

// begin() in the lwIP thread returns right after sending the join, so nothing
// retries when the network isn't found or the join fails. Send it again then.
static void RetryJoinIfFailed(const String& ssid, const String& key) {
  if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) >= 0) return;
  // The core runs cyw43_wifi_join() in the lwIP thread.
  cyw43_wifi_join(&cyw43_state, ssid.length(), (const uint8_t*)ssid.c_str(),
                  key.length(), (const uint8_t*)key.c_str(),
                  key.length() ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN,
                  nullptr, CYW43_CHANNEL_NONE);
}

static void BeginAccessPoint(void* arg) {
  auto* c = static_cast<WifiCredentials*>(arg);
  c->ok = WiFi.softAP(c->ssid, c->password);
}
#endif

// RSSI for code that may run in the lwIP thread, like web handlers. On the
// Pico it's the value read when WiFi connected (see ReadRssi).
static int32_t CurrentRssi() {
#ifdef PICO_BUILD
  return rssiAtConnect;
#else
  return WiFi.RSSI();
#endif
}

// Starts the access point. Returns 0.0.0.0 if it didn't come up.
static IPAddress StartAccessPoint(const char* ssid, const char* password) {
#ifdef PICO_BUILD
  // In the lwIP thread, see BeginStation.
  WifiCredentials credentials = {ssid, password, false};
  lwip_callback(BeginAccessPoint, &credentials);
  if (!credentials.ok) return IPAddress();
  // On the Pico softAP() succeeds even if the radio never powered up, so
  // check that the AP interface is actually up.
  if (!(cyw43_state.itf_state & (1 << CYW43_ITF_AP))) return IPAddress();
#else
  if (!WiFi.softAP(ssid, password)) return IPAddress();
#endif
  return WiFi.softAPIP();
}

WifiTransport::WifiTransport() : Transport() { m_type = WIFI_UDP; }

WifiTransport::~WifiTransport() { deinit(); }

bool WifiTransport::init() {
  char apSSID[16];
  sprintf(apSSID, "ZeDMD-WiFi-%04X", shortId);
  const char* apPassword = "zedmd1234";
  bool softAPFallback = false;
  IPAddress ip;

  if (ssid_length > 0) {
    WiFi.disconnect(true);
#ifdef PICO_BUILD
    const String staSsid = ssid.substring(0, ssid_length);
    const String staPassword = pwd.substring(0, pwd_length);
    // Join with the computed key, see WpaKeyHex. A 64-digit password already
    // is the key.
    const String staKey =
        staPassword.length() >= 8 && staPassword.length() <= 63
            ? WpaKeyHex(staPassword, staSsid)
            : staPassword;
    // In the lwIP thread, see BeginStation.
    WifiCredentials credentials = {staSsid.c_str(), staKey.c_str(), false};
    lwip_callback(BeginStation, &credentials);
    auto retryJoin = [&]() { RetryJoinIfFailed(staSsid, staKey); };
#else
    WiFi.begin(ssid.substring(0, ssid_length).c_str(),
               pwd.substring(0, pwd_length).c_str());
    auto retryJoin = []() {};
#endif

    // Don't use WiFi.waitForConnectResult(10000) here, it blocks the menu
    // button.
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
      CheckMenuButton();
      retryJoin();
      vTaskDelay(pdMS_TO_TICKS(100));  // FreeRTOS delay, avoids blocking
    }

    if (WiFi.status() != WL_CONNECTED) {
      GetDisplayDriver()->DisplayText("No WiFi connection, error ", 10,
                                      TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
      DisplayNumber(WiFi.status(), 2, 26 * 4 + 10, TOTAL_HEIGHT / 2 - 9, 255, 0,
                    0);
      GetDisplayDriver()->DisplayText("Trying again ...", 10,
                                      TOTAL_HEIGHT / 2 - 3, 255, 0, 0);
      // The Pico panel is double buffered; Render() is a no-op on ESP32.
      GetDisplayDriver()->Render();
      // second try
      startTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        CheckMenuButton();
        retryJoin();
        vTaskDelay(pdMS_TO_TICKS(100));  // FreeRTOS delay, avoids blocking
      }
      if (WiFi.status() != WL_CONNECTED) {
        softAPFallback = true;
      }
    }
  } else {
    // Don't use the fallback to skip the countdown.
    ip = StartAccessPoint(apSSID, apPassword);
    captivePortal = true;
  }

  if (!softAPFallback && WiFi.getMode() == WIFI_STA) {
    ip = WiFi.localIP();
  }

  if (ip[0] == 0 || softAPFallback) {
    GetDisplayDriver()->DisplayText("No WiFi connection, maybe     ", 10,
                                    TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
    GetDisplayDriver()->DisplayText("the credentials are wrong.", 10,
                                    TOTAL_HEIGHT / 2 - 3, 255, 0, 0);
    GetDisplayDriver()->DisplayText("Start AP in 20 seconds ...", 10,
                                    TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
    GetDisplayDriver()->Render();
    for (uint8_t i = 19; i > 0; i--) {
      CheckMenuButton();
      vTaskDelay(pdMS_TO_TICKS(1000));
      DisplayNumber(i, 2, 58, TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
      GetDisplayDriver()->Render();
    }
    ip = StartAccessPoint(apSSID, apPassword);
    captivePortal = true;
  }

  if (ip[0] == 0) {
    // Keep polling the menu button so the user can switch back to USB.
    ClearScreen();
    GetDisplayDriver()->DisplayText("WIFI COULD NOT BE STARTED.", 0,
                                    TOTAL_HEIGHT / 2 - 6, 255, 0, 0);
    GetDisplayDriver()->DisplayText("PRESS MENU TO CHANGE MODE.", 0,
                                    TOTAL_HEIGHT / 2, 255, 0, 0);
    GetDisplayDriver()->Render();
    while (true) {
      CheckMenuButton();
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  ClearScreen();
  DisplayLogo();
  DisplayId();

  for (uint8_t i = 0; i < 4; i++) {
    if (i > 0) GetDisplayDriver()->DrawPixel(i * 3 * 4 + i * 2 - 2, 4, 0);
    DisplayNumber(ip[i], 3, i * 3 * 4 + i * 2, 0, 0, 0, 0, true);
  }

#ifdef PICO_BUILD
  // Same as setSleep(false) on ESP32, run in the lwIP thread (see ReadRssi).
  // TX power stays at the chip's default: its power limit can only lower
  // that. wifiPower is ESP32 only.
  lwip_callback(DisablePowerSave, nullptr);
  // In AP mode the Pico reports WL_CONNECTED too, so check the mode.
  if (WiFi.getMode() == WIFI_STA) lwip_callback(ReadRssi, nullptr);
#else
  WiFi.setSleep(false);  // WiFi speed improvement on ESP32 S3 and others.

  WiFi.setTxPower((wifi_power_t)wifiPower);
#endif

  if (debug) {
    GetDisplayDriver()->DisplayText("WiFi RSSI: ", 0, TOTAL_HEIGHT / 2 - 9, 255,
                                    0, 0);
    DisplayNumber(CurrentRssi(), 3, 11 * 4, TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
#ifndef PICO_BUILD
    GetDisplayDriver()->DisplayText("TX Power:  ", 0, TOTAL_HEIGHT / 2 - 3, 255,
                                    0, 0);
    DisplayNumber(WiFi.getTxPower(), 3, 11 * 4, TOTAL_HEIGHT / 2 - 3, 255, 0,
                  0);
#endif
    GetDisplayDriver()->DisplayText("Channel:   ", 0, TOTAL_HEIGHT / 2 + 3, 255,
                                    0, 0);
#ifdef PICO_BUILD
    DisplayNumber(channelAtConnect, 3, 11 * 4, TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
#else
    DisplayNumber(WiFi.channel(), 3, 11 * 4, TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
#endif
  }

  m_active = true;

  // Start the MDNS server for easy detection
  if (!MDNS.begin("zedmd-wifi")) {
    GetDisplayDriver()->DisplayText("MDNS could not be started", 0, 0, 255, 0,
                                    0);
    while (1);
  }

  GetDisplayDriver()->DisplayText("zedmd-wifi.local", 0, TOTAL_HEIGHT - 5, 0, 0,
                                  0, true);
  GetDisplayDriver()->Render();

  if (captivePortal) startCaptivePortalDns(ip);

  startServer();

  if (m_type == WIFI_UDP) {
#ifdef PICO_BUILD
    if (!StartUdpProcessing()) {
      GetDisplayDriver()->DisplayText("UDP server could not be started", 0, 0,
                                      255, 0, 0);
      GetDisplayDriver()->Render();
      while (1);
    }
#endif
    udp = new AsyncUDP();
    udp->onPacket(HandleUdpPacket);
    if (!udp->listen(ip, port)) {
      GetDisplayDriver()->DisplayText("UDP server could not be started", 0, 0,
                                      255, 0, 0);
      while (1);
    }
  } else {
    tcp = new AsyncServer(port);
    tcp->setNoDelay(true);
    tcp->onClient(&NewTcpClient, tcp);
    tcp->begin();
  }

  return true;
}

// Answers a DNS query: A queries for any name get the access point's IP,
// all other types get an empty NOERROR reply. The DNSServer libraries answer
// AAAA queries with NXDOMAIN ("name doesn't exist"), which makes phones skip
// their captive portal check instead of opening the setup page.
static void HandleDnsQuery(AsyncUDPPacket& packet, const IPAddress& ip) {
  const uint8_t* query = packet.data();
  const size_t len = packet.length();
  // Header is 12 bytes; only handle standard queries with one question.
  if (len < 12 || (query[2] & 0xF8) != 0 || query[4] != 0 || query[5] != 1)
    return;

  // Skip the question name (labels ending in a zero byte), type and class.
  size_t pos = 12;
  while (pos < len && query[pos] != 0) {
    if (query[pos] & 0xC0) return;  // no compression in questions
    pos += query[pos] + 1;
  }
  const size_t questionEnd = pos + 1 + 4;
  if (questionEnd > len) return;
  const uint16_t qtype = query[pos + 1] << 8 | query[pos + 2];
  const bool answerA = qtype == 1 || qtype == 255;  // A or ANY

  uint8_t response[512];
  if (questionEnd + 16 > sizeof(response)) return;
  memcpy(response, query, questionEnd);
  response[2] = 0x84 | (query[2] & 0x01);  // response, authoritative, RD
  response[3] = 0x80;                      // recursion available, NOERROR
  response[6] = 0;
  response[7] = answerA ? 1 : 0;  // answers
  memset(&response[8], 0, 4);     // no authority or additional records

  size_t out = questionEnd;
  if (answerA) {
    const uint8_t answer[] = {
        0xC0, 0x0C,              // name: pointer to the question
        0x00, 0x01, 0x00, 0x01,  // type A, class IN
        0x00, 0x00, 0x00, 0x3C,  // TTL 60 seconds
        0x00, 0x04,              // 4 bytes of address
        ip[0], ip[1], ip[2], ip[3]};
    memcpy(&response[out], answer, sizeof(answer));
    out += sizeof(answer);
  }
  packet.write(response, out);
}

void WifiTransport::startCaptivePortalDns(const IPAddress& ip) {
  dns = new AsyncUDP();
  if (!dns->listen(53)) return;
  dns->onPacket([ip](AsyncUDPPacket& packet) { HandleDnsQuery(packet, ip); });
}

#ifdef PICO_BUILD
// Leaves the network, so the router drops the connection right away. Without
// that, the router still has the old connection when the Pico comes back, and
// its first key exchange then times out: WiFi took ~11 s instead of ~5.5 s.
static SemaphoreHandle_t networkLeft = nullptr;
extern "C" bool __isLWIPThread();  // arduino-pico core, FreeRTOS builds

static void Task_LeaveNetwork(void* pvParameters) {
  cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);  // the core runs it in lwIP
  vTaskDelay(pdMS_TO_TICKS(100));  // let the radio send the disassociation
  xSemaphoreGive(networkLeft);
  vTaskDelete(nullptr);
}
#endif

bool WifiTransport::deinit() {
#ifdef PICO_BUILD
  // Only called before a reboot, which resets the radio, so leaving the
  // network is all there is to do. It goes through the lwIP thread, so it
  // runs in its own task with a time limit: a stuck network stack must not
  // block the restart (and with it the settings menu).
  if (m_active && (WiFi.getMode() & WIFI_STA)) {
    if (__isLWIPThread()) {
      // Restart requested from a web handler: we are the lwIP thread.
      cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
      vTaskDelay(pdMS_TO_TICKS(100));  // let the radio send it
    } else {
      networkLeft = xSemaphoreCreateBinary();
      if (networkLeft &&
          xTaskCreate(Task_LeaveNetwork, "Task_LeaveNetwork", 512, nullptr, 1,
                      nullptr) == pdPASS) {
        xSemaphoreTake(networkLeft, pdMS_TO_TICKS(500));
      }
    }
  }
  m_active = false;
  return true;
#else
  if (dns) dns->close();
  MDNS.end();
  const bool ret = WiFi.disconnect(true);
  m_active = false;
  return ret;
#endif
}

bool WifiTransport::loadConfig() {
  File wifiConfig = LittleFS.open("/wifi_config.txt", "r");
  if (!wifiConfig) return false;

  while (wifiConfig.available()) {
    ssid = wifiConfig.readStringUntil('\n');
    ssid_length = wifiConfig.readStringUntil('\n').toInt();
    pwd = wifiConfig.readStringUntil('\n');
    pwd_length = wifiConfig.readStringUntil('\n').toInt();
    port = wifiConfig.readStringUntil('\n').toInt();
    if (wifiConfig.available()) {  // Older configs have no power line.
      // Configs saved while the power default was missing hold 0, which
      // isn't a valid power (ESP32 clamps it to the minimum), so keep the
      // default then.
      const uint8_t power = wifiConfig.readStringUntil('\n').toInt();
      if (power != 0) wifiPower = power;
    }
  }

  wifiConfig.close();

  return true;
}

bool WifiTransport::saveConfig() {
  File wifiConfig = LittleFS.open("/wifi_config.txt", "w");
  if (!wifiConfig) return false;

  wifiConfig.println(ssid);
  wifiConfig.println(String(ssid_length));
  wifiConfig.println(pwd);
  wifiConfig.println(String(pwd_length));
  wifiConfig.println(String(port));
  wifiConfig.println(String(wifiPower));
  wifiConfig.close();

  return true;
}

bool WifiTransport::loadDelay() {
  File f = LittleFS.open("/udp_delay.val", "r");
  if (!f) {
    return saveDelay();
  }

  m_delay = f.read();
  f.close();

  return true;
}

bool WifiTransport::saveDelay() {
  File f = LittleFS.open("/udp_delay.val", "w");
  if (!f) return false;

  f.write(m_delay);
  f.close();

  return true;
}

// Shown on /debug_info to judge packet loss.
static volatile uint32_t udpPacketsReceived = 0;
static volatile uint32_t udpPacketsDropped = 0;

#ifdef PICO_BUILD
// On the Pico, UDP callbacks run in the lwIP thread, which also reads incoming
// packets from the WiFi chip. HandleData() can wait there while ZeDMD's
// buffers are full, and meanwhile the chip drops packets. So the callback
// only copies each datagram into a slot, and Task_ProcessUdp runs
// HandleData() on the other core.
namespace {
constexpr size_t kUdpSlotSize = 1472;  // largest UDP payload at 1500 MTU
constexpr size_t kUdpSlotCount = 16;

struct UdpSlot {
  uint16_t length;
  uint8_t data[kUdpSlotSize];
};

QueueHandle_t udpFreeSlots = nullptr;  // slots ready to be filled
QueueHandle_t udpFullSlots = nullptr;  // slots waiting for HandleData()
}  // namespace

static void Task_ProcessUdp(void* pvParameters) {
  UdpSlot* slot;
  while (true) {
    if (xQueueReceive(udpFullSlots, &slot, portMAX_DELAY) != pdTRUE) continue;
    transportActive = true;
    HandleData(slot->data, slot->length);
    xQueueSend(udpFreeSlots, &slot, 0);
  }
}

static bool StartUdpProcessing() {
  auto* slots = static_cast<UdpSlot*>(malloc(sizeof(UdpSlot) * kUdpSlotCount));
  udpFreeSlots = xQueueCreate(kUdpSlotCount, sizeof(UdpSlot*));
  udpFullSlots = xQueueCreate(kUdpSlotCount, sizeof(UdpSlot*));
  if (!slots || !udpFreeSlots || !udpFullSlots) return false;

  for (size_t i = 0; i < kUdpSlotCount; i++) {
    UdpSlot* slot = &slots[i];
    xQueueSend(udpFreeSlots, &slot, 0);
  }
  // Same core and priority as the USB transport's reader task.
  return xTaskCreatePinnedToCore(Task_ProcessUdp, "Task_ProcessUdp", 2048,
                                 nullptr, 1, nullptr, 0) == pdPASS;
}
#endif

void WifiTransport::HandleUdpPacket(AsyncUDPPacket& packet) {
  udpPacketsReceived = udpPacketsReceived + 1;

#ifdef PICO_BUILD
  // libzedmd sends at most 1400 bytes per datagram. Larger ones would have to
  // be handled here, next to Task_ProcessUdp, but HandleData() can't run
  // twice at once, so they're dropped.
  UdpSlot* slot;
  if (packet.length() > kUdpSlotSize ||
      xQueueReceive(udpFreeSlots, &slot, 0) != pdTRUE) {
    // HandleData() is behind; dropping here keeps the radio drained.
    udpPacketsDropped = udpPacketsDropped + 1;
    return;
  }
  slot->length = packet.length();
  memcpy(slot->data, packet.data(), slot->length);
  xQueueSend(udpFullSlots, &slot, 0);
#else
  static bool isProcessing = false;

  if (!isProcessing) {
    isProcessing = true;
    transportActive = true;
    HandleData(packet.data(), packet.length());
    yield();
    isProcessing = false;
  }
#endif
}

void WifiTransport::HandleTcpData(void* arg, AsyncClient* client, void* data,
                                  size_t len) {
  // Like UDP: if the main loop timed the connection out while the client was
  // quiet, new data makes it active again. Otherwise nothing empties the
  // buffers, and HandleData() blocks the network thread for good.
  transportActive = true;
  HandleData(static_cast<uint8_t*>(data), len);
  client->ack(len);
}

void WifiTransport::HandleTcpDisconnect(void* arg, AsyncClient* client) {
  delete client;
  MarkCurrentBufferDone();
  AcquireNextBuffer();
  bufferSizes[currentBuffer] = 2;
  buffers[currentBuffer][0] = 0;
  buffers[currentBuffer][1] = 0;
  MarkCurrentBufferDone();
  ClearScreen();
  payloadMissing = 0;
  headerBytesReceived = 0;
  numCtrlCharsFound = 0;
  delay(100);
  transportActive = false;
}

void WifiTransport::NewTcpClient(void* arg, AsyncClient* client) {
  if (transportActive) {
    client->close();
    delete client;
    return;
  }
  payloadMissing = 0;
  headerBytesReceived = 0;
  numCtrlCharsFound = 0;
  // The clock still holds the time of the last frame, possibly long ago, so
  // the main loop would drop the new connection before its first keep-alive.
  lastDataReceivedClock.restart();
  transportActive = true;
  client->setNoDelay(true);
  client->setAckTimeout(2);
  client->onData(&HandleTcpData, nullptr);
  client->onDisconnect(&HandleTcpDisconnect, nullptr);
}

#ifdef PICO_BUILD
static void Task_DeferredRestart(void* pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(300));
  Restart();
}
#endif

// For web handlers. On the Pico they run in the lwIP thread, so restarting
// right away would block it before the response is sent, and the browser
// would only see a timeout. So answer first and restart from another task.
static void RestartAfterResponse() {
#ifdef PICO_BUILD
  xTaskCreate(Task_DeferredRestart, "Task_DeferredRestart", 1024, nullptr, 1,
              nullptr);
#else
  Restart();
#endif
}

void WifiTransport::startServer() {
  server = new AsyncWebServer(80);

  // Serve index.html
  server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/index.html", String(), false);
  });

  // Handle AJAX request to save WiFi configuration
  server->on("/save_wifi", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (request->hasParam("ssid", true) &&
        request->hasParam("password", true) &&
        request->hasParam("port", true)) {
      ssid = request->getParam("ssid", true)->value();
      pwd = request->getParam("password", true)->value();
      port = request->getParam("port", true)->value().toInt();
      ssid_length = ssid.length();
      pwd_length = pwd.length();

      const bool success = saveConfig();
      if (success) {
        request->send(200, "text/plain", "Config saved successfully!");
        RestartAfterResponse();
      } else {
        request->send(500, "text/plain", "Failed to save config!");
      }
    } else {
      request->send(400, "text/plain", "Missing parameters!");
    }
  });

  server->on("/wifi_status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String jsonResponse;
    // The Pico reports WL_CONNECTED in access point mode too.
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
      const int8_t rssi = CurrentRssi();
      const IPAddress ip = WiFi.localIP();  // Get the local IP address

      jsonResponse = R"({"connected":true,"ssid":")" + WiFi.SSID() +
                     R"(","signal":)" + String(rssi) + "," + R"("ip":")" +
                     ip.toString() + "\"," + "\"port\":" + String(port) + "}";
    } else {
      jsonResponse = R"({"connected":false})";
    }

    request->send(200, "application/json", jsonResponse);
  });

#ifndef DISPLAY_RM67162_AMOLED
  // Route to save RGB order
  server->on("/save_rgb_order", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("rgbOrder", true)) {
      if (rgbModeLoaded != 0) {
        request->send(200, "text/plain",
                      "ZeDMD needs to reboot first before the RGB order can be "
                      "adjusted. Try again in a few seconds.");

        rgbMode = 0;
        SaveRgbOrder();
        RestartAfterResponse();
      }

      const String rgbOrderValue = request->getParam("rgbOrder", true)->value();
      rgbMode =
          rgbOrderValue.toInt();  // Convert to integer and set the RGB mode
      SaveRgbOrder();
      RefreshSetupScreen();
      request->send(200, "text/plain", "RGB order updated successfully");
    } else {
      request->send(400, "text/plain", "Missing RGB order parameter");
    }
  });
#endif

  // Route to save brightness
  server->on("/save_brightness", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("brightness", true)) {
      const String brightnessValue =
          request->getParam("brightness", true)->value();
      brightness = brightnessValue.toInt();
      GetDisplayDriver()->SetBrightness(brightness);
      SaveLum();
      RefreshSetupScreen();
      request->send(200, "text/plain", "Brightness updated successfully");
    } else {
      request->send(400, "text/plain", "Missing brightness parameter");
    }
  });

  server->on("/get_version", HTTP_GET, [](AsyncWebServerRequest* request) {
    const String version = String(ZEDMD_VERSION_MAJOR) + "." +
                           String(ZEDMD_VERSION_MINOR) + "." +
                           String(ZEDMD_VERSION_PATCH);
    request->send(200, "text/plain", version);
  });

  server->on("/get_height", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(TOTAL_HEIGHT));
  });

  server->on("/get_width", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(TOTAL_WIDTH));
  });
#ifndef DISPLAY_RM67162_AMOLED
  server->on("/get_rgb_order", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(rgbMode));
  });

  server->on("/get_panel_clkphase", HTTP_GET,
             [](AsyncWebServerRequest* request) {
               request->send(200, "text/plain", String(panelClkphase));
             });

  server->on("/get_panel_driver", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(panelDriver));
  });

  server->on("/get_panel_i2sspeed", HTTP_GET,
             [](AsyncWebServerRequest* request) {
               request->send(200, "text/plain", String(panelI2sspeed));
             });

  server->on("/get_panel_latchblanking", HTTP_GET,
             [](AsyncWebServerRequest* request) {
               request->send(200, "text/plain", String(panelLatchBlanking));
             });

  server->on("/get_panel_minrefreshrate", HTTP_GET,
             [](AsyncWebServerRequest* request) {
               request->send(200, "text/plain", String(panelMinRefreshRate));
             });

  server->on("/get_y_offset", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(yOffset));
  });
#endif
  server->on("/get_udp_delay", HTTP_GET,
             [this](AsyncWebServerRequest* request) {
               request->send(200, "text/plain", String(m_delay));
             });

  server->on("/get_brightness", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(brightness));
  });

  server->on("/get_protocol", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (m_type == WIFI_UDP) {
      request->send(200, "text/plain", "UDP");
    } else {
      request->send(200, "text/plain", "TCP");
    }
  });

  server->on("/get_port", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(port));
  });

  server->on(
      "/get_usb_package_size", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", String(usbPackageSizeMultiplier * 32));
      });

  server->on("/get_ssid", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", ssid);
  });

  server->on("/get_s3", HTTP_GET, [](AsyncWebServerRequest* request) {
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED) || \
    defined(PICO_BUILD)
    request->send(200, "text/plain", String(1));
#else
    request->send(200, "text/plain", String(0));
#endif
  });

  server->on("/get_short_id", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(shortId));
  });

  server->on("/handshake", HTTP_GET, [this](AsyncWebServerRequest* request) {
    request->send(200, "text/plain",
                  String(TOTAL_WIDTH) + "|" + String(TOTAL_HEIGHT) + "|" +
                      String(ZEDMD_VERSION_MAJOR) + "." +
                      String(ZEDMD_VERSION_MINOR) + "." +
                      String(ZEDMD_VERSION_PATCH) + "|" +
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED) || \
    defined(PICO_BUILD)
                      String(1)
#else
        String(0)
#endif
                      + "|" + (m_type == WIFI_UDP ? "UDP" : "TCP") + "|" +
                      String(port) + "|" + String(m_delay) + "|" +
                      String(usbPackageSizeMultiplier * 32) + "|" +
                      String(brightness) + "|" +
#ifndef DISPLAY_RM67162_AMOLED
                      String(rgbMode) + "|" + String(panelClkphase) + "|" +
                      String(panelDriver) + "|" + String(panelI2sspeed) + "|" +
                      String(panelLatchBlanking) + "|" +
                      String(panelMinRefreshRate) + "|" + String(yOffset)
#else
        "0|0|0|0|0|0|0"
#endif
                      + "|" + ssid + "|" +
#ifdef ZEDMD_HD_HALF
                      "1"
#else
            "0"
#endif
                      + "|" + String(shortId) + "|" + String(wifiPower) + "|" +
#if defined(ARDUINO_ESP32_S3_N16R8)
                      "1"  // ESP32 S3
#elif defined(DISPLAY_RM67162_AMOLED)
            "2"  // ESP32 S3 with RM67162
#elif defined(PICO_BUILD)
#ifdef BOARD_HAS_PSRAM
            "3"  // RP2350
#else
            "4"  // RP2040
#endif
#else
            "0"  // ESP32
#endif
                      + "|" +
#ifndef DISPLAY_RM67162_AMOLED
                      String(panelLineDecoder)
#else
            "0"
#endif
                      );
  });

  server->on("/ppuc.png", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/ppuc.png", "image/png");
  });

  server->on("/reset_wifi", HTTP_POST, [](AsyncWebServerRequest* request) {
    LittleFS.remove("/wifi_config.txt");  // Remove Wi-Fi config
    request->send(200, "text/plain", "Wi-Fi reset successful.");
    RestartAfterResponse();
  });

  server->on("/apply", HTTP_POST, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Apply successful.");
    RestartAfterResponse();
  });

  // Serve debug information
  server->on("/debug_info", HTTP_GET, [](AsyncWebServerRequest* request) {
    String debugInfo = "IP Address: " + WiFi.localIP().toString() + "\n";
    debugInfo += "SSID: " + WiFi.SSID() + "\n";
#ifdef PICO_BUILD
    debugInfo += "RSSI (at connect): " + String(CurrentRssi()) + "\n";
    debugInfo += "Heap Free: " + String(rp2040.getFreeHeap()) + " bytes\n";
#else
    debugInfo += "RSSI: " + String(CurrentRssi()) + "\n";
    debugInfo += "TX Power setting: " + String(wifiPower / 4.0f, 2) + " dBm\n";
    debugInfo += "TX Power actual: " +
                 String(WiFi.getTxPower() / 4.0f, 2) + " dBm\n";
    debugInfo += "Power save: " + String(WiFi.getSleep() ? "on" : "off") + "\n";
    debugInfo += "Heap Free: " + String(ESP.getFreeHeap()) + " bytes\n";
#endif
    debugInfo += "Uptime: " + String(millis() / 1000) + " seconds\n";
    debugInfo += "UDP packets received: " + String(udpPacketsReceived) + "\n";
#ifdef PICO_BUILD
    debugInfo += "UDP packets dropped (ZeDMD behind): " +
                 String(udpPacketsDropped) + "\n";
#endif
    // Add more here if you need it
    request->send(200, "text/plain", debugInfo);
  });

  // Route to return the current settings as JSON
  server->on("/get_config", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String trimmedSsid = ssid;
    trimmedSsid.trim();

    String json = "{";
    json += R"("ssid":")" + trimmedSsid + "\",";
    json += R"("port":)" + String(port) + ",";
#ifndef DISPLAY_RM67162_AMOLED
    json += R"("rgbOrder":)" + String(rgbMode) + ",";
#endif
    json += R"("brightness":)" + String(brightness) + ",";
    json +=
        R"("scaleMode":)" + String(GetDisplayDriver()->GetCurrentScalingMode());
    json += "}";
    request->send(200, "application/json", json);
  });

  server->on(
      "/get_scaling_modes", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!GetDisplayDriver()) {
          request->send(500, "application/json",
                        R"({"error":"Display object not initialized"})");
          return;
        }

        String jsonResponse;
        if (GetDisplayDriver()->HasScalingModes()) {
          jsonResponse = "{";
          jsonResponse += R"("hasScalingModes":true,)";

          // Fetch current scaling mode
          const uint8_t currentMode =
              GetDisplayDriver()->GetCurrentScalingMode();
          jsonResponse += R"("currentMode":)" + String(currentMode) + ",";

          // Add the list of available scaling modes
          jsonResponse += R"("modes":[)";
          const char** scalingModes = GetDisplayDriver()->GetScalingModes();
          const uint8_t modeCount = GetDisplayDriver()->GetScalingModeCount();
          for (uint8_t i = 0; i < modeCount; i++) {
            jsonResponse += "\"" + String(scalingModes[i]) + "\"";
            if (i < modeCount - 1) {
              jsonResponse += ",";
            }
          }
          jsonResponse += "]";
          jsonResponse += "}";
        } else {
          jsonResponse = R"({"hasScalingModes":false})";
        }

        request->send(200, "application/json", jsonResponse);
      });

  // POST request to save the selected scaling mode
  server->on(
      "/save_scaling_mode", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!GetDisplayDriver()) {
          request->send(500, "text/plain", "Display object not initialized");
          return;
        }

        if (request->hasParam("scalingMode", true)) {
          const String scalingModeValue =
              request->getParam("scalingMode", true)->value();
          const uint8_t scalingMode = scalingModeValue.toInt();

          // Update the scaling mode using the global display object
          GetDisplayDriver()->SetCurrentScalingMode(scalingMode);
          SaveScale();
          request->send(200, "text/plain", "Scaling mode updated successfully");
        } else {
          request->send(400, "text/plain", "Missing scaling mode parameter");
        }
      });

  if (captivePortal) {
    // After joining a network, phones and laptops fetch a known URL (e.g.
    // /generate_204, /hotspot-detect.html). Redirecting those to the setup
    // page makes them show it right away.
    server->onNotFound([](AsyncWebServerRequest* request) {
      request->redirect("http://" + WiFi.softAPIP().toString() + "/");
    });
  }

  // Start the web server
  server->begin();
  serverRunning = true;
}

#endif  // ZEDMD_NO_NETWORKING