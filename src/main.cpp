#include <Arduino.h>
#include <AsyncUDP.h>
#include <Bounce2.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <cstring>

#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#include "S3Specific.h"
#endif
#include "displayConfig.h"  // Variables shared by main and displayDrivers
#include "displayDriver.h"  // Base class for all display drivers
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "miniz/miniz.h"
#include "panel.h"
#include "version.h"
#include "webserver.h"

// To save RAM only include the driver we want to use.
#ifdef DISPLAY_RM67162_AMOLED
#include "displays/Rm67162Amoled.h"
#elif defined(DISPLAY_LED_MATRIX)
#include "displays/LEDMatrix.h"
#endif

#define N_CTRL_CHARS 5
#define N_INTERMEDIATE_CTR_CHARS 4
#ifdef BOARD_HAS_PSRAM
#define NUM_BUFFERS 48  // Number of buffers
#else
#define NUM_BUFFERS 16  // Number of buffers
#endif
#define BUFFER_SIZE 1152
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#define SERIAL_BAUD 2000000  // Serial baud rate.
#else
#define SERIAL_BAUD 921600  // Serial baud rate.
#endif
#define SERIAL_BUFFER 2048  // Serial buffer size in byte.
#define SERIAL_TIMEOUT \
  8  // Time in milliseconds to wait for the next data chunk.

// Specific improvements and #define for the ESP32 S3 series
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#include "S3Specific.h"
#endif

#ifdef ARDUINO_ESP32_S3_N16R8
#define RGB_ORDER_BUTTON_PIN 45
#define BRIGHTNESS_BUTTON_PIN 48
#elif defined(DISPLAY_RM67162_AMOLED)
#define RGB_ORDER_BUTTON_PIN 0
#define BRIGHTNESS_BUTTON_PIN 21
#else
#define RGB_ORDER_BUTTON_PIN 21
#define BRIGHTNESS_BUTTON_PIN 33
#endif

#define LED_CHECK_DELAY 1000  // ms per color

enum { ZEDMD_UART = 0, ZEDMD_WIFI = 1 };

DisplayDriver *display;
AsyncUDP udp;

// Buffers for storing data
u_int8_t buffers[NUM_BUFFERS][BUFFER_SIZE] __attribute__((aligned(4)));
uint16_t bufferSizes[NUM_BUFFERS] = {0};

// Semaphores
SemaphoreHandle_t xBufferFilled[NUM_BUFFERS];
SemaphoreHandle_t xBufferProcessed[NUM_BUFFERS];

// @todo zu gross
uint8_t uncompressBuffer[TOTAL_BYTES] __attribute__((aligned(4)));
uint8_t renderBuffer[TOTAL_BYTES] __attribute__((aligned(4)));
uint8_t processingBuffer = 0;

uint16_t payloadSize = 0;
uint8_t command = 0;
uint8_t currentBuffer = NUM_BUFFERS - 1;
uint8_t lastBuffer = currentBuffer;

uint8_t lumstep = 5;  // Init display on medium brightness, otherwise it starts
                      // up black on some displays
uint8_t settingsMenu = 0;

String ssid;
String pwd;
uint16_t port = 3333;
uint8_t ssid_length;
uint8_t pwd_length;
bool wifiActive = false;
#if defined(DISPLAY_RM67162_AMOLED)
uint8_t transport = ZEDMD_WIFI;
#else
uint8_t transport = ZEDMD_UART;
#endif

void DoRestart(int sec) {
  if (wifiActive) {
    MDNS.end();
    WiFi.disconnect(true);
  }
  sleep(sec);
  ESP.restart();
}

void Restart() { DoRestart(1); }

void RestartAfterError() { DoRestart(30); }

void DisplayNumber(uint32_t chf, uint8_t nc, uint16_t x, uint16_t y, uint8_t r,
                   uint8_t g, uint8_t b, bool transparent = false) {
  char text[nc];
  sprintf(text, "%d", chf);

  uint8_t i = 0;
  if (strlen(text) < nc) {
    for (; i < (nc - strlen(text)); i++) {
      display->DisplayText(" ", x + (4 * i), y, r, g, b, transparent);
    }
  }

  display->DisplayText(text, x + (4 * i), y, r, g, b, transparent);
}

void DisplayVersion(bool logo = false) {
  // display the version number to the lower right
  char version[10];
  snprintf(version, 9, "%d.%d.%d", ZEDMD_VERSION_MAJOR, ZEDMD_VERSION_MINOR,
           ZEDMD_VERSION_PATCH);
  display->DisplayText(version, TOTAL_WIDTH - (strlen(version) * 4),
                       TOTAL_HEIGHT - 5, 255 * !logo, 255 * !logo, 255 * !logo,
                       logo);
}

void DisplayLum(uint8_t r = 128, uint8_t g = 128, uint8_t b = 128) {
  display->DisplayText(" ", (TOTAL_WIDTH / 2) - 26 - 1, TOTAL_HEIGHT - 6, r, g,
                       b);
  display->DisplayText("Brightness:", (TOTAL_WIDTH / 2) - 26, TOTAL_HEIGHT - 6,
                       r, g, b);
  DisplayNumber(lumstep, 2, (TOTAL_WIDTH / 2) + 18, TOTAL_HEIGHT - 6, 255, 191,
                0);
}

void DisplayRGB(uint8_t r = 128, uint8_t g = 128, uint8_t b = 128) {
  display->DisplayText("red", 0, 0, 0, 0, 0, true, true);
  for (uint8_t i = 0; i < 6; i++) {
    display->DrawPixel(TOTAL_WIDTH - (4 * 4) - 1, i, 0, 0, 0);
    display->DrawPixel((TOTAL_WIDTH / 2) - (6 * 4) - 1, i, 0, 0, 0);
  }
  display->DisplayText("blue", TOTAL_WIDTH - (4 * 4), 0, 0, 0, 0, true, true);
  display->DisplayText("green", 0, TOTAL_HEIGHT - 6, 0, 0, 0, true, true);
  display->DisplayText("RGB Order:", (TOTAL_WIDTH / 2) - (6 * 4), 0, r, g, b);
  DisplayNumber(rgbMode, 2, (TOTAL_WIDTH / 2) + (4 * 4), 0, 255, 191, 0);
}

/// @brief Get DisplayDriver object, required for webserver
DisplayDriver *GetDisplayObject() { return display; }

void ClearScreen() {
  memset(renderBuffer, 0, TOTAL_BYTES);
  display->ClearScreen();
  display->SetBrightness(lumstep);
}

void LoadSettingsMenu() {
  File f = LittleFS.open("/settings_menu.val", "r");
  if (!f) return;
  settingsMenu = f.read();
  f.close();
}

void SaveSettingsMenu() {
  File f = LittleFS.open("/settings_menu.val", "w");
  f.write(settingsMenu);
  f.close();
}

void LoadTransport() {
  File f = LittleFS.open("/transport.val", "r");
  if (!f) return;
  transport = f.read();
  f.close();
}

void SaveTransport() {
  File f = LittleFS.open("/transport.val", "w");
  f.write(transport);
  f.close();
}

void LoadRgbOrder() {
  File f = LittleFS.open("/rgb_order.val", "r");
  if (!f) return;
  rgbMode = rgbModeLoaded = f.read();
  f.close();
}

void SaveRgbOrder() {
  File f = LittleFS.open("/rgb_order.val", "w");
  f.write(rgbMode);
  f.close();
}

void LoadLum() {
  File f = LittleFS.open("/lum.val", "r");
  if (!f) return;
  lumstep = f.read();
  f.close();
}

void SaveLum() {
  File f = LittleFS.open("/lum.val", "w");
  f.write(lumstep);
  f.close();
}

void LoadScale() {
  File f = LittleFS.open("/scale.val", "r");
  if (!f) return;
  display->SetCurrentScalingMode(f.read());
  f.close();
}

void SaveScale() {
  File f = LittleFS.open("/scale.val", "w");
  f.write(display->GetCurrentScalingMode());
  f.close();
}

bool LoadWiFiConfig() {
  File wifiConfig = LittleFS.open("/wifi_config.txt", "r");
  if (!wifiConfig) return false;

  while (wifiConfig.available()) {
    ssid = wifiConfig.readStringUntil('\n');
    ssid_length = wifiConfig.readStringUntil('\n').toInt();
    pwd = wifiConfig.readStringUntil('\n');
    pwd_length = wifiConfig.readStringUntil('\n').toInt();
    port = wifiConfig.readStringUntil('\n').toInt();
  }
  wifiConfig.close();
  return true;
}

bool SaveWiFiConfig() {
  File wifiConfig = LittleFS.open("/wifi_config.txt", "w");
  if (!wifiConfig) return false;

  wifiConfig.println(ssid);
  wifiConfig.println(String(ssid_length));
  wifiConfig.println(pwd);
  wifiConfig.println(String(pwd_length));
  wifiConfig.println(String(port));
  wifiConfig.close();
  return true;
}
void LedTester(void) {
  display->FillScreen(255, 0, 0);
  delay(LED_CHECK_DELAY);

  display->FillScreen(0, 255, 0);
  delay(LED_CHECK_DELAY);

  display->FillScreen(0, 0, 255);
  delay(LED_CHECK_DELAY);

  display->ClearScreen();
}

void DisplayLogo(void) {
  ClearScreen();

  File f;

  if (TOTAL_HEIGHT == 64) {
    f = LittleFS.open("/logoHD.raw", "r");
  } else {
    f = LittleFS.open("/logo.raw", "r");
  }

  if (!f) {
    display->DisplayText("Logo is missing", 0, 0, 255, 0, 0);
    return;
  }

  for (uint16_t tj = 0; tj < TOTAL_BYTES; tj += 3) {
    if (rgbMode == rgbModeLoaded) {
      renderBuffer[tj] = f.read();
      renderBuffer[tj + 1] = f.read();
      renderBuffer[tj + 2] = f.read();
    } else {
      renderBuffer[tj + rgbOrder[rgbMode * 3]] = f.read();
      renderBuffer[tj + rgbOrder[rgbMode * 3 + 1]] = f.read();
      renderBuffer[tj + rgbOrder[rgbMode * 3 + 2]] = f.read();
    }
  }

  f.close();

  display->FillPanelRaw(renderBuffer);

  DisplayVersion(true);
}

void DisplayUpdate(void) {
  ClearScreen();

  File f;

  if (TOTAL_HEIGHT == 64) {
    f = LittleFS.open("/ppucHD.raw", "r");
  } else {
    f = LittleFS.open("/ppuc.raw", "r");
  }

  if (!f) {
    return;
  }

  for (uint16_t tj = 0; tj < TOTAL_BYTES; tj++) {
    renderBuffer[tj] = f.read();
  }
  f.close();

  display->FillPanelRaw(renderBuffer);
}

/// @brief Refreshes screen after color change, needed for webserver
void RefreshSetupScreen() {
  DisplayLogo();
  DisplayRGB();
  DisplayLum();
}

void AcquireNextBuffer() {
  if (currentBuffer == lastBuffer) {
    // Move to the next buffer
    currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
    xSemaphoreTake(xBufferProcessed[currentBuffer], portMAX_DELAY);
  }
}

void Task_ReadSerial(void *pvParameters) {
  const uint8_t CtrlChars[5] = {'Z', 'e', 'D', 'M', 'D'};
  uint8_t numCtrlCharsFound = 0;

  Serial.setRxBufferSize(SERIAL_BUFFER);
#if (defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1)
  // S3 USB CDC
  Serial.begin(115200);
#else
  Serial.setTimeout(SERIAL_TIMEOUT);
  Serial.begin(SERIAL_BAUD);
  while (!Serial);
#endif

  while (1) {
    // Wait for data to be ready
    if (Serial.available()) {
      uint8_t byte = Serial.read();
      // DisplayNumber(byte, 2, TOTAL_WIDTH - 2 * 4, TOTAL_HEIGHT - 6, 255, 255,
      // 255);

      if (numCtrlCharsFound < N_CTRL_CHARS) {
        // Detect 5 consecutive start bits
        if (byte == CtrlChars[numCtrlCharsFound]) {
          numCtrlCharsFound++;
        } else {
          numCtrlCharsFound = 0;
        }
      } else if (numCtrlCharsFound == N_CTRL_CHARS) {
        command = byte;

        switch (command) {
          case 12:  // ask for resolution (and shake hands)
          {
            for (u_int8_t i = 0; i < N_INTERMEDIATE_CTR_CHARS; i++) {
              Serial.write(CtrlChars[i]);
            }
            Serial.write(TOTAL_WIDTH & 0xff);
            Serial.write((TOTAL_WIDTH >> 8) & 0xff);
            Serial.write(TOTAL_HEIGHT & 0xff);
            Serial.write((TOTAL_HEIGHT >> 8) & 0xff);
            numCtrlCharsFound = 0;
            Serial.write('R');
            break;
          }

          case 32:  // get version
          {
            Serial.write(ZEDMD_VERSION_MAJOR);
            Serial.write(ZEDMD_VERSION_MINOR);
            Serial.write(ZEDMD_VERSION_PATCH);
            numCtrlCharsFound = 0;
            break;
          }

          case 33:  // get panel resolution
          {
            Serial.write(TOTAL_WIDTH & 0xff);
            Serial.write((TOTAL_WIDTH >> 8) & 0xff);
            Serial.write(TOTAL_HEIGHT & 0xff);
            Serial.write((TOTAL_HEIGHT >> 8) & 0xff);
            numCtrlCharsFound = 0;
            break;
          }

          case 22:  // set brightness
          {
            lumstep = Serial.read();
            display->SetBrightness(lumstep);
            Serial.write('A');
            numCtrlCharsFound = 0;
            break;
          }

          case 23:  // set RGB order
          {
            rgbMode = Serial.read();
            Serial.write('A');
            numCtrlCharsFound = 0;
            break;
          }

          case 24:  // get brightness
          {
            Serial.write(lumstep);
            numCtrlCharsFound = 0;
            break;
          }

          case 25:  // get RGB order
          {
            Serial.write(rgbMode);
            numCtrlCharsFound = 0;
            break;
          }

          case 30:  // save settings
          {
            SaveLum();
            SaveRgbOrder();
            Serial.write('A');
            numCtrlCharsFound = 0;
            break;
          }

          case 31:  // reset
          {
            Serial.write('A');
            Restart();
            numCtrlCharsFound = 0;
            break;
          }

          case 16: {
            Serial.write('A');
            LedTester();
            Restart();
            numCtrlCharsFound = 0;
            break;
          }

          case 10:  // clear screen
          {
            // Wait until everything is rendered
            xSemaphoreTake(xBufferProcessed[currentBuffer], portMAX_DELAY);
            xSemaphoreGive(xBufferProcessed[currentBuffer]);
            Serial.write('A');
            ClearScreen();
            numCtrlCharsFound = 0;
            break;
          }

          case 4:  // announce RGB565 zones streaming
          {
            AcquireNextBuffer();
            Serial.write('A');
            numCtrlCharsFound = 0;
            break;
          }

          case 5:  // mode RGB565 zones streaming
          {
            // Read payload size (next 2 bytes)
            payloadSize = (Serial.read() << 8) | Serial.read();

            if (payloadSize > BUFFER_SIZE || payloadSize == 0) {
              Serial.write('E');
              numCtrlCharsFound = 0;
              break;
            }
            bufferSizes[currentBuffer] = payloadSize;

            numCtrlCharsFound++;

            break;
          }

          default: {
            Serial.write('E');
            numCtrlCharsFound = 0;
          }
        }
      }

      if (numCtrlCharsFound > N_CTRL_CHARS) {
        uint16_t bytesRead = 0;
        while (bytesRead < payloadSize) {
          // Fill the buffer with payload
          bytesRead += Serial.readBytes(
              &buffers[currentBuffer][bytesRead],
              min(Serial.available(), payloadSize - bytesRead));
        }

        // Signal to process the filled buffer
        xSemaphoreGive(xBufferFilled[currentBuffer]);
        lastBuffer = currentBuffer;

        // Send an (A)cknowledge signal to tell the client that we
        // successfully read the data.
        Serial.write('A');

        numCtrlCharsFound = 0;
      }
    } else {
      // Avoid busy-waiting
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

/// @brief Handles the UDP Packet parsing for ZeDMD WiFi and ZeDMD-HD WiFi
/// @param packet
void IRAM_ATTR HandlePacket(AsyncUDPPacket packet) {
  uint8_t *pPacket = packet.data();
  uint16_t receivedBytes = packet.length();
  if (receivedBytes >= 1 && receivedBytes <= (BUFFER_SIZE + 1)) {
    command = pPacket[0];

    switch (command) {
      case 16: {
        LedTester();
        Restart();
        break;
      }

      case 10:  // clear screen
      {
        // Wait until everything is rendered
        xSemaphoreTake(xBufferProcessed[currentBuffer], portMAX_DELAY);
        xSemaphoreGive(xBufferProcessed[currentBuffer]);
        ClearScreen();
        break;
      }

      case 5:  // RGB565 Zones Stream
      {
        payloadSize = receivedBytes - 1;
        AcquireNextBuffer();
        bufferSizes[currentBuffer] = payloadSize;
        memcpy(buffers[currentBuffer], &pPacket[1], payloadSize);
        xSemaphoreGive(xBufferFilled[currentBuffer]);
        lastBuffer = currentBuffer;
        break;
      }
    }
  }
}

/// @brief Handles the mDNS Packets for ZeDMD WiFi, this allows autodiscovery
void RunMDNS() {
  if (!MDNS.begin("ZeDMD-WiFi")) {
    return;
  }
  MDNS.addService("zedmd-wifi", "udp", port);
}

void StartWiFi() {
  const char *apSSID = "ZeDMD-WiFi";
  const char *apPassword = "zedmd1234";

  if (LoadWiFiConfig()) {
    WiFi.disconnect(true);
    WiFi.begin(ssid.substring(0, ssid_length).c_str(),
               pwd.substring(0, pwd_length).c_str());

    WiFi.setSleep(false);  // WiFi speed improvement on ESP32 S3 and others.

    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      WiFi.softAP(apSSID, apPassword);  // Start AP if WiFi fails to connect
    }
  } else {
    WiFi.softAP(apSSID, apPassword);  // Start AP if config not found
  }

  IPAddress ip;
  if (WiFi.getMode() == WIFI_AP) {
    ip = WiFi.softAPIP();
  } else if (WiFi.getMode() == WIFI_STA) {
    ip = WiFi.localIP();
  }

  if (!ip[0]) {
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.softAP(apSSID, apPassword);
    delay(1000);
    ip = WiFi.softAPIP();
  }

  for (uint8_t i = 0; i < 4; i++) {
    DisplayNumber(ip[i], 3, i * 3 * 4 + i, 0, 255, 191, 0);
  }

  runWebServer();  // Start the web server
  RunMDNS();       // Start the MDNS server for easy detection

  if (udp.listen(ip, port)) {
    udp.onPacket(HandlePacket);  // Start listening to ZeDMD UDP traffic
  }

  wifiActive = true;
}

void Task_SettingsMenu(void *pvParameters) {
  while (1) {
    if (!digitalRead(BRIGHTNESS_BUTTON_PIN)) {
      File f = LittleFS.open("/settings_menu.val", "w");
      f.write(1);
      f.close();
      delay(100);
      Restart();
    }
    // Avoid busy-waiting
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  bool fileSystemOK;
  if (fileSystemOK = LittleFS.begin()) {
    LoadSettingsMenu();
    LoadTransport();
    LoadRgbOrder();
    LoadLum();
  }

#ifdef DISPLAY_RM67162_AMOLED
  display = new Rm67162Amoled();  // For AMOLED display
#elif defined(DISPLAY_LED_MATRIX)
  display = new LedMatrix();  // For LED matrix display
#endif

  if (!fileSystemOK) {
    display->DisplayText("Error reading file system!", 0, 0, 255, 0, 0);
    display->DisplayText("Try to flash the firmware again.", 0, 6, 255, 255,
                         255);
    while (true);
  }

  if (settingsMenu) {
    RefreshSetupScreen();
    display->DisplayText(transport == ZEDMD_UART ? "USB " : "WiFi", 7, 13, 128,
                         128, 128);
    display->DisplayText("Exit", 105, 13, 255, 191, 0);

    Bounce2::Button *brightnessButton = new Bounce2::Button();
    brightnessButton->attach(BRIGHTNESS_BUTTON_PIN, INPUT_PULLUP);
    brightnessButton->interval(100);
    brightnessButton->setPressedState(LOW);

    Bounce2::Button *rgbOrderButton = new Bounce2::Button();
    rgbOrderButton->attach(RGB_ORDER_BUTTON_PIN, INPUT_PULLUP);
    rgbOrderButton->interval(100);
    rgbOrderButton->setPressedState(LOW);

    uint8_t position = 1;
    while (1) {
      brightnessButton->update();
      if (brightnessButton->pressed()) {
        if (++position > 4) position = 1;

        switch (position) {
          case 1: {
            DisplayLum();
            DisplayRGB();
            display->DisplayText(transport == ZEDMD_UART ? "USB " : "WiFi", 7,
                                 13, 128, 128, 128);
            display->DisplayText("Exit", 105, 13, 255, 191, 0);
            break;
          }
          case 2: {
            DisplayLum(255, 191, 0);
            DisplayRGB();
            display->DisplayText(transport == ZEDMD_UART ? "USB " : "WiFi", 7,
                                 13, 128, 128, 128);
            display->DisplayText("Exit", 105, 13, 128, 128, 128);
            break;
          }
          case 3: {
            DisplayLum();
            DisplayRGB();
            display->DisplayText(transport == ZEDMD_UART ? "USB " : "WiFi", 7,
                                 13, 255, 191, 0);
            display->DisplayText("Exit", 105, 13, 128, 128, 128);
            break;
          }
          case 4: {
            DisplayLum();
            DisplayRGB(255, 191, 0);
            display->DisplayText(transport == ZEDMD_UART ? "USB " : "WiFi", 7,
                                 13, 128, 128, 128);
            display->DisplayText("Exit", 105, 13, 128, 128, 128);
            break;
          }
        }
      }

      rgbOrderButton->update();
      if (rgbOrderButton->pressed()) {
        switch (position) {
          case 1: {
            settingsMenu = false;
            SaveSettingsMenu();
            delay(10);
            Restart();
            break;
          }
          case 2: {
            lumstep++;
            if (lumstep >= 16) lumstep = 1;
            display->SetBrightness(lumstep);
            DisplayLum(255, 191, 0);
            SaveLum();
            break;
          }
          case 3: {
            transport = !transport;
            display->DisplayText(transport == ZEDMD_UART ? "USB " : "WiFi", 7,
                                 13, 255, 191, 0);
            SaveTransport();
            break;
          }
          case 4: {
            if (rgbModeLoaded != 0) {
              rgbMode = 0;
              SaveRgbOrder();
              delay(10);
              Restart();
            }
            if (++rgbMode > 5) rgbMode = 0;
            RefreshSetupScreen();
            DisplayRGB(255, 191, 0);
            SaveRgbOrder();
            break;
          }
        }
      }
    }
  }

  pinMode(BRIGHTNESS_BUTTON_PIN, INPUT_PULLUP);

  DisplayLogo();

  // Create synchronization primitives
  for (uint8_t i = 0; i < NUM_BUFFERS; i++) {
    xBufferFilled[i] = xSemaphoreCreateBinary();
    xBufferProcessed[i] = xSemaphoreCreateBinary();
  }

  xTaskCreatePinnedToCore(Task_SettingsMenu, "Task_SettingsMenu", 4096, NULL, 1,
                          NULL, 1);

  switch (transport) {
    case ZEDMD_UART: {
      xTaskCreatePinnedToCore(Task_ReadSerial, "Task_ReadSerial", 4096, NULL, 1,
                              NULL, 0);
      break;
    }

    case ZEDMD_WIFI: {
      StartWiFi();
      break;
    }
  }

  // Enable all buffers
  for (uint8_t i = 0; i < NUM_BUFFERS; i++) {
    xSemaphoreGive(xBufferProcessed[i]);
  }
}

void loop() {
  if (xSemaphoreTake(xBufferFilled[processingBuffer], portMAX_DELAY)) {
    mz_ulong compressedBufferSize = (mz_ulong)bufferSizes[processingBuffer];
    mz_ulong uncompressedBufferSize = (mz_ulong)TOTAL_BYTES;

    int minizStatus =
        mz_uncompress2(uncompressBuffer, &uncompressedBufferSize,
                       &buffers[processingBuffer][0], &compressedBufferSize);

    // Mark buffer as free
    xSemaphoreGive(xBufferProcessed[processingBuffer]);

    if (MZ_OK == minizStatus) {
      uint16_t uncompressedBufferPosition = 0;
      while (uncompressedBufferPosition < uncompressedBufferSize) {
        if (uncompressBuffer[uncompressedBufferPosition] >= 128) {
          display->ClearZone(uncompressBuffer[uncompressedBufferPosition++] -
                             128);
        } else {
          display->FillZoneRaw565(
              uncompressBuffer[uncompressedBufferPosition++],
              &uncompressBuffer[uncompressedBufferPosition]);
          uncompressedBufferPosition += RGB565_ZONE_SIZE;
        }
      }
    } else {
      // display->DisplayText("miniz error ", 0, 0, 255, 0, 0);
      // DisplayNumber(minizStatus, 3, 0, 6, 255, 0, 0);
    }
  }

  // Move to the next buffer
  processingBuffer = (processingBuffer + 1) % NUM_BUFFERS;
}
