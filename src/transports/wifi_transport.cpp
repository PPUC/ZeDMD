#ifndef ZEDMD_NO_NETWORKING

#include "wifi_transport.h"

#include <LittleFS.h>
#include <WiFi.h>

#include "main.h"
#include "panel.h"
#include "version.h"

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
    WiFi.begin(ssid.substring(0, ssid_length).c_str(),
               pwd.substring(0, pwd_length).c_str());

    // Don't use WiFi.waitForConnectResult(10000) here, it blocks the menu
    // button.
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
      CheckMenuButton();
      vTaskDelay(pdMS_TO_TICKS(100));  // FreeRTOS delay, avoids blocking
    }

    if (WiFi.status() != WL_CONNECTED) {
      GetDisplayDriver()->DisplayText("No WiFi connection, error ", 10,
                                      TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
      DisplayNumber(WiFi.status(), 2, 26 * 4 + 10, TOTAL_HEIGHT / 2 - 9, 255, 0,
                    0);
      GetDisplayDriver()->DisplayText("Trying again ...", 10,
                                      TOTAL_HEIGHT / 2 - 3, 255, 0, 0);
      // second try
      startTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        CheckMenuButton();
        vTaskDelay(pdMS_TO_TICKS(100));  // FreeRTOS delay, avoids blocking
      }
      if (WiFi.status() != WL_CONNECTED) {
        softAPFallback = true;
      }
    }
  } else {
    // Don't use the fallback to skip the countdown.
    WiFi.softAP(apSSID, apPassword);
    ip = WiFi.softAPIP();
  }

  if (!softAPFallback && WiFiClass::getMode() == WIFI_STA) {
    ip = WiFi.localIP();
  }

  if (ip[0] == 0 || softAPFallback) {
    GetDisplayDriver()->DisplayText("No WiFi connection, maybe     ", 10,
                                    TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
    GetDisplayDriver()->DisplayText("the credentials are wrong.", 10,
                                    TOTAL_HEIGHT / 2 - 3, 255, 0, 0);
    GetDisplayDriver()->DisplayText("Start AP in 20 seconds ...", 10,
                                    TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
    for (uint8_t i = 19; i > 0; i--) {
      CheckMenuButton();
      vTaskDelay(pdMS_TO_TICKS(1000));
      DisplayNumber(i, 2, 58, TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
    }
    WiFi.softAP(apSSID, apPassword);
    ip = WiFi.softAPIP();
  }

  ClearScreen();
  DisplayLogo();
  DisplayId();

  for (uint8_t i = 0; i < 4; i++) {
    if (i > 0) GetDisplayDriver()->DrawPixel(i * 3 * 4 + i * 2 - 2, 4, 0);
    DisplayNumber(ip[i], 3, i * 3 * 4 + i * 2, 0, 0, 0, 0, true);
  }

  WiFi.setSleep(false);  // WiFi speed improvement on ESP32 S3 and others.

  WiFi.setTxPower((wifi_power_t)wifiPower);

  if (debug) {
    GetDisplayDriver()->DisplayText("WiFi RSSI: ", 0, TOTAL_HEIGHT / 2 - 9, 255,
                                    0, 0);
    DisplayNumber(WiFi.RSSI(), 3, 11 * 4, TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
    GetDisplayDriver()->DisplayText("TX Power:  ", 0, TOTAL_HEIGHT / 2 - 3, 255,
                                    0, 0);
    DisplayNumber(WiFi.getTxPower(), 3, 11 * 4, TOTAL_HEIGHT / 2 - 3, 255, 0,
                  0);
    GetDisplayDriver()->DisplayText("Channel:   ", 0, TOTAL_HEIGHT / 2 + 3, 255,
                                    0, 0);
    DisplayNumber(WiFi.channel(), 3, 11 * 4, TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
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

  startServer();

  if (m_type == WIFI_UDP) {
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

bool WifiTransport::deinit() {
  MDNS.end();
  const bool ret = WiFi.disconnect(true);
  m_active = false;
  return ret;
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
    if (wifiConfig
            .available())  // Backward compatibility, check if power line exists
      wifiPower = wifiConfig.readStringUntil('\n').toInt();
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

void WifiTransport::HandleUdpPacket(AsyncUDPPacket packet) {
  static bool isProcessing = false;

  if (!isProcessing) {
    isProcessing = true;
    transportActive = true;
    HandleData(packet.data(), packet.length());
    yield();
    isProcessing = false;
  }
}

void WifiTransport::HandleTcpData(void* arg, AsyncClient* client, void* data,
                                  size_t len) {
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
    client->stop();
    delete client;
    return;
  }
  payloadMissing = 0;
  headerBytesReceived = 0;
  numCtrlCharsFound = 0;
  transportActive = true;
  client->setNoDelay(true);
  client->setAckTimeout(2);
  client->onData(&HandleTcpData, nullptr);
  client->onDisconnect(&HandleTcpDisconnect, nullptr);
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
        Restart();
      } else {
        request->send(500, "text/plain", "Failed to save config!");
      }
    } else {
      request->send(400, "text/plain", "Missing parameters!");
    }
  });

  server->on("/wifi_status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String jsonResponse;
    if (WiFi.status() == WL_CONNECTED) {
      const int8_t rssi = WiFi.RSSI();
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
        Restart();
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
    request->send(
        200, "text/plain",
        String(TOTAL_WIDTH) + "|" + String(TOTAL_HEIGHT) + "|" +
            String(ZEDMD_VERSION_MAJOR) + "." + String(ZEDMD_VERSION_MINOR) +
            "." + String(ZEDMD_VERSION_PATCH) + "|" +
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED) || \
    defined(PICO_BUILD)
            String(1)
#else
        String(0)
#endif
            + "|" + (m_type == WIFI_UDP ? "UDP" : "TCP") + "|" + String(port) +
            "|" + String(m_delay) + "|" +
            String(usbPackageSizeMultiplier * 32) + "|" + String(brightness) +
            "|" +
#ifndef DISPLAY_RM67162_AMOLED
            String(rgbMode) + "|" + String(panelClkphase) + "|" +
            String(panelDriver) + "|" + String(panelI2sspeed) + "|" +
            String(panelLatchBlanking) + "|" + String(panelMinRefreshRate) +
            "|" + String(yOffset)
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
    );
  });

  server->on("/ppuc.png", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/ppuc.png", "image/png");
  });

  server->on("/reset_wifi", HTTP_POST, [](AsyncWebServerRequest* request) {
    LittleFS.remove("/wifi_config.txt");  // Remove Wi-Fi config
    request->send(200, "text/plain", "Wi-Fi reset successful.");
    Restart();  // Restart the device
  });

  server->on("/apply", HTTP_POST, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Apply successful.");
    Restart();  // Restart the device
  });

  // Serve debug information
  server->on("/debug_info", HTTP_GET, [](AsyncWebServerRequest* request) {
    String debugInfo = "IP Address: " + WiFi.localIP().toString() + "\n";
    debugInfo += "SSID: " + WiFi.SSID() + "\n";
    debugInfo += "RSSI: " + String(WiFi.RSSI()) + "\n";
    debugInfo += "Heap Free: " + String(ESP.getFreeHeap()) + " bytes\n";
    debugInfo += "Uptime: " + String(millis() / 1000) + " seconds\n";
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

  // Start the web server
  server->begin();
  serverRunning = true;
}

#endif  // ZEDMD_NO_NETWORKING