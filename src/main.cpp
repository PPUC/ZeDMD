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
#define NUM_RENDER_BUFFERS 2
#else
#define NUM_BUFFERS 16  // Number of buffers
#define NUM_RENDER_BUFFERS 1
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

enum { TRANSPORT_UART = 0, TRANSPORT_WIFI = 1, TRANSPORT_SPI = 2 };

DisplayDriver *display;
AsyncUDP udp;
const char *apSSID = "ZeDMD-WiFi";
const char *apPassword = "zedmd1234";

// Buffers for storing data
uint8_t *buffers[NUM_BUFFERS];
uint16_t bufferSizes[NUM_BUFFERS] = {0};

// Semaphores
static SemaphoreHandle_t xBufferFilled[NUM_BUFFERS];
static SemaphoreHandle_t xBufferProcessed[NUM_BUFFERS];
static SemaphoreHandle_t xRenderMutex;

// The uncompress buffer should be bug enough
uint8_t uncompressBuffer[2048] __attribute__((aligned(4)));
static uint8_t *renderBuffer[NUM_RENDER_BUFFERS];
static uint8_t currentRenderBuffer = 0;
static uint8_t lastRenderBuffer = NUM_RENDER_BUFFERS - 1;

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
#ifdef ZEDMD_WIFI
uint8_t transport = TRANSPORT_WIFI;
#else
uint8_t transport = TRANSPORT_UART;
#endif
static bool transportActive = false;
uint8_t transportWaitCounter = 0;

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

void LoadSettingsMenu() {
  File f = LittleFS.open("/settings_menu.val", "r");
  if (!f) {
#if !defined(DISPLAY_RM67162_AMOLED)
    // Show settings menu on freshly installed device
    settingsMenu = 1;
#endif
    return;
  }
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

void Render() {
  if (xSemaphoreTake(xRenderMutex, portMAX_DELAY)) {
    if (NUM_RENDER_BUFFERS == 1) {
      display->FillPanelRaw(renderBuffer[currentRenderBuffer]);
    } else if (currentRenderBuffer != lastRenderBuffer) {
      uint16_t pos;

      for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
        for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
          pos = (y * TOTAL_WIDTH + x) * 3;
          if (!(0 == memcmp(&renderBuffer[currentRenderBuffer][pos],
                            &renderBuffer[lastRenderBuffer][pos], 3))) {
            display->DrawPixel(x, y, renderBuffer[currentRenderBuffer][pos],
                               renderBuffer[currentRenderBuffer][pos + 1],
                               renderBuffer[currentRenderBuffer][pos + 2]);
          }
        }
      }

      lastRenderBuffer = currentRenderBuffer;
      currentRenderBuffer = (currentRenderBuffer + 1) % NUM_RENDER_BUFFERS;
      memcpy(renderBuffer[currentRenderBuffer], renderBuffer[lastRenderBuffer],
             TOTAL_BYTES);
    }
    xSemaphoreGive(xRenderMutex);
  }
}

void ClearScreen() {
  display->ClearScreen();
  if (xSemaphoreTake(xRenderMutex, portMAX_DELAY)) {
    memset(renderBuffer[currentRenderBuffer], 0, TOTAL_BYTES);

    if (NUM_RENDER_BUFFERS > 1) {
      lastRenderBuffer = currentRenderBuffer;
      currentRenderBuffer = (currentRenderBuffer + 1) % NUM_RENDER_BUFFERS;
    }
    xSemaphoreGive(xRenderMutex);
  }
}

void DisplayLogo(void) {
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
  if (xSemaphoreTake(xRenderMutex, portMAX_DELAY)) {
    for (uint16_t tj = 0; tj < TOTAL_BYTES; tj += 3) {
      if (rgbMode == rgbModeLoaded) {
        renderBuffer[currentRenderBuffer][tj] = f.read();
        renderBuffer[currentRenderBuffer][tj + 1] = f.read();
        renderBuffer[currentRenderBuffer][tj + 2] = f.read();
      } else {
        renderBuffer[currentRenderBuffer][tj + rgbOrder[rgbMode * 3]] =
            f.read();
        renderBuffer[currentRenderBuffer][tj + rgbOrder[rgbMode * 3 + 1]] =
            f.read();
        renderBuffer[currentRenderBuffer][tj + rgbOrder[rgbMode * 3 + 2]] =
            f.read();
      }
    }
    xSemaphoreGive(xRenderMutex);
  }

  f.close();

  Render();
  DisplayVersion(true);
}

void DisplayUpdate(void) {
  File f;

  if (TOTAL_HEIGHT == 64) {
    f = LittleFS.open("/ppucHD.raw", "r");
  } else {
    f = LittleFS.open("/ppuc.raw", "r");
  }

  if (!f) {
    return;
  }

  if (xSemaphoreTake(xRenderMutex, portMAX_DELAY)) {
    for (uint16_t tj = 0; tj < TOTAL_BYTES; tj++) {
      renderBuffer[currentRenderBuffer][tj] = f.read();
    }
    xSemaphoreGive(xRenderMutex);
  }

  f.close();

  Render();
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
  display->DisplayText("USB CDC", 0, 0, 0, 0, 0, 1);
#else
  Serial.setTimeout(SERIAL_TIMEOUT);
  Serial.begin(SERIAL_BAUD);
  while (!Serial);
  DisplayNumber(SERIAL_BAUD, (SERIAL_BAUD >= 1000000 ? 7 : 6), 0, 0, 0, 0, 0,
                1);
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
            transportActive = true;
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
            AcquireNextBuffer();
            bufferSizes[currentBuffer] = 2;
            buffers[currentBuffer][0] = 0;
            buffers[currentBuffer][1] = 0;
            xSemaphoreGive(xBufferFilled[currentBuffer]);
            lastBuffer = currentBuffer;
            // Send an (A)cknowledge signal to tell the client that we
            // successfully read the data.
            // 'F' requests a full frame as next frame.
            Serial.write(transportActive ? 'A' : 'F');
            if (!transportActive) {
              transportActive = true;
            }
            numCtrlCharsFound = 0;
            break;
          }

          case 4:  // announce RGB565 zones streaming
          {
            AcquireNextBuffer();
            // Send an (A)cknowledge signal to tell the client that we
            // successfully read the data.
            // 'F' requests a full frame as next frame.
            Serial.write(transportActive ? 'A' : 'F');
            if (!transportActive) {
              transportActive = true;
            }
            numCtrlCharsFound = 0;
            break;
          }

          case 5:  // mode RGB565 zones streaming
          {
            // Read payload size (next 2 bytes)
            payloadSize = (Serial.read() << 8) | Serial.read();

            if (payloadSize > BUFFER_SIZE || payloadSize == 0) {
              // display->DisplayText("payloadSize > BUFFER_SIZE", 10, 13, 255,
              // 0, 0); while (1);
              Serial.write('E');
              numCtrlCharsFound = 0;
              break;
            }
            bufferSizes[currentBuffer] = payloadSize;

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

            break;
          }

          case 6:  // Render
          {
#if defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)
            AcquireNextBuffer();
            bufferSizes[currentBuffer] = 2;
            buffers[currentBuffer][0] = 255;
            buffers[currentBuffer][1] = 255;
            xSemaphoreGive(xBufferFilled[currentBuffer]);
            lastBuffer = currentBuffer;
#endif
            // Send an (A)cknowledge signal to tell the client that we
            // successfully read the data.
            Serial.write('A');
            numCtrlCharsFound = 0;
            break;
          }

          default: {
            display->DisplayText("Unsupported Command", 10, 13, 255, 0, 0);
            DisplayNumber(command, 3, 0, 0, 255, 255, 255);
            Serial.write('E');
            numCtrlCharsFound = 0;
          }
        }
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
        AcquireNextBuffer();
        bufferSizes[currentBuffer] = 2;
        buffers[currentBuffer][0] = 0;
        buffers[currentBuffer][1] = 0;
        xSemaphoreGive(xBufferFilled[currentBuffer]);
        lastBuffer = currentBuffer;
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

      case 6:  // Render
      {
#if defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)
        AcquireNextBuffer();
        bufferSizes[currentBuffer] = 2;
        buffers[currentBuffer][0] = 255;
        buffers[currentBuffer][1] = 255;
        xSemaphoreGive(xBufferFilled[currentBuffer]);
        lastBuffer = currentBuffer;
#endif
        break;
      }
    }

    if (!transportActive) {
      transportActive = true;
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
  if (LoadWiFiConfig() && ssid_length > 0) {
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
    display->DisplayText("zedmd-wifi.local", 0, TOTAL_HEIGHT - 5, 0, 0, 0, 1);
  } else if (WiFi.getMode() == WIFI_STA) {
    ip = WiFi.localIP();
  }

  for (uint8_t i = 0; i < 4; i++) {
    if (i > 0) display->DrawPixel(i * 3 * 4 + i * 2 - 2, 4, 0);
    DisplayNumber(ip[i], 3, i * 3 * 4 + i * 2, 0, 0, 0, 0, 1);
  }

  if (ip[0] == 0) {
    display->DisplayText("No WiFi connection, turn off", 10,
                         TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
    display->DisplayText("or the credentials will be", 10, TOTAL_HEIGHT / 2 - 3,
                         255, 0, 0);
    display->DisplayText("resettet in 60 seconds.", 10, TOTAL_HEIGHT / 2 + 3,
                         255, 0, 0);
    for (uint8_t i = 59; i > 0; i--) {
      sleep(1);
      DisplayNumber(i, 2, 58, TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
    }
    ssid = "\n";
    pwd = "\n";
    SaveWiFiConfig();
    delay(100);
    Restart();
  }

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
    vTaskDelay(pdMS_TO_TICKS(100));
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

  for (uint8_t i = 0; i < NUM_RENDER_BUFFERS; i++) {
#ifdef BOARD_HAS_PSRAM
    renderBuffer[i] = (uint8_t *)ps_malloc(TOTAL_BYTES);
#else
    renderBuffer[i] = (uint8_t *)malloc(TOTAL_BYTES);
#endif
    if (nullptr == renderBuffer[i]) {
      display->DisplayText("out of memory", 10, 13, 255, 0, 0);
      while (1);
    }
    memset(renderBuffer[i], 0, TOTAL_BYTES);
  }

  xRenderMutex = xSemaphoreCreateMutex();

  if (settingsMenu) {
    RefreshSetupScreen();
    display->DisplayText(transport == TRANSPORT_UART
                             ? "USB "
                             : (transport == TRANSPORT_WIFI ? "WiFi" : "SPI "),
                         7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 128,
                         128, 128);
    display->DisplayText("Exit", TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                         (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);

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
            display->DisplayText(
                transport == TRANSPORT_UART
                    ? "USB "
                    : (transport == TRANSPORT_WIFI ? "WiFi" : "SPI "),
                7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
            display->DisplayText("Exit",
                                 TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                                 (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
            break;
          }
          case 2: {
            DisplayLum(255, 191, 0);
            DisplayRGB();
            display->DisplayText(
                transport == TRANSPORT_UART
                    ? "USB "
                    : (transport == TRANSPORT_WIFI ? "WiFi" : "SPI "),
                7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
            display->DisplayText("Exit",
                                 TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                                 (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
            break;
          }
          case 3: {
            DisplayLum();
            DisplayRGB();
            display->DisplayText(
                transport == TRANSPORT_UART
                    ? "USB "
                    : (transport == TRANSPORT_WIFI ? "WiFi" : "SPI "),
                7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
            display->DisplayText("Exit",
                                 TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                                 (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
            break;
          }
          case 4: {
            DisplayLum();
            DisplayRGB(255, 191, 0);
            display->DisplayText(
                transport == TRANSPORT_UART
                    ? "USB "
                    : (transport == TRANSPORT_WIFI ? "WiFi" : "SPI "),
                7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
            display->DisplayText("Exit",
                                 TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                                 (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
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
            if (++transport > TRANSPORT_SPI) transport = TRANSPORT_UART;
            display->DisplayText(
                transport == TRANSPORT_UART
                    ? "USB "
                    : (transport == TRANSPORT_WIFI ? "WiFi" : "SPI "),
                7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
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
#ifdef BOARD_HAS_PSRAM
    buffers[i] = (uint8_t *)ps_malloc(BUFFER_SIZE);
#else
    buffers[i] = (uint8_t *)malloc(BUFFER_SIZE);
#endif
    if (nullptr == buffers[i]) {
      display->DisplayText("out of memory", 10, 13, 255, 0, 0);
      while (1);
    }
  }

  switch (transport) {
    case TRANSPORT_UART: {
      xTaskCreatePinnedToCore(Task_ReadSerial, "Task_ReadSerial", 4096, NULL, 1,
                              NULL, 0);
      break;
    }

    case TRANSPORT_WIFI: {
      StartWiFi();
      if (wifiActive) {
        RunMDNS();       // Start the MDNS server for easy detection
        runWebServer();  // Start the web server
      } else {
        display->DisplayText("No WiFi connection", 10, 13, 255, 0, 0);
        while (1);
      }
      break;
    }

    case TRANSPORT_SPI: {
      display->DisplayText("SPI is not implemented yet", 10, 13, 255, 0, 0);
      while (1);
      break;
    }
  }

  xTaskCreatePinnedToCore(Task_SettingsMenu, "Task_SettingsMenu", 4096, NULL, 1,
                          NULL, 1);

  // Enable all buffers
  for (uint8_t i = 0; i < NUM_BUFFERS; i++) {
    xSemaphoreGive(xBufferProcessed[i]);
  }

  display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 2,
                     TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 2, 127, 127, 127);
}

void loop() {
  while (!transportActive) {
    for (uint8_t i = 0; i <= 127; i += 127) {
      switch (transportWaitCounter) {
        case 0:
          display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 2,
                             TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 3, i, i,
                             i);
          break;
        case 1:
          display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 1,
                             TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 3, i, i,
                             i);
          break;
        case 2:
          display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 1,
                             TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 2, i, i,
                             i);
          break;
        case 3:
          display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 1,
                             TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 1, i, i,
                             i);
          break;
        case 4:
          display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 2,
                             TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 1, i, i,
                             i);
          break;
        case 5:
          display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 3,
                             TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 1, i, i,
                             i);
          break;
        case 6:
          display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 3,
                             TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 2, i, i,
                             i);
          break;
        case 7:
          display->DrawPixel(TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5) - 3,
                             TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5) - 3, i, i,
                             i);
          break;
      }

      if (!i) transportWaitCounter = (transportWaitCounter + 1) % 8;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // display->DisplayText("enter loop", 10, 13, 255, 0, 0);
  if (xSemaphoreTake(xBufferFilled[processingBuffer], portMAX_DELAY)) {
    if (2 == bufferSizes[processingBuffer] &&
        255 == buffers[processingBuffer][0] &&
        255 == buffers[processingBuffer][1]) {
      // Mark buffer as free
      xSemaphoreGive(xBufferProcessed[processingBuffer]);
#if defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)
      Render();
#endif
    } else if (2 == bufferSizes[processingBuffer] &&
               0 == buffers[processingBuffer][0] &&
               0 == buffers[processingBuffer][1]) {
      // Mark buffer as free
      xSemaphoreGive(xBufferProcessed[processingBuffer]);
      ClearScreen();
    } else {
      mz_ulong compressedBufferSize = (mz_ulong)bufferSizes[processingBuffer];
      mz_ulong uncompressedBufferSize = (mz_ulong)TOTAL_BYTES;
      memset(uncompressBuffer, 0, 2048);
      int minizStatus =
          mz_uncompress2(uncompressBuffer, &uncompressedBufferSize,
                         buffers[processingBuffer], &compressedBufferSize);

      // Mark buffer as free
      xSemaphoreGive(xBufferProcessed[processingBuffer]);

      if (MZ_OK == minizStatus) {
        uint16_t uncompressedBufferPosition = 0;
        while (uncompressedBufferPosition < uncompressedBufferSize) {
          if (uncompressBuffer[uncompressedBufferPosition] >= 128) {
#if defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)
            const uint8_t idx =
                uncompressBuffer[uncompressedBufferPosition++] - 128;
            const uint8_t yOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
            const uint8_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;
            if (xSemaphoreTake(xRenderMutex, portMAX_DELAY)) {
              for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
                memset(
                    &renderBuffer[currentRenderBuffer]
                                 [((yOffset + y) * TOTAL_WIDTH + xOffset) * 3],
                    0, ZONE_WIDTH * 3);
              }
              xSemaphoreGive(xRenderMutex);
            }
#else
            display->ClearZone(uncompressBuffer[uncompressedBufferPosition++] -
                               128);
#endif
          } else {
#if defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)
            uint8_t idx = uncompressBuffer[uncompressedBufferPosition++];
            const uint8_t yOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
            const uint8_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

            if (xSemaphoreTake(xRenderMutex, portMAX_DELAY)) {
              for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
                for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
                  const uint16_t rgb565 =
                      uncompressBuffer[uncompressedBufferPosition++] +
                      (((uint16_t)
                            uncompressBuffer[uncompressedBufferPosition++])
                       << 8);
                  uint8_t rgb888[3];
                  rgb888[0] = (rgb565 >> 8) & 0xf8;
                  rgb888[1] = (rgb565 >> 3) & 0xfc;
                  rgb888[2] = (rgb565 << 3);
                  rgb888[0] |= (rgb888[0] >> 5);
                  rgb888[1] |= (rgb888[1] >> 6);
                  rgb888[2] |= (rgb888[2] >> 5);
                  memcpy(&renderBuffer
                             [currentRenderBuffer]
                             [((yOffset + y) * TOTAL_WIDTH + xOffset + x) * 3],
                         rgb888, 3);
                }
              }
              xSemaphoreGive(xRenderMutex);
            }
#else
            display->FillZoneRaw565(
                uncompressBuffer[uncompressedBufferPosition++],
                &uncompressBuffer[uncompressedBufferPosition]);
            uncompressedBufferPosition += RGB565_ZONE_SIZE;
#endif
          }
        }
      } else {
        // display->DisplayText("miniz error ", 0, 0, 255, 0, 0);
        // DisplayNumber(minizStatus, 3, 0, 6, 255, 0, 0);
        // while (1);
      }
    }
  }

  // Move to the next buffer
  processingBuffer = (processingBuffer + 1) % NUM_BUFFERS;
}
