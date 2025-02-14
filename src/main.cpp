
#include <Arduino.h>
#include <AsyncUDP.h>
#include <Bounce2.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <cstring>

// Specific improvements and #define for the ESP32 S3 series
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#include "S3Specific.h"
#endif
#include "displayDriver.h"  // Base class for all display drivers
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "miniz/miniz.h"
#include "panel.h"
#include "version.h"

// To save RAM only include the driver we want to use.
#ifdef DISPLAY_RM67162_AMOLED
#include "displays/Rm67162Amoled.h"
#else
#include "displays/LEDMatrix.h"
#endif

#define N_FRAME_CHARS 5
#define N_CTRL_CHARS 5
#define N_ACK_CHARS (N_CTRL_CHARS + 1)
#define N_INTERMEDIATE_CTR_CHARS 4
#ifdef BOARD_HAS_PSRAM
#define NUM_BUFFERS 128  // Number of buffers
#ifdef DISPLAY_RM67162_AMOLED
// @fixme double buffering doesn't work on Lilygo Amoled
#define NUM_RENDER_BUFFERS 1
#else
#define NUM_RENDER_BUFFERS 2
#endif
#define BUFFER_SIZE 1152
#else
#define NUM_BUFFERS 12  // Number of buffers
#define NUM_RENDER_BUFFERS 1
#define BUFFER_SIZE 1152
#endif
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#if (defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1)
// USB CDC
#define SERIAL_BAUD 115200
#define USB_PACKAGE_SIZE 512
#else
// UART
#define SERIAL_BAUD 2000000
#define USB_PACKAGE_SIZE 32
#endif
#else
#define SERIAL_BAUD 921600
#define USB_PACKAGE_SIZE 32
#endif
#define SERIAL_TIMEOUT \
  8  // Time in milliseconds to wait for the next data chunk.

#define CONNECTION_TIMEOUT 5000

#ifdef ARDUINO_ESP32_S3_N16R8
#define UP_BUTTON_PIN 0
#define DOWN_BUTTON_PIN 45
#define FORWARD_BUTTON_PIN 48
#define BACKWARD_BUTTON_PIN 47
#elif defined(DISPLAY_RM67162_AMOLED)
#define UP_BUTTON_PIN 0
#define FORWARD_BUTTON_PIN 21
#else
#define UP_BUTTON_PIN 21
#define FORWARD_BUTTON_PIN 33
#endif

#define LED_CHECK_DELAY 1000  // ms per color

#define RC 0
#define GC 1
#define BC 2

enum {
  TRANSPORT_USB = 0,
  TRANSPORT_WIFI_UDP = 1,
  TRANSPORT_WIFI_TCP = 2,
  TRANSPORT_SPI = 3
};

const uint8_t FrameChars[5]
    __attribute__((aligned(4))) = {'F', 'R', 'A', 'M', 'E'};
const uint8_t CtrlChars[6]
    __attribute__((aligned(4))) = {'Z', 'e', 'D', 'M', 'D', 'A'};
uint8_t numCtrlCharsFound = 0;

AsyncWebServer *server;
AsyncServer *tcp;
AsyncUDP *udp;
DisplayDriver *display;

static portMUX_TYPE bufferMutex = portMUX_INITIALIZER_UNLOCKED;

// Buffers for storing data
uint8_t *buffers[NUM_BUFFERS];
uint16_t bufferSizes[NUM_BUFFERS] __attribute__((aligned(4))) = {0};
bool bufferCompressed[NUM_BUFFERS] __attribute__((aligned(4))) = {0};

// The uncompress buffer should be bug enough
uint8_t uncompressBuffer[2048] __attribute__((aligned(4)));
uint8_t *renderBuffer[NUM_RENDER_BUFFERS];
uint8_t currentRenderBuffer __attribute__((aligned(4)));
uint8_t lastRenderBuffer __attribute__((aligned(4)));
char tmpStringBuffer[33] __attribute__((aligned(4))) = {0};
bool payloadCompressed __attribute__((aligned(4)));
uint16_t payloadSize __attribute__((aligned(4)));
uint16_t payloadMissing __attribute__((aligned(4)));
uint8_t headerBytesReceived __attribute__((aligned(4)));
uint8_t command __attribute__((aligned(4)));
uint8_t currentBuffer __attribute__((aligned(4)));
uint8_t lastBuffer __attribute__((aligned(4)));
uint8_t processingBuffer __attribute__((aligned(4)));

// Init display on a low brightness to avoid power issues, but bright enough to
// see something.
#ifdef DISPLAY_RM67162_AMOLED
uint8_t brightness = 5;
#else
uint8_t brightness = 2;
int8_t rgbMode = 0;
uint8_t rgbModeLoaded = 0;
int8_t yOffset = 0;
uint8_t panelClkphase = 0;
uint8_t panelDriver = 0;
uint8_t panelI2sspeed = 8;
uint8_t panelLatchBlanking = 2;
uint8_t panelMinRefreshRate = 30;

// I needed to change these from RGB to RC (Red Color), BC, GC to prevent
// conflicting with the TFT_SPI Library.
const uint8_t rgbOrder[3 * 6] = {
    RC, GC, BC,  // rgbMode 0
    BC, RC, GC,  // rgbMode 1
    GC, BC, RC,  // rgbMode 2
    RC, BC, GC,  // rgbMode 3
    GC, RC, BC,  // rgbMode 4
    BC, GC, RC   // rgbMode 5
};

#endif
uint8_t usbPackageSizeMultiplier = USB_PACKAGE_SIZE / 32;
uint8_t settingsMenu = 0;
uint8_t debug = 0;
uint8_t udpDelay = 5;

String ssid;
String pwd;
uint16_t port = 3333;
uint8_t ssid_length;
uint8_t pwd_length;
bool wifiActive;
#ifdef ZEDMD_WIFI
int8_t transport = TRANSPORT_WIFI_UDP;
#else
int8_t transport = TRANSPORT_USB;
#endif
bool logoActive;
bool transportActive;
uint8_t transportWaitCounter;
uint8_t logoWaitCounter;
uint32_t lastDataReceived;
bool serverRunning;
uint8_t throbberColors[6] __attribute__((aligned(4))) = {0};

void DoRestart(int sec) {
  if (wifiActive) {
    MDNS.end();
    WiFi.disconnect(true);
  }
  display->ClearScreen();
  display->DisplayText("Restarting ...", 0, 0, 255, 0, 0);
  vTaskDelay(pdMS_TO_TICKS(sec * 1000));
  display->ClearScreen();
  delay(20);

  // Note: ESP.restart() or esp_restart() will keep the state of global and
  // static variables. And not all sub-systems get resetted.
#if (defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1)
  esp_sleep_enable_timer_wakeup(1000);  // Wake up after 1ms
  esp_deep_sleep_start();  // Enter deep sleep (ESP32 reboots on wake)
#else
  esp_restart();
#endif
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
  display->DisplayText(version, TOTAL_WIDTH - (strlen(version) * 4) - 5,
                       TOTAL_HEIGHT - 5, 255 * !logo, 255 * !logo, 255 * !logo,
                       logo);
}

void DisplayLum(uint8_t r = 128, uint8_t g = 128, uint8_t b = 128) {
  display->DisplayText(" ", (TOTAL_WIDTH / 2) - 26 - 1, TOTAL_HEIGHT - 6, r, g,
                       b);
  display->DisplayText("Brightness:", (TOTAL_WIDTH / 2) - 26, TOTAL_HEIGHT - 6,
                       r, g, b);
  DisplayNumber(brightness, 2, (TOTAL_WIDTH / 2) + 18, TOTAL_HEIGHT - 6, 255,
                191, 0);
}

void DisplayRGB(uint8_t r = 128, uint8_t g = 128, uint8_t b = 128) {
#ifndef DISPLAY_RM67162_AMOLED
  display->DisplayText("red", 0, 0, 0, 0, 0, true, true);
  for (uint8_t i = 0; i < 6; i++) {
    display->DrawPixel(TOTAL_WIDTH - (4 * 4) - 1, i, 0, 0, 0);
    display->DrawPixel((TOTAL_WIDTH / 2) - (6 * 4) - 1, i, 0, 0, 0);
  }
  display->DisplayText("blue", TOTAL_WIDTH - (4 * 4), 0, 0, 0, 0, true, true);
  display->DisplayText("green", 0, TOTAL_HEIGHT - 6, 0, 0, 0, true, true);
  display->DisplayText("RGB Order:", (TOTAL_WIDTH / 2) - (6 * 4), 0, r, g, b);
  DisplayNumber(rgbMode, 2, (TOTAL_WIDTH / 2) + (4 * 4), 0, 255, 191, 0);
#endif
}

/// @brief Get DisplayDriver object, required for webserver
DisplayDriver *GetDisplayObject() { return display; }

void SaveSettingsMenu() {
  File f = LittleFS.open("/settings_menu.val", "w");
  f.write(settingsMenu);
  f.close();
}

void LoadSettingsMenu() {
  File f = LittleFS.open("/settings_menu.val", "r");
  if (!f) {
#if !defined(DISPLAY_RM67162_AMOLED)
    // Show settings menu on freshly installed device
    settingsMenu = 1;
#endif
    SaveSettingsMenu();
    return;
  }
  settingsMenu = f.read();
  f.close();
}

void SaveTransport() {
  File f = LittleFS.open("/transport.val", "w");
  f.write(transport);
  f.close();
}

void LoadTransport() {
  File f = LittleFS.open("/transport.val", "r");
  if (!f) {
    SaveTransport();
    return;
  }
  transport = f.read();
  f.close();
}

#ifdef DISPLAY_LED_MATRIX
void SaveRgbOrder() {
  File f = LittleFS.open("/rgb_order.val", "w");
  f.write(rgbMode);
  f.close();
}

void LoadRgbOrder() {
  File f = LittleFS.open("/rgb_order.val", "r");
  if (!f) {
    SaveRgbOrder();
    return;
  }
  rgbMode = rgbModeLoaded = f.read();
  f.close();
}

void SavePanelSettings() {
  File f = LittleFS.open("/panel_clkphase.val", "w");
  f.write(panelClkphase);
  f.close();
  f = LittleFS.open("/panel_driver.val", "w");
  f.write(panelDriver);
  f.close();
  f = LittleFS.open("/panel_i2sspeed.val", "w");
  f.write(panelI2sspeed);
  f.close();
  f = LittleFS.open("/panel_latch_blanking.val", "w");
  f.write(panelLatchBlanking);
  f.close();
  f = LittleFS.open("/panel_min_refresh_rate.val", "w");
  f.write(panelMinRefreshRate);
  f.close();
}

void LoadPanelSettings() {
  File f = LittleFS.open("/panel_clkphase.val", "r");
  if (!f) {
    SavePanelSettings();
  }
  panelClkphase = f.read();
  f.close();
  f = LittleFS.open("/panel_driver.val", "r");
  if (!f) {
    SavePanelSettings();
  }
  panelDriver = f.read();
  f.close();
  f = LittleFS.open("/panel_i2sspeed.val", "r");
  if (!f) {
    SavePanelSettings();
  }
  panelI2sspeed = f.read();
  f.close();
  f = LittleFS.open("/panel_latch_blanking.val", "r");
  if (!f) {
    SavePanelSettings();
  }
  panelLatchBlanking = f.read();
  f.close();
  f = LittleFS.open("/panel_min_refresh_rate.val", "r");
  if (!f) {
    SavePanelSettings();
  }
  panelMinRefreshRate = f.read();
  f.close();
}

#endif

void SaveLum() {
  File f = LittleFS.open("/lum.val", "w");
  f.write(brightness);
  f.close();
}

void LoadLum() {
  File f = LittleFS.open("/lum.val", "r");
  if (!f) {
    SaveLum();
    return;
  }
  brightness = f.read();
  f.close();
}

void SaveDebug() {
  File f = LittleFS.open("/debug.val", "w");
  f.write(debug);
  f.close();
}

void LoadDebug() {
  File f = LittleFS.open("/debug.val", "r");
  if (!f) {
    SaveDebug();
    return;
  }
  debug = f.read();
  f.close();
}

void SaveUsbPackageSizeMultiplier() {
  File f = LittleFS.open("/usb_size.val", "w");
  f.write(usbPackageSizeMultiplier);
  f.close();
}

void LoadUsbPackageSizeMultiplier() {
  File f = LittleFS.open("/usb_size.val", "r");
  if (!f) {
    SaveUsbPackageSizeMultiplier();
    return;
  }
  usbPackageSizeMultiplier = f.read();
  f.close();
}

void SaveUdpDelay() {
  File f = LittleFS.open("/udp_delay.val", "w");
  f.write(udpDelay);
  f.close();
}

void LoadUdpDelay() {
  File f = LittleFS.open("/udp_delay.val", "r");
  if (!f) {
    SaveUdpDelay();
    return;
  }
  udpDelay = f.read();
  f.close();
}

#ifdef ZEDMD_HD_HALF
void SaveYOffset() {
  File f = LittleFS.open("/y_offset.val", "w");
  f.write(yOffset);
  f.close();
}

void LoadYOffset() {
  File f = LittleFS.open("/y_offset.val", "r");
  if (!f) {
    SaveYOffset();
    return;
  }
  yOffset = f.read();
  f.close();
}
#endif

void SaveScale() {
  File f = LittleFS.open("/scale.val", "w");
  f.write(display->GetCurrentScalingMode());
  f.close();
}

void LoadScale() {
  File f = LittleFS.open("/scale.val", "r");
  if (!f) {
    SaveScale();
    return;
  }
  display->SetCurrentScalingMode(f.read());
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

void AcquireNextBuffer() {
  // currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
  // return;
  while (1) {
    portENTER_CRITICAL(&bufferMutex);
    if (currentBuffer == lastBuffer &&
        ((currentBuffer + 1) % NUM_BUFFERS) != processingBuffer) {
      currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
      portEXIT_CRITICAL(&bufferMutex);
      return;
    }
    portEXIT_CRITICAL(&bufferMutex);
    // Avoid busy-waiting
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void MarkCurrentBufferDone() {
  portENTER_CRITICAL(&bufferMutex);
  lastBuffer = currentBuffer;
  portEXIT_CRITICAL(&bufferMutex);
}

bool AcquireNextProcessingBuffer() {
  if (processingBuffer != currentBuffer &&
      (((processingBuffer + 1) % NUM_BUFFERS) != currentBuffer ||
       currentBuffer == lastBuffer)) {
    processingBuffer = (processingBuffer + 1) % NUM_BUFFERS;
    return true;
  }
  return false;
}

void Render() {
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
}

void ClearScreen() {
  display->ClearScreen();
  memset(renderBuffer[currentRenderBuffer], 0, TOTAL_BYTES);

  if (NUM_RENDER_BUFFERS > 1) {
    lastRenderBuffer = currentRenderBuffer;
    currentRenderBuffer = (currentRenderBuffer + 1) % NUM_RENDER_BUFFERS;
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
#ifndef DISPLAY_RM67162_AMOLED
  for (uint16_t tj = 0; tj < TOTAL_BYTES; tj += 3) {
    if (rgbMode == rgbModeLoaded) {
      renderBuffer[currentRenderBuffer][tj] = f.read();
      renderBuffer[currentRenderBuffer][tj + 1] = f.read();
      renderBuffer[currentRenderBuffer][tj + 2] = f.read();
    } else {
      renderBuffer[currentRenderBuffer][tj + rgbOrder[rgbMode * 3]] = f.read();
      renderBuffer[currentRenderBuffer][tj + rgbOrder[rgbMode * 3 + 1]] =
          f.read();
      renderBuffer[currentRenderBuffer][tj + rgbOrder[rgbMode * 3 + 2]] =
          f.read();
    }
  }
#else
  for (uint16_t tj = 0; tj < TOTAL_BYTES; tj++) {
    renderBuffer[currentRenderBuffer][tj] = f.read();
  }
#endif
  f.close();

  Render();
  DisplayVersion(true);

  throbberColors[0] = 0;
  throbberColors[1] = 0;
  throbberColors[2] = 0;
  throbberColors[3] = 255;
  throbberColors[4] = 255;
  throbberColors[5] = 255;

  logoActive = true;
  logoWaitCounter = 0;
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

  for (uint16_t tj = 0; tj < TOTAL_BYTES; tj++) {
    renderBuffer[currentRenderBuffer][tj] = f.read();
  }

  f.close();

  Render();

  throbberColors[0] = 0;
  throbberColors[1] = 0;
  throbberColors[2] = 0;
  throbberColors[3] = 255;
  throbberColors[4] = 255;
  throbberColors[5] = 0;
}

void ScreenSaver() {
  ClearScreen();

  throbberColors[0] = 48;
  throbberColors[1] = 0;
  throbberColors[2] = 0;
  throbberColors[3] = 0;
  throbberColors[4] = 0;
  throbberColors[5] = 0;
}

void RefreshSetupScreen() {
  DisplayLogo();
  for (uint16_t y = (TOTAL_HEIGHT / 32 * 5);
       y < TOTAL_HEIGHT - (TOTAL_HEIGHT / 32 * 5); y++) {
    for (uint16_t x = (TOTAL_WIDTH / 128 * 5);
         x < TOTAL_WIDTH - (TOTAL_WIDTH / 128 * 5); x++) {
      display->DrawPixel(x, y, 0, 0, 0);
    }
  }
  DisplayRGB();
  DisplayLum();
  display->DisplayText(
      transport == TRANSPORT_USB
          ? "USB "
          : (transport == TRANSPORT_WIFI_UDP
                 ? "WiFi UDP"
                 : (transport == TRANSPORT_WIFI_TCP ? "WiFi TCP" : "SPI ")),
      7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
  display->DisplayText("Debug:", 7 * (TOTAL_WIDTH / 128),
                       (TOTAL_HEIGHT / 2) - 10, 128, 128, 128);
  DisplayNumber(debug, 1, 7 * (TOTAL_WIDTH / 128) + (6 * 4),
                (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
  display->DisplayText("USB Packet Size:", 7 * (TOTAL_WIDTH / 128),
                       (TOTAL_HEIGHT / 2) + 4, 128, 128, 128);
  DisplayNumber(usbPackageSizeMultiplier * 32, 4,
                7 * (TOTAL_WIDTH / 128) + (16 * 4), (TOTAL_HEIGHT / 2) + 4, 255,
                191, 0);
  display->DisplayText(
      "UDP Delay:", TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - (11 * 4),
      (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
  DisplayNumber(udpDelay, 1, TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 4,
                (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);

#ifdef ZEDMD_HD_HALF
  display->DisplayText("Y-Offset", TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 32,
                       (TOTAL_HEIGHT / 2) - 10, 128, 128, 128);
#endif
  display->DisplayText("Exit", TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                       (TOTAL_HEIGHT / 2) + 4, 128, 128, 128);
}

static uint8_t IRAM_ATTR HandleData(uint8_t *pData, size_t len) {
  uint16_t pos = 0;
  bool headerCompleted = false;

  while (pos < len ||
         (headerCompleted && command != 5 && command != 22 && command != 23 &&
          command != 27 && command != 28 && command != 29 && command != 40 &&
          command != 41 && command != 42 && command != 43 && command != 44 &&
          command != 45 && command != 46 && command != 47 && command != 48)) {
    headerCompleted = false;
    if (numCtrlCharsFound < N_CTRL_CHARS) {
      // Detect 5 consecutive start bits
      if (pData[pos++] == CtrlChars[numCtrlCharsFound]) {
        numCtrlCharsFound++;
      } else {
        numCtrlCharsFound = 0;
      }
    } else if (numCtrlCharsFound == N_CTRL_CHARS) {
      if (headerBytesReceived == 0) {
        command = pData[pos++];
        ++headerBytesReceived;
        continue;
      } else if (headerBytesReceived == 1) {
        payloadSize = pData[pos++] << 8;
        ++headerBytesReceived;
        continue;
      } else if (headerBytesReceived == 2) {
        payloadSize |= pData[pos++];
        payloadMissing = payloadSize;
        ++headerBytesReceived;
        continue;
      } else if (headerBytesReceived == 3) {
        payloadCompressed = (bool)pData[pos++];
        ++headerBytesReceived;
        headerCompleted = true;
        continue;
      } else if (headerBytesReceived == 4) {
        esp_task_wdt_reset();
        if (payloadSize > BUFFER_SIZE) {
          if (debug) {
            portENTER_CRITICAL(&bufferMutex);
            display->DisplayText("Error, payloadSize > BUFFER_SIZE", 0, 0, 255,
                                 0, 0);
            DisplayNumber(payloadSize, 5, 0, 19, 255, 0, 0);
            DisplayNumber(BUFFER_SIZE, 5, 0, 25, 255, 0, 0);
            portEXIT_CRITICAL(&bufferMutex);
            while (1);
          }
          headerBytesReceived = 0;
          numCtrlCharsFound = 0;
          return 2;
        }

        if (debug) {
          portENTER_CRITICAL(&bufferMutex);
          display->DisplayText("Command:", 7 * (TOTAL_WIDTH / 128),
                               (TOTAL_HEIGHT / 2) - 10, 128, 128, 128);
          DisplayNumber(command, 2, 7 * (TOTAL_WIDTH / 128) + (8 * 4),
                        (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
          display->DisplayText("Payload:", 7 * (TOTAL_WIDTH / 128),
                               (TOTAL_HEIGHT / 2) - 4, 128, 128, 128);
          DisplayNumber(payloadSize, 2, 7 * (TOTAL_WIDTH / 128) + (8 * 4),
                        (TOTAL_HEIGHT / 2) - 4, 255, 191, 0);
          portEXIT_CRITICAL(&bufferMutex);
        }

        switch (command) {
          case 12:  // handshake
          {
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;

            // Including the ACK, the response will be 64 bytes long. That
            // leaves some space for future features.
            uint8_t *response = (uint8_t *)malloc(64 - N_ACK_CHARS);
            memset(response, 0, 64 - N_ACK_CHARS);
            memcpy(response, CtrlChars, N_INTERMEDIATE_CTR_CHARS);
            response[N_INTERMEDIATE_CTR_CHARS] = TOTAL_WIDTH & 0xff;
            response[N_INTERMEDIATE_CTR_CHARS + 1] = (TOTAL_WIDTH >> 8) & 0xff;
            response[N_INTERMEDIATE_CTR_CHARS + 2] = TOTAL_HEIGHT & 0xff;
            response[N_INTERMEDIATE_CTR_CHARS + 3] = (TOTAL_HEIGHT >> 8) & 0xff;
            response[N_INTERMEDIATE_CTR_CHARS + 4] = ZEDMD_VERSION_MAJOR;
            response[N_INTERMEDIATE_CTR_CHARS + 5] = ZEDMD_VERSION_MINOR;
            response[N_INTERMEDIATE_CTR_CHARS + 6] = ZEDMD_VERSION_PATCH;
            response[N_INTERMEDIATE_CTR_CHARS + 7] =
                (usbPackageSizeMultiplier * 32) & 0xff;
            response[N_INTERMEDIATE_CTR_CHARS + 8] =
                ((usbPackageSizeMultiplier * 32) >> 8) & 0xff;
            response[N_INTERMEDIATE_CTR_CHARS + 9] = brightness;
#ifndef DISPLAY_RM67162_AMOLED
            response[N_INTERMEDIATE_CTR_CHARS + 10] = rgbMode;
            response[N_INTERMEDIATE_CTR_CHARS + 11] = yOffset;
            response[N_INTERMEDIATE_CTR_CHARS + 12] = panelClkphase;
            response[N_INTERMEDIATE_CTR_CHARS + 13] = panelDriver;
            response[N_INTERMEDIATE_CTR_CHARS + 14] = panelI2sspeed;
            response[N_INTERMEDIATE_CTR_CHARS + 15] = panelLatchBlanking;
            response[N_INTERMEDIATE_CTR_CHARS + 16] = panelMinRefreshRate;
#endif
            response[N_INTERMEDIATE_CTR_CHARS + 17] = udpDelay;
#ifdef ZEDMD_HD_HALF
            response[N_INTERMEDIATE_CTR_CHARS + 18] = 1;
#else
            response[N_INTERMEDIATE_CTR_CHARS + 18] = 0;
#endif
            response[63 - N_ACK_CHARS] = 'R';
            Serial.write(response, 64 - N_ACK_CHARS);
            // This flush is required for USB CDC on Windows.
            Serial.flush();
            free(response);
            return 1;
          }

          case 22:  // set brightness
          {
            brightness = pData[pos++];
            display->SetBrightness(brightness);
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }
#ifndef DISPLAY_RM67162_AMOLED
          case 23:  // set RGB order
          {
            rgbMode = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }
#endif
          case 27:  // set SSID
          {
            if (payloadMissing == payloadSize) {
              memset(tmpStringBuffer, 0, 33);
              if (payloadMissing > (len - pos)) {
                memcpy(tmpStringBuffer, &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(tmpStringBuffer, &pData[pos], payloadSize);
                ssid = String(tmpStringBuffer);
                ssid_length = payloadSize;
                pos += payloadSize;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            } else {
              if (payloadMissing > (len - pos)) {
                memcpy(&tmpStringBuffer[payloadSize - payloadMissing],
                       &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(&tmpStringBuffer[payloadSize - payloadMissing],
                       &pData[pos], payloadMissing);
                ssid = String(tmpStringBuffer);
                ssid_length = payloadSize;
                pos += payloadMissing;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            if (wifiActive) break;
            return 1;
          }

          case 28:  // set password
          {
            if (payloadMissing == payloadSize) {
              memset(tmpStringBuffer, 0, 33);
              if (payloadMissing > (len - pos)) {
                memcpy(tmpStringBuffer, &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(tmpStringBuffer, &pData[pos], payloadSize);
                pwd = String(tmpStringBuffer);
                pwd_length = payloadSize;
                pos += payloadSize;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            } else {
              if (payloadMissing > (len - pos)) {
                memcpy(&tmpStringBuffer[payloadSize - payloadMissing],
                       &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(&tmpStringBuffer[payloadSize - payloadMissing],
                       &pData[pos], payloadMissing);
                pwd = String(tmpStringBuffer);
                pwd_length = payloadSize;
                pos += payloadMissing;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            if (wifiActive) break;
            return 1;
          }

          case 29:  // set port
          {
            if (payloadMissing == payloadSize) {
              memset(tmpStringBuffer, 0, 33);
              if (payloadMissing > (len - pos)) {
                memcpy(tmpStringBuffer, &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(tmpStringBuffer, &pData[pos], payloadSize);
                port = tmpStringBuffer[0] << 8;
                port |= tmpStringBuffer[1];
                pos += payloadSize;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            } else {
              if (payloadMissing > (len - pos)) {
                memcpy(&tmpStringBuffer[payloadSize - payloadMissing],
                       &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(&tmpStringBuffer[payloadSize - payloadMissing],
                       &pData[pos], payloadMissing);
                port = tmpStringBuffer[0] << 8;
                port |= tmpStringBuffer[1];
                pos += payloadMissing;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            if (wifiActive) break;
            return 1;
          }

          case 30:  // save settings 0x1e
          {
            if (!wifiActive) {
              // send fast ack
              Serial.write(CtrlChars, N_ACK_CHARS);
              Serial.flush();
            }
            display->DisplayText("Saving settings ...", 0, 0, 255, 0, 0);
            SaveLum();
            SaveDebug();
            SaveTransport();
            SaveUsbPackageSizeMultiplier();
            SaveUdpDelay();
            SaveWiFiConfig();
#ifdef DISPLAY_LED_MATRIX
            SaveRgbOrder();
            SavePanelSettings();
#endif
#ifdef ZEDMD_HD_HALF
            SaveYOffset();
#endif
            display->DisplayText("Saving settings ... done", 0, 0, 255, 0, 0);
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 3;
          }

          case 31:  // reset 0x1f
          {
            if (!wifiActive) {
              Serial.write(CtrlChars, N_ACK_CHARS);
              Serial.flush();
            }
            Restart();
          }
#ifndef DISPLAY_RM67162_AMOLED
          case 40:  // set panelClkphase
          {
            panelClkphase = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 41:  // set panelI2sspeed
          {
            panelI2sspeed = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 42:  // set panelLatchBlanking
          {
            panelLatchBlanking = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 43:  // set panelMinRefreshRate
          {
            panelMinRefreshRate = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 44:  // set panelDriver
          {
            panelDriver = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }
#endif
          case 45:  // set transport
          {
            transport = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 46:  // set udpDelay
          {
            udpDelay = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 47:  // set usbPackageSizeMultiplier
          {
            usbPackageSizeMultiplier = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }
#ifndef DISPLAY_RM67162_AMOLED
          case 48:  // set yOffset
          {
            yOffset = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }
#endif
          case 16: {
            if (!wifiActive) {
              Serial.write(CtrlChars, N_ACK_CHARS);
              Serial.flush();
            }
            LedTester();
            Restart();
          }

          case 10: {  // Clear screen
            AcquireNextBuffer();
            bufferCompressed[currentBuffer] = false;
            bufferSizes[currentBuffer] = 2;
            buffers[currentBuffer][0] = 0;
            buffers[currentBuffer][1] = 0;
            MarkCurrentBufferDone();
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 11:  // KeepAlive
          {
            if (debug) {
              portENTER_CRITICAL(&bufferMutex);
              display->DisplayText("KEEP ALIVE RECEIVED",
                                   7 * (TOTAL_WIDTH / 128),
                                   (TOTAL_HEIGHT / 2) - 10, 128, 128, 128);
              portEXIT_CRITICAL(&bufferMutex);
            }
            lastDataReceived = millis();
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 98:  // disable debug mode
          {
            debug = 0;
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 99:  // enable debug mode
          {
            debug = 1;
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          case 5: {  // RGB565 Zones Stream
            if (payloadMissing == payloadSize) {
              AcquireNextBuffer();
              bufferCompressed[currentBuffer] = payloadCompressed;
              bufferSizes[currentBuffer] = payloadSize;
              if (payloadMissing > (len - pos)) {
                memcpy(&buffers[currentBuffer][0], &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(&buffers[currentBuffer][0], &pData[pos], payloadSize);
                pos += payloadSize;
                MarkCurrentBufferDone();
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            } else {
              if (payloadMissing > (len - pos)) {
                memcpy(&buffers[currentBuffer][payloadSize - payloadMissing],
                       &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(&buffers[currentBuffer][payloadSize - payloadMissing],
                       &pData[pos], payloadMissing);
                pos += payloadMissing;
                MarkCurrentBufferDone();
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            break;
          }

          case 6: {  // Render
#if defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)
            AcquireNextBuffer();
            bufferCompressed[currentBuffer] = false;
            bufferSizes[currentBuffer] = 2;
            buffers[currentBuffer][0] = 255;
            buffers[currentBuffer][1] = 255;
            MarkCurrentBufferDone();
#endif
            lastDataReceived = millis();
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }

          default: {
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (wifiActive) break;
            return 1;
          }
        }
      }
    }
  }

  return 0;
}

void Task_ReadSerial(void *pvParameters) {
  const uint16_t usbPackageSize = usbPackageSizeMultiplier * 32;
  bool connected = false;

  Serial.setRxBufferSize(usbPackageSize + 128);
  Serial.setTxBufferSize(64);
#if (defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1)
  // S3 USB CDC. The actual baud rate doesn't matter.
  Serial.begin(115200);
  while (!Serial) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  display->DisplayText("USB CDC", 0, 0, 0, 0, 0, 1);
#else
  Serial.setTimeout(SERIAL_TIMEOUT);
  Serial.begin(SERIAL_BAUD);
  while (!Serial) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (1 == debug) {
    DisplayNumber(SERIAL_BAUD, (SERIAL_BAUD >= 1000000 ? 7 : 6), 0, 0, 0, 0, 0,
                  1);
  } else {
    display->DisplayText("USB UART", 0, 0, 0, 0, 0, 1);
  }
#endif

#ifdef BOARD_HAS_PSRAM
  uint8_t *pUsbBuffer = (uint8_t *)heap_caps_malloc(
      usbPackageSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
#else
  uint8_t *pUsbBuffer = (uint8_t *)malloc(usbPackageSize);
#endif

  if (nullptr == pUsbBuffer) {
    display->DisplayText("out of memory", 0, 0, 255, 0, 0);
    while (1);
  }

  payloadMissing = 0;
  headerBytesReceived = 0;
  numCtrlCharsFound = 0;

  int16_t received = 0;
  int16_t expected = 0;
  uint16_t noDataMs = 0;
  uint8_t numFrameCharsFound = 0;
  uint8_t result = 0;

  while (1) {
    noDataMs = 0;
    numFrameCharsFound = 0;
    // Wait for FRAME header
    while (numFrameCharsFound < N_FRAME_CHARS) {
      if (Serial.available()) {
        if (Serial.read() == FrameChars[numFrameCharsFound]) {
          numFrameCharsFound++;
        } else {
          numFrameCharsFound = 0;
        }
      } else {
        if (++noDataMs > 5000) {
          transportActive = false;
          noDataMs = 0;
        }
        // Avoid busy-waiting
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }

    expected = usbPackageSize - N_FRAME_CHARS;
    transportActive = true;
    noDataMs = 0;
    result = 0;

    while (1) {
      // Wait for data to be ready
      if (Serial.available() >= expected ||
          (!connected && Serial.available() >= (N_CTRL_CHARS + 4))) {
        memset(pUsbBuffer, 0, usbPackageSize);
        received = Serial.readBytes(pUsbBuffer, expected);
        result = HandleData(pUsbBuffer, received);
        expected = usbPackageSize;
        if (2 == result) {  // Error
          Serial.write(CtrlChars, N_CTRL_CHARS);
          Serial.write('F');
          Serial.flush();
          vTaskDelay(pdMS_TO_TICKS(2));
          Serial.end();
          vTaskDelay(pdMS_TO_TICKS(10));
          Serial.begin(SERIAL_BAUD);
          while (!Serial) {
            vTaskDelay(pdMS_TO_TICKS(10));
          }
          break;  // Wait for the next FRAME header
        }
        connected = true;
        if (3 == result) {
          break;  // fast ack has been sent, wait for the next FRAME header
        }
        Serial.write(CtrlChars, N_ACK_CHARS);
        Serial.flush();
        if (1 == result) break;  // Wait for the next FRAME header
        noDataMs = 0;
      } else {
        if (++noDataMs > 5000) {
          transportActive = false;
          noDataMs = 0;
          break;  // Wait for the next FRAME header
        }
        // Avoid busy-waiting
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
  }
}

static void HandleUdpPacket(AsyncUDPPacket packet) {
  static bool isProcessing = false;

  if (!isProcessing) {
    isProcessing = true;
    transportActive = true;
    HandleData(packet.data(), packet.length());
    yield();
    isProcessing = false;
  }
}

static void HandleTcpData(void *arg, AsyncClient *client, void *data,
                          size_t len) {
  HandleData((uint8_t *)data, len);
  client->ack(len);
}

static void HandleTcpDisconnect(void *arg, AsyncClient *client) {
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

static void NewTcpClient(void *arg, AsyncClient *client) {
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
  client->onData(&HandleTcpData, NULL);
  client->onDisconnect(&HandleTcpDisconnect, NULL);
}

void StartServer() {
  server = new AsyncWebServer(80);

  // Serve index.html
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", String(), false);
  });

  // Handle AJAX request to save WiFi configuration
  server->on("/save_wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
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
        Restart();
      } else {
        request->send(500, "text/plain", "Failed to save config!");
      }
    } else {
      request->send(400, "text/plain", "Missing parameters!");
    }
  });

  server->on("/wifi_status", HTTP_GET, [](AsyncWebServerRequest *request) {
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

#ifndef DISPLAY_RM67162_AMOLED
  // Route to save RGB order
  server->on("/save_rgb_order", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("rgbOrder", true)) {
      if (rgbModeLoaded != 0) {
        request->send(200, "text/plain",
                      "ZeDMD needs to reboot first before the RGB order can be "
                      "adjusted. Try again in a few seconds.");

        rgbMode = 0;
        SaveRgbOrder();
        Restart();
      }

      String rgbOrderValue = request->getParam("rgbOrder", true)->value();
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
  server->on("/save_brightness", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("brightness", true)) {
      String brightnessValue = request->getParam("brightness", true)->value();
      brightness = brightnessValue.toInt();
      GetDisplayObject()->SetBrightness(brightness);
      SaveLum();
      RefreshSetupScreen();
      request->send(200, "text/plain", "Brightness updated successfully");
    } else {
      request->send(400, "text/plain", "Missing brightness parameter");
    }
  });

  server->on("/get_version", HTTP_GET, [](AsyncWebServerRequest *request) {
    String version = String(ZEDMD_VERSION_MAJOR) + "." +
                     String(ZEDMD_VERSION_MINOR) + "." +
                     String(ZEDMD_VERSION_PATCH);
    request->send(200, "text/plain", version);
  });

  server->on("/get_height", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(TOTAL_HEIGHT));
  });

  server->on("/get_width", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(TOTAL_WIDTH));
  });
#ifndef DISPLAY_RM67162_AMOLED
  server->on("/get_rgb_order", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(rgbMode));
  });

  server->on("/get_panel_clkphase", HTTP_GET,
             [](AsyncWebServerRequest *request) {
               request->send(200, "text/plain", String(panelClkphase));
             });

  server->on("/get_panel_driver", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(panelDriver));
  });

  server->on("/get_panel_i2sspeed", HTTP_GET,
             [](AsyncWebServerRequest *request) {
               request->send(200, "text/plain", String(panelI2sspeed));
             });

  server->on("/get_panel_latchblanking", HTTP_GET,
             [](AsyncWebServerRequest *request) {
               request->send(200, "text/plain", String(panelLatchBlanking));
             });

  server->on("/get_panel_minrefreshrate", HTTP_GET,
             [](AsyncWebServerRequest *request) {
               request->send(200, "text/plain", String(panelMinRefreshRate));
             });

  server->on("/get_y_offset", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(yOffset));
  });
#endif
  server->on("/get_udp_delay", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(udpDelay));
  });

  server->on("/get_brightness", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(brightness));
  });

  server->on("/get_protocol", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (TRANSPORT_WIFI_UDP == transport) {
      request->send(200, "text/plain", "UDP");
    } else {
      request->send(200, "text/plain", "TCP");
    }
  });

  server->on("/get_port", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(port));
  });

  server->on(
      "/get_usb_package_size", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(usbPackageSizeMultiplier * 32));
      });

  server->on("/get_ssid", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", ssid);
  });

  server->on("/get_s3", HTTP_GET, [](AsyncWebServerRequest *request) {
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
    request->send(200, "text/plain", String(1));
#else
    request->send(200, "text/plain", String(0));
#endif
  });

  server->on("/handshake", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(
        200, "text/plain",
        String(TOTAL_WIDTH) + "|" + String(TOTAL_HEIGHT) + "|" +
            String(ZEDMD_VERSION_MAJOR) + "." + String(ZEDMD_VERSION_MINOR) +
            "." + String(ZEDMD_VERSION_PATCH) + "|" +
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
            String(1)
#else
            String(0)
#endif
            + "|" + ((TRANSPORT_WIFI_UDP == transport) ? "UDP" : "TCP") + "|" +
            String(port) + "|" + String(udpDelay) + "|" +
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
    );
  });

  server->on("/ppuc.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/ppuc.png", "image/png");
  });

  server->on("/reset_wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    LittleFS.remove("/wifi_config.txt");  // Remove Wi-Fi config
    request->send(200, "text/plain", "Wi-Fi reset successful.");
    Restart();  // Restart the device
  });

  server->on("/apply", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Apply successful.");
    Restart();  // Restart the device
  });

  // Serve debug information
  server->on("/debug_info", HTTP_GET, [](AsyncWebServerRequest *request) {
    String debugInfo = "IP Address: " + WiFi.localIP().toString() + "\n";
    debugInfo += "SSID: " + WiFi.SSID() + "\n";
    debugInfo += "RSSI: " + String(WiFi.RSSI()) + "\n";
    debugInfo += "Heap Free: " + String(ESP.getFreeHeap()) + " bytes\n";
    debugInfo += "Uptime: " + String(millis() / 1000) + " seconds\n";
    // Add more here if you need it
    request->send(200, "text/plain", debugInfo);
  });

  // Route to return the current settings as JSON
  server->on("/get_config", HTTP_GET, [](AsyncWebServerRequest *request) {
    String trimmedSsid = ssid;
    trimmedSsid.trim();

    String json = "{";
    json += "\"ssid\":\"" + trimmedSsid + "\",";
    json += "\"port\":" + String(port) + ",";
#ifndef DISPLAY_RM67162_AMOLED
    json += "\"rgbOrder\":" + String(rgbMode) + ",";
#endif
    json += "\"brightness\":" + String(brightness) + ",";
    json += "\"scaleMode\":" + String(display->GetCurrentScalingMode());
    json += "}";
    request->send(200, "application/json", json);
  });

  server->on(
      "/get_scaling_modes", HTTP_GET, [](AsyncWebServerRequest *request) {
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
  server->on(
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

  // Start the web server
  server->begin();
  serverRunning = true;
}

void StartWiFi() {
  const char *apSSID = "ZeDMD-WiFi";
  const char *apPassword = "zedmd1234";
  bool softAPFallback = false;
  IPAddress ip;

  if (ssid_length > 0) {
    WiFi.disconnect(true);
    WiFi.begin(ssid.substring(0, ssid_length).c_str(),
               pwd.substring(0, pwd_length).c_str());

    uint8_t error = WiFi.waitForConnectResult();
    if (error != WL_CONNECTED) {
      display->DisplayText("No WiFi connection, error ", 10,
                           TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
      DisplayNumber(error, 3, 26, TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
      // second try
      error = WiFi.waitForConnectResult();
      if (error != WL_CONNECTED) {
        softAPFallback = true;
      }
    }
  } else {
    // Don't use the fallback to skip the countdown.
    WiFi.softAP(apSSID, apPassword);
    ip = WiFi.softAPIP();
  }

  if (!softAPFallback && WiFi.getMode() == WIFI_STA) {
    ip = WiFi.localIP();
  }

  if (ip[0] == 0 || softAPFallback) {
    display->DisplayText("No WiFi connection, maybe the   ", 10,
                         TOTAL_HEIGHT / 2 - 9, 255, 0, 0);
    display->DisplayText("the credentials are wrong.", 10, TOTAL_HEIGHT / 2 - 3,
                         255, 0, 0);
    display->DisplayText("Start AP in 30 seconds ...", 10, TOTAL_HEIGHT / 2 + 3,
                         255, 0, 0);
    for (uint8_t i = 29; i > 0; i--) {
      sleep(1);
      DisplayNumber(i, 2, 58, TOTAL_HEIGHT / 2 + 3, 255, 0, 0);
    }
    WiFi.softAP(apSSID, apPassword);
    ip = WiFi.softAPIP();
  }

  for (uint8_t i = 0; i < 4; i++) {
    if (i > 0) display->DrawPixel(i * 3 * 4 + i * 2 - 2, 4, 0);
    DisplayNumber(ip[i], 3, i * 3 * 4 + i * 2, 0, 0, 0, 0, 1);
  }

  WiFi.setSleep(false);  // WiFi speed improvement on ESP32 S3 and others.

  wifiActive = true;

  // Start the MDNS server for easy detection
  if (!MDNS.begin("zedmd-wifi")) {
    display->DisplayText("MDNS could not be started", 0, 0, 255, 0, 0);
    while (1);
  }

  display->DisplayText("zedmd-wifi.local", 0, TOTAL_HEIGHT - 5, 0, 0, 0, 1);

  StartServer();

  if (TRANSPORT_WIFI_UDP == transport) {
    udp = new AsyncUDP();
    udp->onPacket(HandleUdpPacket);
    if (!udp->listen(ip, port)) {
      display->DisplayText("UDP server could not be started", 0, 0, 255, 0, 0);
      while (1);
    }
  } else {
    tcp = new AsyncServer(port);
    tcp->setNoDelay(true);
    tcp->onClient(&NewTcpClient, tcp);
    tcp->begin();
  }
}

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);

  // (Re-)Initialize global state variables that might have survived a restart
  // and that don't get set by Load() functions below.
  currentRenderBuffer = 0;
  lastRenderBuffer = NUM_RENDER_BUFFERS - 1;
  payloadCompressed = false;
  payloadSize = 0;
  payloadMissing = 0;
  headerBytesReceived = 0;
  command = 0;
  currentBuffer = NUM_BUFFERS - 1;
  lastBuffer = currentBuffer;
  processingBuffer = NUM_BUFFERS - 1;
  wifiActive = false;
  logoActive = true;
  transportActive = false;
  transportWaitCounter = 0;
  logoWaitCounter = 0;
  lastDataReceived = 0;
  serverRunning = false;

  bool fileSystemOK;
  if (fileSystemOK = LittleFS.begin()) {
    LoadSettingsMenu();
#ifndef ZEDMD_WIFI
    LoadTransport();
#endif
    LoadWiFiConfig();
    LoadUsbPackageSizeMultiplier();
#ifdef DISPLAY_LED_MATRIX
    LoadRgbOrder();
    LoadPanelSettings();
#endif
    LoadLum();
    LoadDebug();
    LoadUdpDelay();
#ifdef ZEDMD_HD_HALF
    LoadYOffset();
#endif
  }

#ifdef DISPLAY_RM67162_AMOLED
  display = new Rm67162Amoled();  // For AMOLED display
#elif defined(DISPLAY_LED_MATRIX)
  display = new LedMatrix();  // For LED matrix display
#endif
  display->SetBrightness(brightness);

  if (!fileSystemOK) {
    display->DisplayText("Error reading file system!", 0, 0, 255, 0, 0);
    display->DisplayText("Try to flash the firmware again.", 0, 6, 255, 0, 0);
    while (true);
  }

  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_CPU_LOCKUP: {
      display->DisplayText("An unrecoverable error happend!", 0, 0, 255, 0, 0);
      display->DisplayText("Coredump has been writen. See", 0, 6, 255, 0, 0);
      display->DisplayText("ppuc.org/ZeDMD how to download", 0, 12, 255, 0, 0);
      display->DisplayText("it.", 0, 18, 255, 0, 0);
      display->DisplayText("Reboot in 30 seconds ...", 0, 24, 255, 0, 0);
      for (uint8_t i = 29; i > 0; i--) {
        sleep(1);
        DisplayNumber(i, 2, 40, 24, 255, 0, 0);
      }
      Restart();
      break;
    }

    case ESP_RST_PWR_GLITCH: {
      display->DisplayText("A power glitch caused a restart!", 0, 0, 255, 0, 0);
      display->DisplayText("Check your power supply and", 0, 6, 255, 0, 0);
      display->DisplayText("hardware.", 0, 12, 255, 0, 0);
      display->DisplayText("Reboot in 30 seconds ...", 0, 24, 255, 0, 0);
      for (uint8_t i = 29; i > 0; i--) {
        sleep(1);
        DisplayNumber(i, 2, 40, 24, 255, 0, 0);
      }
      Restart();
      break;
    }

    default:
      break;
  }

  for (uint8_t i = 0; i < NUM_RENDER_BUFFERS; i++) {
#ifdef BOARD_HAS_PSRAM
    renderBuffer[i] = (uint8_t *)heap_caps_malloc(
        TOTAL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
#else
    renderBuffer[i] = (uint8_t *)malloc(TOTAL_BYTES);
#endif
    if (nullptr == renderBuffer[i]) {
      display->DisplayText("out of memory", 0, 0, 255, 0, 0);
      while (1);
    }
    memset(renderBuffer[i], 0, TOTAL_BYTES);
  }

#ifndef DISPLAY_RM67162_AMOLED
  if (settingsMenu) {
    // Turn off settings menu after restart here.
    // Previously, the value has been set when selecting exit.
    // But this way, people who can't access the buttons in their cab
    // can leave the menu with a power cycle.
    settingsMenu = false;
    SaveSettingsMenu();

    RefreshSetupScreen();
    display->DisplayText("Exit", TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                         (TOTAL_HEIGHT / 2) + 4, 255, 191, 0);

    Bounce2::Button *forwardButton = new Bounce2::Button();
    forwardButton->attach(FORWARD_BUTTON_PIN, INPUT_PULLUP);
    forwardButton->interval(100);
    forwardButton->setPressedState(LOW);

    Bounce2::Button *upButton = new Bounce2::Button();
    upButton->attach(UP_BUTTON_PIN, INPUT_PULLUP);
    upButton->interval(100);
    upButton->setPressedState(LOW);

#ifdef ARDUINO_ESP32_S3_N16R8
    Bounce2::Button *backwardButton = new Bounce2::Button();
    backwardButton->attach(BACKWARD_BUTTON_PIN, INPUT_PULLUP);
    backwardButton->interval(100);
    backwardButton->setPressedState(LOW);

    Bounce2::Button *downButton = new Bounce2::Button();
    downButton->attach(DOWN_BUTTON_PIN, INPUT_PULLUP);
    downButton->interval(100);
    downButton->setPressedState(LOW);
#endif

    uint8_t position = 1;
    while (1) {
      forwardButton->update();
      bool forward = forwardButton->pressed();
      bool backward = false;
#ifdef ARDUINO_ESP32_S3_N16R8
      backwardButton->update();
      backward = backwardButton->pressed();
#endif
      if (forward || backward) {
#ifdef ZEDMD_HD_HALF
        if (forward && ++position > 8)
          position = 1;
        else if (backward && --position < 1)
          position = 8;
#else
        if (forward && ++position > 7)
          position = 1;
        else if (backward && --position < 1)
          position = 7;
#endif
        switch (position) {
          case 1: {  // Exit
            RefreshSetupScreen();
            display->DisplayText("Exit",
                                 TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                                 (TOTAL_HEIGHT / 2) + 4, 255, 191, 0);
            break;
          }
          case 2: {  // Brightness
            RefreshSetupScreen();
            DisplayLum(255, 191, 0);
            break;
          }
          case 3: {  // USB Package Size
            RefreshSetupScreen();
            display->DisplayText("USB Packet Size:", 7 * (TOTAL_WIDTH / 128),
                                 (TOTAL_HEIGHT / 2) + 4, 255, 191, 0);
            break;
          }
          case 4: {  // Transport
            RefreshSetupScreen();
            display->DisplayText(
                transport == TRANSPORT_USB
                    ? "USB     "
                    : (transport == TRANSPORT_WIFI_UDP
                           ? "WiFi UDP"
                           : (transport == TRANSPORT_WIFI_TCP ? "WiFi TCP"
                                                              : "SPI     ")),
                7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
            break;
          }
          case 5: {  // Debug
            RefreshSetupScreen();
            display->DisplayText("Debug:", 7 * (TOTAL_WIDTH / 128),
                                 (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
            break;
          }
          case 6: {  // RGB order
            RefreshSetupScreen();
            DisplayRGB(255, 191, 0);
            break;
          }
          case 7: {  // UDP Delay
            RefreshSetupScreen();
            display->DisplayText(
                "UDP Delay:",
                TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - (11 * 4),
                (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
            break;
          }
#ifdef ZEDMD_HD_HALF
          case 8: {  // Y Offset
            RefreshSetupScreen();
            display->DisplayText("Y-Offset",
                                 TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 32,
                                 (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
            break;
          }
#endif
        }
      }

      upButton->update();
      bool up = upButton->pressed();
      bool down = false;
#ifdef ARDUINO_ESP32_S3_N16R8
      downButton->update();
      down = downButton->pressed();
#endif
      if (up || down) {
        switch (position) {
          case 1: {  // Exit
            Restart();
            break;
          }
          case 2: {  // Brightness
            if (up && ++brightness > 15)
              brightness = 1;
            else if (down && --brightness < 1)
              brightness = 15;

            display->SetBrightness(brightness);
            DisplayLum(255, 191, 0);
            SaveLum();
            break;
          }
          case 3: {  // USB Package Size
            if (up && ++usbPackageSizeMultiplier > 60)
              usbPackageSizeMultiplier = 1;
            else if (down && --usbPackageSizeMultiplier < 1)
              usbPackageSizeMultiplier = 60;

            DisplayNumber(usbPackageSizeMultiplier * 32, 4,
                          7 * (TOTAL_WIDTH / 128) + (16 * 4),
                          (TOTAL_HEIGHT / 2) + 4, 255, 191, 0);
            SaveUsbPackageSizeMultiplier();
            break;
          }
          case 4: {  // Transport
            if (up && ++transport > TRANSPORT_SPI)
              transport = TRANSPORT_USB;
            else if (down && --transport < TRANSPORT_USB)
              transport = TRANSPORT_SPI;
            display->DisplayText(
                transport == TRANSPORT_USB
                    ? "USB     "
                    : (transport == TRANSPORT_WIFI_UDP
                           ? "WiFi UDP"
                           : (transport == TRANSPORT_WIFI_TCP ? "WiFi TCP"
                                                              : "SPI     ")),
                7 * (TOTAL_WIDTH / 128), (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
            SaveTransport();
            break;
          }
          case 5: {  // Debug
            if (++debug > 1) debug = 0;
            DisplayNumber(debug, 1, 7 * (TOTAL_WIDTH / 128) + (6 * 4),
                          (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
            SaveDebug();
            break;
          }
          case 6: {  // RGB order
            if (rgbModeLoaded != 0) {
              rgbMode = 0;
              SaveRgbOrder();
              delay(10);
              Restart();
            }
            if (up && ++rgbMode > 5)
              rgbMode = 0;
            else if (down && --rgbMode < 0)
              rgbMode = 5;
            RefreshSetupScreen();
            DisplayRGB(255, 191, 0);
            SaveRgbOrder();
            break;
          }
          case 7: {  // UDP Delay
            if (up && ++udpDelay > 9)
              udpDelay = 0;
            else if (down && udpDelay == 0)
              udpDelay = 9;
            else if (down)
              --udpDelay;

            DisplayNumber(udpDelay, 1,
                          TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 4,
                          (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
            SaveUdpDelay();
            break;
          }
#ifdef ZEDMD_HD_HALF
          case 8: {  // Y-Offset
            if (up && ++yOffset > 32)
              yOffset = 0;
            else if (down && --yOffset < 0)
              yOffset = 32;
            ClearScreen();
            RefreshSetupScreen();
            display->DisplayText("Y-Offset",
                                 TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 32,
                                 (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
            SaveYOffset();
            break;
          }
#endif
        }
      }

      delay(1);
    }
  }
#endif

  pinMode(FORWARD_BUTTON_PIN, INPUT_PULLUP);

  DisplayLogo();

  // Create synchronization primitives
  for (uint8_t i = 0; i < NUM_BUFFERS; i++) {
#ifdef BOARD_HAS_PSRAM
    buffers[i] = (uint8_t *)heap_caps_malloc(
        BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
#else
    buffers[i] = (uint8_t *)malloc(BUFFER_SIZE);
#endif
    if (nullptr == buffers[i]) {
      display->DisplayText("out of memory", 0, 0, 255, 0, 0);
      while (1);
    }
  }

  switch (transport) {
    case TRANSPORT_USB: {
#ifdef BOARD_HAS_PSRAM
      xTaskCreatePinnedToCore(Task_ReadSerial, "Task_ReadSerial", 8192, NULL, 1,
                              NULL, 0);
#else
      xTaskCreatePinnedToCore(Task_ReadSerial, "Task_ReadSerial", 4096, NULL, 1,
                              NULL, 0);
#endif
      break;
    }

    case TRANSPORT_WIFI_UDP:
    case TRANSPORT_WIFI_TCP: {
      StartWiFi();
      break;
    }

    case TRANSPORT_SPI: {
      display->DisplayText("SPI connection failure ...", 0, 0, 255, 0, 0);
      delay(5000);
      display->DisplayText("Is the SPI interface turned on?", 0, 6, 255, 0, 0);
      delay(5000);
      display->DisplayText("Your SPI cable might be too long", 0, 12, 255, 0,
                           0);
      delay(5000);
      display->DisplayText("No, your SPI cable is too short!", 0, 18, 255, 0,
                           0);
      delay(5000);
      display->DisplayText("SPI is not implemented yet!", 0, 24, 255, 191, 0);
      while (digitalRead(FORWARD_BUTTON_PIN));
      settingsMenu = true;
      SaveSettingsMenu();
      delay(20);
      Restart();
      break;
    }
  }
}

void loop() {
#ifndef DISPLAY_RM67162_AMOLED
  if (!digitalRead(FORWARD_BUTTON_PIN)) {
    settingsMenu = true;
    SaveSettingsMenu();
    delay(20);
    Restart();
  }
#endif

  if (!transportActive) {
    if (wifiActive && !serverRunning) {
      // @see https://github.com/ESP32Async/ESPAsyncWebServer/issues/21
      // StartServer();
    }

    if (!logoActive) {
      logoActive = true;
      logoWaitCounter = 199;
    }

    if (logoWaitCounter < 201) ++logoWaitCounter;

    if (100 == logoWaitCounter) {
      DisplayUpdate();
    }
    if (200 == logoWaitCounter) {
      ScreenSaver();
    }

    display->DrawPixel(TOTAL_WIDTH - 3, TOTAL_HEIGHT - 3, throbberColors[0],
                       throbberColors[1], throbberColors[2]);

    switch (transportWaitCounter) {
      case 0:
        display->DrawPixel(TOTAL_WIDTH - 4, TOTAL_HEIGHT - 4, throbberColors[3],
                           throbberColors[4], throbberColors[5]);
        display->DrawPixel(TOTAL_WIDTH - 3, TOTAL_HEIGHT - 4, throbberColors[0],
                           throbberColors[1], throbberColors[2]);
        break;
      case 1:
        display->DrawPixel(TOTAL_WIDTH - 3, TOTAL_HEIGHT - 4, throbberColors[3],
                           throbberColors[4], throbberColors[5]);
        display->DrawPixel(TOTAL_WIDTH - 2, TOTAL_HEIGHT - 4, throbberColors[0],
                           throbberColors[1], throbberColors[2]);
        break;
      case 2:
        display->DrawPixel(TOTAL_WIDTH - 2, TOTAL_HEIGHT - 4, throbberColors[3],
                           throbberColors[4], throbberColors[5]);
        display->DrawPixel(TOTAL_WIDTH - 2, TOTAL_HEIGHT - 3, throbberColors[0],
                           throbberColors[1], throbberColors[2]);
        break;
      case 3:
        display->DrawPixel(TOTAL_WIDTH - 2, TOTAL_HEIGHT - 3, throbberColors[3],
                           throbberColors[4], throbberColors[5]);
        display->DrawPixel(TOTAL_WIDTH - 2, TOTAL_HEIGHT - 2, throbberColors[0],
                           throbberColors[1], throbberColors[2]);
        break;
      case 4:
        display->DrawPixel(TOTAL_WIDTH - 2, TOTAL_HEIGHT - 2, throbberColors[3],
                           throbberColors[4], throbberColors[5]);
        display->DrawPixel(TOTAL_WIDTH - 3, TOTAL_HEIGHT - 2, throbberColors[0],
                           throbberColors[1], throbberColors[2]);
        break;
      case 5:
        display->DrawPixel(TOTAL_WIDTH - 3, TOTAL_HEIGHT - 2, throbberColors[3],
                           throbberColors[4], throbberColors[5]);
        display->DrawPixel(TOTAL_WIDTH - 4, TOTAL_HEIGHT - 2, throbberColors[0],
                           throbberColors[1], throbberColors[2]);
        break;
      case 6:
        display->DrawPixel(TOTAL_WIDTH - 4, TOTAL_HEIGHT - 2, throbberColors[3],
                           throbberColors[4], throbberColors[5]);
        display->DrawPixel(TOTAL_WIDTH - 4, TOTAL_HEIGHT - 3, throbberColors[0],
                           throbberColors[1], throbberColors[2]);
        break;
      case 7:
        display->DrawPixel(TOTAL_WIDTH - 4, TOTAL_HEIGHT - 3, throbberColors[3],
                           throbberColors[4], throbberColors[5]);
        display->DrawPixel(TOTAL_WIDTH - 4, TOTAL_HEIGHT - 4, throbberColors[0],
                           throbberColors[1], throbberColors[2]);
        break;
    }

    transportWaitCounter = (transportWaitCounter + 1) % 8;

    vTaskDelay(pdMS_TO_TICKS(200));
  } else {
    // if (wifiActive && serverRunning) {
    //  @see https://github.com/ESP32Async/ESPAsyncWebServer/issues/21
    //  server->end();
    //  delete server;
    //  server = nullptr;
    //  serverRunning = false;
    //}

    if (lastDataReceived > 0 &&
        (millis() - lastDataReceived) > CONNECTION_TIMEOUT) {
      transportActive = false;
      return;
    }

    if (logoActive) {
      ClearScreen();
      logoActive = false;
    }

    if (AcquireNextProcessingBuffer()) {
      if (2 == bufferSizes[processingBuffer] &&
          255 == buffers[processingBuffer][0] &&
          255 == buffers[processingBuffer][1]) {
#if defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)
        Render();
#endif
      } else if (2 == bufferSizes[processingBuffer] &&
                 0 == buffers[processingBuffer][0] &&
                 0 == buffers[processingBuffer][1]) {
        ClearScreen();
      } else {
        mz_ulong uncompressedBufferSize = (mz_ulong)TOTAL_BYTES;
        if (bufferCompressed[processingBuffer]) {
          mz_ulong compressedBufferSize =
              (mz_ulong)bufferSizes[processingBuffer];
          memset(uncompressBuffer, 0, 2048);

          portENTER_CRITICAL(&bufferMutex);
          int minizStatus =
              mz_uncompress2(uncompressBuffer, &uncompressedBufferSize,
                             buffers[processingBuffer], &compressedBufferSize);
          portEXIT_CRITICAL(&bufferMutex);

          if (MZ_OK != minizStatus) {
            if (1 == debug) {
              display->DisplayText("miniz error: ", 0, 0, 255, 0, 0);
              DisplayNumber(minizStatus, 3, 13 * 4, 0, 255, 0, 0);
              display->DisplayText("free heap: ", 0, 6, 255, 0, 0);
              DisplayNumber(esp_get_free_heap_size(), 8, 11 * 4, 6, 255, 0, 0);
              while (1);
            }
            return;
          }
        } else {
          uncompressedBufferSize = bufferSizes[processingBuffer];
          memcpy(uncompressBuffer, buffers[processingBuffer],
                 uncompressedBufferSize);
        }

        uint16_t uncompressedBufferPosition = 0;
        while (uncompressedBufferPosition < uncompressedBufferSize) {
          if (uncompressBuffer[uncompressedBufferPosition] >= 128) {
#if defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)
            const uint8_t idx =
                uncompressBuffer[uncompressedBufferPosition++] - 128;
            const uint8_t yOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
            const uint8_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;
            for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
              memset(&renderBuffer[currentRenderBuffer]
                                  [((yOffset + y) * TOTAL_WIDTH + xOffset) * 3],
                     0, ZONE_WIDTH * 3);
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

            for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
              for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
                const uint16_t rgb565 =
                    uncompressBuffer[uncompressedBufferPosition++] +
                    (((uint16_t)uncompressBuffer[uncompressedBufferPosition++])
                     << 8);
                uint8_t rgb888[3];
                rgb888[0] = (rgb565 >> 8) & 0xf8;
                rgb888[1] = (rgb565 >> 3) & 0xfc;
                rgb888[2] = (rgb565 << 3);
                rgb888[0] |= (rgb888[0] >> 5);
                rgb888[1] |= (rgb888[1] >> 6);
                rgb888[2] |= (rgb888[2] >> 5);
                memcpy(
                    &renderBuffer[currentRenderBuffer]
                                 [((yOffset + y) * TOTAL_WIDTH + xOffset + x) *
                                  3],
                    rgb888, 3);
              }
            }
#else
            display->FillZoneRaw565(
                uncompressBuffer[uncompressedBufferPosition++],
                &uncompressBuffer[uncompressedBufferPosition]);
            uncompressedBufferPosition += RGB565_ZONE_SIZE;
#endif
          }
        }
      }
    } else {
      // Avoid busy-waiting
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}
