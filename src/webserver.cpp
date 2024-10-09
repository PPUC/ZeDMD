
#ifdef ZEDMD_WIFI
#include "webserver.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "version.h"

#define MINUTES_TO_MS 60000

AsyncWebServer server(80);

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index,
                  uint8_t *data, size_t len, bool final) {
  static File uploadFile;

  filename = "screensaver.jpg";  // For now only 1 image is supported, set the
                                 // filename.

  if (!index) {  // Start of the file upload (index == 0)
    uploadFile = LittleFS.open("/" + filename, FILE_WRITE);
    if (!uploadFile) {
      request->send(500, "text/plain", "Failed to open file for writing");
      return;
    }
  }

  if (uploadFile) {  // Write the received chunk into the file
    uploadFile.write(data, len);
  }

  if (final) {
    uploadFile.close();
    String imagePath = "/" + filename;
    if (!VerifyImage(imagePath.c_str())) {
      request->send(400, "text/plain",
                    "Invalid image size, must be 128x32 or 256x64");
      return;
    }
    DisplayImage(imagePath.c_str());
    request->send(200, "text/plain", "Image uploaded succesfully");
  }
}

void runWebServer() {
  // Serve index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", String(), false);
  });

  // Handle AJAX request to save WiFi configuration
  server.on("/save_wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true) &&
        request->hasParam("password", true) &&
        request->hasParam("port", true)) {
      ssid = request->getParam("ssid", true)->value();
      pwd = request->getParam("password", true)->value();
      port = request->getParam("port", true)->value().toInt();
      ssid_length = ssid.length();
      pwd_length = pwd.length();

      bool success = SaveWiFiConfig();
      if (success) {
        request->send(200, "text/plain", "Config saved successfully!");
        delay(1000);
        ESP.restart();
      } else {
        request->send(500, "text/plain", "Failed to save config!");
      }
    } else {
      request->send(400, "text/plain", "Missing parameters!");
    }
  });

  server.on("/wifi_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String jsonResponse;
    if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      IPAddress ip = WiFi.localIP();  // Get the local IP address

      jsonResponse = "{\"connected\":true,\"ssid\":\"" + WiFi.SSID() +
                     "\",\"signal\":" + String(rssi) + "," + "\"ip\":\"" +
                     ip.toString() + "\"," + "\"port\":" + String(port) + "}";
    } else {
      jsonResponse = "{\"connected\":false}";
    }

    request->send(200, "application/json", jsonResponse);
  });

  // Route to save RGB order
  server.on("/save_rgb_order", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("rgbOrder", true)) {
      String rgbOrderValue = request->getParam("rgbOrder", true)->value();
      rgbMode =
          rgbOrderValue.toInt();  // Convert to integer and set the RGB mode
      RefreshScreen();
      SaveRgbOrder();
      request->send(200, "text/plain", "RGB Order updated successfully");
    } else {
      request->send(400, "text/plain", "Missing RGB order parameter");
    }
  });

  // Route to save brightness
  server.on("/save_brightness", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("brightness", true)) {
      String brightnessValue = request->getParam("brightness", true)->value();
      lumstep = brightnessValue.toInt();
      GetDisplayObject()->SetBrightness(lumstep);
      SaveLum();
      request->send(200, "text/plain", "Brightness updated successfully");
    } else {
      request->send(400, "text/plain", "Missing brightness parameter");
    }
  });

  // Save screensaver settings
  server.on("/save_screensaver_settings", HTTP_POST,
            [](AsyncWebServerRequest *request) {
              if (request->hasParam("screensaverMode", true) &&
                  request->hasParam("enableDimAfterTimeout", true) &&
                  request->hasParam("dimTime", true)) {
                screensaverMode =
                    request->getParam("screensaverMode", true)->value().toInt();
                enableDimAfterTimeout =
                    request->getParam("enableDimAfterTimeout", true)
                        ->value()
                        .toInt() == 1;
                dimTimeout =
                    request->getParam("dimTime", true)->value().toInt() *
                    MINUTES_TO_MS;

                SaveScreensaverConfig();

                request->send(200, "text/plain", "Settings saved successfully");
              } else {
                request->send(400, "text/plain", "Invalid settings");
              }
            });

  server.on("/get_version", HTTP_GET, [](AsyncWebServerRequest *request) {
    String version = String(ZEDMD_VERSION_MAJOR) + "." +
                     String(ZEDMD_VERSION_MINOR) + "." +
                     String(ZEDMD_VERSION_PATCH);
    request->send(200, "text/plain", version);
  });

  server.on("/ppuc.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/ppuc.png", "image/png");
  });

  server.on("/screensaver.jpg", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/screensaver.jpg", "image/jpg");
  });

  server.on("/reset_wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    LittleFS.remove("/wifi_config.txt");  // Remove Wi-Fi config
    request->send(200, "text/plain", "Wi-Fi reset successful.");
    delay(1000);
    ESP.restart();  // Restart the device
  });

  // Handle image upload
  server.on(
      "/upload_image", HTTP_POST, [](AsyncWebServerRequest *request) {},
      handleUpload);

  server.on("/preview_screensaver", HTTP_GET,
            [](AsyncWebServerRequest *request) {
              // Render the image on the panel
              if (!VerifyImage("/screensaver.jpg")) {
                request->send(200, "text/plain", "Error with verifying image.");
                return;
              }
              DisplayImage("/screensaver.jpg");
              request->send(200, "text/plain", "Preview active");
            });

  // Serve debug information
  server.on("/debug_info", HTTP_GET, [](AsyncWebServerRequest *request) {
    String debugInfo = "IP Address: " + WiFi.localIP().toString() + "\n";
    debugInfo += "SSID: " + WiFi.SSID() + "\n";
    debugInfo += "RSSI: " + String(WiFi.RSSI()) + "\n";
    debugInfo += "Heap Free: " + String(ESP.getFreeHeap()) + " bytes\n";
    debugInfo += "Uptime: " + String(millis() / 1000) + " seconds\n";
    // Add more here if you need it
    request->send(200, "text/plain", debugInfo);
  });

  // Route to return the current settings as JSON
  server.on("/get_config", HTTP_GET, [](AsyncWebServerRequest *request) {
    String trimmedSsid = ssid;
    trimmedSsid.trim();

    if (port == 0) {
      port = 3333;  // Set default port number for webinterface
    }

    String json = "{";
    json += "\"ssid\":\"" + trimmedSsid + "\",";
    json += "\"port\":" + String(port) + ",";
    json += "\"rgbOrder\":" + String(rgbMode) + ",";
    json += "\"brightness\":" + String(lumstep) + ",";
    json += "\"screensaverMode\":" + String(screensaverMode) + ",";
    json += "\"enableDimAfterTimeout\":" + String(enableDimAfterTimeout) + ",";
    json += "\"dimTimeout\":" + String(dimTimeout / MINUTES_TO_MS) + ",";
    json += "\"scaleMode\":" + String(display->GetCurrentScalingMode());
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/get_scaling_modes", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!display) {
      request->send(500, "application/json",
                    "{\"error\":\"Display object not initialized\"}");
      return;
    }

    String jsonResponse;
    if (display->HasScalingModes()) {
      jsonResponse = "{";
      jsonResponse += "\"hasScalingModes\":true,";

      // Fetch current scaling mode
      uint8_t currentMode = display->GetCurrentScalingMode();
      jsonResponse += "\"currentMode\":" + String(currentMode) + ",";

      // Add the list of available scaling modes
      jsonResponse += "\"modes\":[";
      const char **scalingModes = display->GetScalingModes();
      uint8_t modeCount = display->GetScalingModeCount();
      for (uint8_t i = 0; i < modeCount; i++) {
        jsonResponse += "\"" + String(scalingModes[i]) + "\"";
        if (i < modeCount - 1) {
          jsonResponse += ",";
        }
      }
      jsonResponse += "]";
      jsonResponse += "}";
    } else {
      jsonResponse = "{\"hasScalingModes\":false}";
    }

    request->send(200, "application/json", jsonResponse);
  });

  // POST request to save the selected scaling mode
  server.on(
      "/save_scaling_mode", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!display) {
          request->send(500, "text/plain", "Display object not initialized");
          return;
        }

        if (request->hasParam("scalingMode", true)) {
          String scalingModeValue =
              request->getParam("scalingMode", true)->value();
          uint8_t scalingMode = scalingModeValue.toInt();

          // Update the scaling mode using the global display object
          display->SetCurrentScalingMode(scalingMode);
          SaveScale();
          request->send(200, "text/plain", "Scaling mode updated successfully");
        } else {
          request->send(400, "text/plain", "Missing scaling mode parameter");
        }
      });

  server.begin();  // Start the web server
}
#endif