#ifdef PICO_BUILD
// Set the 266 MHz system clock
// For RP2350/RP2040 custom clocks, PLL parameters must be provided
#define PLL_SYS_VCO_FREQ_HZ 1596000000ul
#define PLL_SYS_POSTDIV1 6
#define PLL_SYS_POSTDIV2 1
// officially, 200MHz clock is supported, but 266MHz should fine as well
// @see SYS_CLK_MHZ https://github.com/raspberrypi/pico-sdk/releases/tag/2.1.1
#define SYS_CLK_MHZ 266
#endif

#include <Arduino.h>
#include <Bounce2.h>

#ifdef PICO_RP2350
#include "hardware/clocks.h"
#endif

#ifdef PICO_BUILD
#include "hardware/vreg.h"
#include "pico/zedmd_pico.h"
#endif
#include <LittleFS.h>

#ifndef DMDREADER
#include "transports/usb_transport.h"
#else
#include <dmdreader.h>
#endif
#include "transports/spi_transport.h"
#ifndef ZEDMD_NO_NETWORKING
#include "transports/wifi_transport.h"
#endif

#ifdef SPEAKER_LIGHTS
#include <WS2812FX.h>
#endif

// Specific improvements and #define for the ESP32 S3 series
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#include "S3Specific.h"
#endif
#ifndef PICO_BUILD
#include "esp_log.h"
#include "esp_task_wdt.h"
#endif
#ifndef DMDREADER
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif
#include "main.h"
#include "utility/clock.h"

// To save RAM only include the driver we want to use.
#ifdef DISPLAY_RM67162_AMOLED
#include "displays/Rm67162Amoled.h"
#elif defined(PICO_BUILD)
#include "displays/PicoLedMatrix.h"
#else
#include "displays/Esp32LedMatrix.h"
#endif

#define LED_CHECK_DELAY 1000  // ms per color

#define RC 0
#define GC 1
#define BC 2

#ifdef SPEAKER_LIGHTS
#ifdef ARDUINO_ESP32_S3_N16R8
#define SPEAKER_LIGHTS_LEFT_PIN 9    // Left speaker LED strip
#define SPEAKER_LIGHTS_RIGHT_PIN 10  // Right speaker LED strip
#elif defined(DMDREADER)
#define SPEAKER_LIGHTS_LEFT_PIN 55   // Left speaker LED strip
#define SPEAKER_LIGHTS_RIGHT_PIN 56  // Right speaker LED strip
#endif

#define FX_MODE_AMBILIGHT 200

uint8_t speakerLightsLeftNumLeds;
uint8_t speakerLightsRightNumLeds;
uint8_t speakerLightsLeftLedType;
uint8_t speakerLightsRightLedType;
uint8_t speakerLightsLeftMode;
uint8_t speakerLightsRightMode;
uint32_t speakerLightsLeftColor;
uint32_t speakerLightsRightColor;
WS2812FX *speakerLightsLeft;
WS2812FX *speakerLightsRight;

uint8_t speakerLightsBlackThreshold;  // Ignore pixels below this brightness
uint8_t speakerLightsGammaFactor;     // Scaling factor (0-256), higher = less
                                      // black impact
#endif

const uint8_t FrameChars[5]
    __attribute__((aligned(4))) = {'F', 'R', 'A', 'M', 'E'};
const uint8_t CtrlChars[6]
    __attribute__((aligned(4))) = {'Z', 'e', 'D', 'M', 'D', 'A'};
uint8_t numCtrlCharsFound = 0;

DisplayDriver *display;

// Buffers for storing data
uint8_t *buffers[NUM_BUFFERS];
mz_ulong bufferSizes[NUM_BUFFERS] __attribute__((aligned(4))) = {0};
bool bufferCompressed[NUM_BUFFERS] __attribute__((aligned(4))) = {0};

#ifndef DMDREADER
// The uncompress buffer should be big enough
uint8_t uncompressBuffer[2048] __attribute__((aligned(4)));
#endif
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
bool rgb565ZoneStream = false;

// Init display on a low brightness to avoid power issues, but bright enough to
// see something.
#ifdef DISPLAY_RM67162_AMOLED
uint8_t brightness = 5;
#else
uint8_t brightness = 2;
uint8_t rgbMode = 0;  // Valid values are 0-5.
uint8_t rgbModeLoaded = 0;
int8_t yOffset = 0;
uint8_t panelClkphase = 0;
uint8_t panelDriver = 0;
uint8_t panelI2sspeed = 8;
uint8_t panelLatchBlanking = 2;
uint8_t panelMinRefreshRate = 60;
#ifdef DMDREADER
bool core_0_initialized = false;
bool core_1_initialized = false;
uint8_t loopbackColor = (uint8_t)Color::DMD_ORANGE;

const char *ColorString(uint8_t color) {
  switch ((Color)color) {
    case Color::DMD_ORANGE:
      return "orange";
    case Color::DMD_RED:
      return "red   ";
    case Color::DMD_YELLOW:
      return "yellow";
    case Color::DMD_GREEN:
      return "green ";
    case Color::DMD_BLUE:
      return "blue  ";
    case Color::DMD_PURPLE:
      return "purple";
    case Color::DMD_PINK:
      return "pink  ";
    case Color::DMD_WHITE:
      return "white ";
    default:
      return nullptr;
  }
}

// Simple nearest-neighbor 2x upscaler for RGB888 loopback frames.
static void Scale2xLoopback(const uint8_t *src, uint8_t *dst, uint16_t srcWidth,
                            uint16_t srcHeight) {
  const uint16_t dstWidth = srcWidth * 2;
  const uint32_t srcStride = srcWidth * 3;
  const uint32_t dstStride = dstWidth * 3;

  for (uint16_t y = 0; y < srcHeight; ++y) {
    const uint8_t *srcRow = src + y * srcStride;
    uint8_t *dstRow0 = dst + (y * 2) * dstStride;
    uint8_t *dstRow1 = dstRow0 + dstStride;

    for (uint16_t x = 0; x < srcWidth; ++x) {
      const uint8_t *pixel = srcRow + x * 3;
      const uint8_t r = pixel[0];
      const uint8_t g = pixel[1];
      const uint8_t b = pixel[2];

      uint8_t *out0 = dstRow0 + (x * 2) * 3;
      uint8_t *out1 = dstRow1 + (x * 2) * 3;

      out0[0] = out0[3] = out1[0] = out1[3] = r;
      out0[1] = out0[4] = out1[1] = out1[4] = g;
      out0[2] = out0[5] = out1[2] = out1[5] = b;
    }
  }
}
#endif

// We needed to change these from RGB to RC (Red Color), BC, GC to prevent
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

static uint8_t Expand5To8[32];
static uint8_t Expand6To8[64];

static inline void InitRgbLuts() {
  for (uint8_t i = 0; i < 32; ++i) {
    Expand5To8[i] = static_cast<uint8_t>((i << 3) | (i >> 2));
  }
  for (uint8_t i = 0; i < 64; ++i) {
    Expand6To8[i] = static_cast<uint8_t>((i << 2) | (i >> 4));
  }
}

uint8_t usbPackageSizeMultiplier = USB_PACKAGE_SIZE / 32;
uint8_t settingsMenu = 0;
uint8_t debug = 0;

Transport *transport = nullptr;

bool logoActive, updateActive, saverActive;
bool transportActive;
uint8_t transportWaitCounter;
Clock logoWaitCounterClock;
Clock lastDataReceivedClock;
uint8_t throbberColors[6] __attribute__((aligned(4))) = {0};
mz_ulong uncompressedBufferSize = 2048;
uint16_t shortId;
uint8_t wifiPower;
#ifdef PICO_BUILD
bool rebootToBootloader = false;
#endif

void DoRestart(int sec) {
  if (transport->isActive()) {
    transport->deinit();
  }
  display->ClearScreen();
  display->DisplayText("Restarting ...", 0, 0, 255, 0, 0);
  display->Render();
#ifndef DMDREADER
  vTaskDelay(pdMS_TO_TICKS(sec * 1000));
#else
  delay(1000);
#endif
  display->ClearScreen();
  display->Render();
  delay(20);

#ifdef PICO_BUILD
  if (rebootToBootloader) rp2040.rebootToBootloader();
#endif

  // Note: ESP.restart() or esp_restart() will keep the state of global and
  // static variables. And not all sub-systems get resetted.
#if (defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1)
#ifdef PICO_BUILD
  rp2040.reboot();
#else
  esp_sleep_enable_timer_wakeup(1000);  // Wake up after 1ms
  esp_deep_sleep_start();  // Enter deep sleep (ESP32 reboots on wake)
#endif
#else
#ifdef PICO_BUILD
  rp2040.reboot();
#else
  esp_restart();
#endif
#endif
}

void Restart() { DoRestart(1); }

void RestartAfterError() { DoRestart(30); }

void DisplayNumber(uint32_t chf, uint8_t nc, uint16_t x, uint16_t y, uint8_t r,
                   uint8_t g, uint8_t b, bool transparent) {
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
DisplayDriver *GetDisplayDriver() { return display; }

void TransportCreate(const uint8_t type =
#ifdef DMDREADER
                         Transport::SPI
#elif defined(ZEDMD_WIFI_ONLY)
                         Transport::WIFI_UDP
#else
                         Transport::USB
#endif
) {

  // "reload" new transport (without init)
  delete transport;

  switch (type) {
#ifdef DMDREADER
    case Transport::SPI: {
      transport = new SpiTransport();
      break;
    }
#else
    case Transport::USB: {
      transport = new UsbTransport();
      break;
    }
#ifndef ZEDMD_NO_NETWORKING
    case Transport::WIFI_UDP:
    case Transport::WIFI_TCP: {
      transport = new WifiTransport();
      break;
    }
#endif
    case Transport::SPI:
    default: {
      transport = new Transport();
    }
#endif
  }

  transport->loadConfig();
  transport->loadDelay();
}

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

void SaveTransport(const uint8_t type = Transport::USB) {
  if (!transport) return;

  File f = LittleFS.open("/transport.val", "w");
  f.write(type);
  f.close();

  // set new transport type (not active until reboot)
  transport->setType(type);
}

void LoadTransport() {
  File f = LittleFS.open("/transport.val", "r");
  if (!f) {
    const uint8_t type = transport != nullptr ? transport->getType() :
#ifdef DMDREADER
                                              Transport::SPI
#elif defined(ZEDMD_WIFI_ONLY)
                                              Transport::WIFI_UDP
#else
                                              Transport::USB
#endif
        ;
    SaveTransport(type);
    TransportCreate(type);
    return;
  }

  const uint8_t type = f.read();
  f.close();
  TransportCreate(type);
}

#if defined(DISPLAY_LED_MATRIX)
void SaveRgbOrder() {
  File f = LittleFS.open("/rgb_order.val", "w");
  f.write((uint8_t)rgbMode);
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

#ifdef DMDREADER
void SaveColor() {
  File f = LittleFS.open("/color.val", "w");
  f.write(loopbackColor);
  f.close();
}

void LoadColor() {
  File f = LittleFS.open("/color.val", "r");
  if (!f) {
    SaveColor();
    return;
  }
  loopbackColor = f.read();
  if (ColorString(loopbackColor) == nullptr) {
    loopbackColor = (uint8_t)Color::DMD_ORANGE;
    SaveColor();
  }
  f.close();
}
#endif

#ifdef SPEAKER_LIGHTS
void SaveSpeakerLightsSettings() {
  File f = LittleFS.open("/speaker_lights_left_num.val", "w");
  f.write(speakerLightsLeftNumLeds);
  f.close();
  f = LittleFS.open("/speaker_lights_right_num.val", "w");
  f.write(speakerLightsRightNumLeds);
  f.close();
  f = LittleFS.open("/speaker_lights_left_type.val", "w");
  f.write(speakerLightsLeftLedType);
  f.close();
  f = LittleFS.open("/speaker_lights_right_type.val", "w");
  f.write(speakerLightsRightLedType);
  f.close();
  f = LittleFS.open("/speaker_lights_left_mode.val", "w");
  f.write(speakerLightsLeftMode);
  f.close();
  f = LittleFS.open("/speaker_lights_right_mode.val", "w");
  f.write(speakerLightsRightMode);
  f.close();
  f = LittleFS.open("/speaker_lights_left_color.val", "w");
  f.write(speakerLightsLeftColor & 0xFF);
  f.write((speakerLightsLeftColor >> 8) & 0xFF);
  f.write((speakerLightsLeftColor >> 16) & 0xFF);
  f.close();
  f = LittleFS.open("/speaker_lights_right_color.val", "w");
  f.write(speakerLightsRightColor & 0xFF);
  f.write((speakerLightsRightColor >> 8) & 0xFF);
  f.write((speakerLightsRightColor >> 16) & 0xFF);
  f.close();
  f = LittleFS.open("/speaker_lights_black_threshold.val", "w");
  f.write(speakerLightsBlackThreshold);
  f.close();
  f = LittleFS.open("/speaker_lights_gamma_factor.val", "w");
  f.write(speakerLightsGammaFactor);
  f.close();
}

void LoadSpeakerLightsSettings() {
  File f = LittleFS.open("/speaker_lights_left_num.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsLeftNumLeds = f.read();
  f.close();
  f = LittleFS.open("/speaker_lights_right_num.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsRightNumLeds = f.read();
  f.close();
  f = LittleFS.open("/speaker_lights_left_type.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsLeftLedType = f.read();
  f.close();
  f = LittleFS.open("/speaker_lights_right_type.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsRightLedType = f.read();
  f.close();
  f = LittleFS.open("/speaker_lights_left_mode.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsLeftMode = f.read();
  f.close();
  f = LittleFS.open("/speaker_lights_right_mode.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsRightMode = f.read();
  f.close();
  f = LittleFS.open("/speaker_lights_left_color.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsLeftColor = f.read() + (f.read() << 8) + (f.read() << 16);
  f.close();
  f = LittleFS.open("/speaker_lights_right_color.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsRightColor = f.read() + (f.read() << 8) + (f.read() << 16);
  f.close();
  f = LittleFS.open("/speaker_lights_black_threshold.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsBlackThreshold = f.read();
  f.close();
  f = LittleFS.open("/speaker_lights_gamma_factor.val", "r");
  if (!f) {
    SaveSpeakerLightsSettings();
  }
  speakerLightsGammaFactor = f.read();
  f.close();
  // @todo LED Type and RGB order
}
#endif

void LedTester(void) {
  display->FillScreen(255, 0, 0);
  display->Render();
  delay(LED_CHECK_DELAY);

  display->FillScreen(0, 255, 0);
  display->Render();
  delay(LED_CHECK_DELAY);

  display->FillScreen(0, 0, 255);
  display->Render();
  delay(LED_CHECK_DELAY);

  display->ClearScreen();
  display->Render();
}

void AcquireNextBuffer() {
  // currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
  // return;
  while (1) {
    if (currentBuffer == lastBuffer &&
        ((currentBuffer + 1) % NUM_BUFFERS) != processingBuffer) {
      currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
      return;
    }
    // Avoid busy-waiting
#ifndef DMDREADER
    vTaskDelay(pdMS_TO_TICKS(1));
#else
    tight_loop_contents();
#endif
  }
}

void CheckMenuButton() {
#ifndef DISPLAY_RM67162_AMOLED
  if (!digitalRead(FORWARD_BUTTON_PIN)) {
    settingsMenu = true;
    SaveSettingsMenu();
    delay(20);
    Restart();
  }
#endif
}

void MarkCurrentBufferDone() { lastBuffer = currentBuffer; }

bool AcquireNextProcessingBuffer() {
  if (processingBuffer != currentBuffer &&
      (((processingBuffer + 1) % NUM_BUFFERS) != currentBuffer ||
       currentBuffer == lastBuffer)) {
    processingBuffer = (processingBuffer + 1) % NUM_BUFFERS;
    return true;
  }
  return false;
}

// #define ZEDMD_CLIENT_DEBUG_FPS
#ifdef ZEDMD_CLIENT_DEBUG_FPS
Clock fpsClock;
uint16_t frames = 0;
char fpsStr[3];

void FpsUpdate() {
  frames++;

  if (fpsClock.getElapsedTime().asMilliseconds() >= 200) {
    snprintf(fpsStr, 3, "%02i", frames * 5);
    frames = 0;
    fpsClock.restart();
  }

  display->DisplayText(fpsStr, TOTAL_WIDTH - 7, TOTAL_HEIGHT - 5, 255, 0, 0,
                       false, false);
  display->Render();
}
#endif

uint8_t GetPixelBrightness(uint8_t r, uint8_t g, uint8_t b) {
  return (r * 77 + g * 150 + b * 29) >> 8;  // Optimized luminance calculation
}

void Render(bool renderAll = true) {
#ifdef DISPLAY_RM67162_AMOLED
  display->FillPanelRaw(renderBuffer[currentRenderBuffer]);
#else
  if (NUM_RENDER_BUFFERS == 1 || currentRenderBuffer != lastRenderBuffer) {
#ifdef SPEAKER_LIGHTS
    uint32_t sumRLeft = 0, sumGLeft = 0, sumBLeft = 0, sumRRight = 0,
             sumGRight = 0, sumBRight = 0;
    uint16_t colorCountLeft = 0, blackCountLeft = 0, colorCountRight = 0,
             blackCountRight = 0;
#endif
    uint16_t pos;

    for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
      for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
        pos = (y * TOTAL_WIDTH + x) * 3;
        // When only one render buffer is in use there is no previous frame to
        // diff against, so always draw. Otherwise draw only on loopback or
        // when the pixel actually changed.
        if (NUM_RENDER_BUFFERS == 1 || transport->isLoopback() ||
            memcmp(&renderBuffer[currentRenderBuffer][pos],
                   &renderBuffer[lastRenderBuffer][pos], 3) != 0) {
          display->DrawPixel(x, y, renderBuffer[currentRenderBuffer][pos],
                             renderBuffer[currentRenderBuffer][pos + 1],
                             renderBuffer[currentRenderBuffer][pos + 2]);
        }
#ifdef SPEAKER_LIGHTS
        if (FX_MODE_AMBILIGHT == speakerLightsLeftMode ||
            FX_MODE_AMBILIGHT == speakerLightsRightMode) {
          // Stern SAM ROMs have a big black zone on the left side of the screen
          if (speakerLightsLeftNumLeds > 0 && x > TOTAL_WIDTH / 4 &&
              x <= TOTAL_WIDTH / 2) {
            if (GetPixelBrightness(renderBuffer[currentRenderBuffer][pos],
                                   renderBuffer[currentRenderBuffer][pos + 1],
                                   renderBuffer[currentRenderBuffer][pos + 2]) >
                speakerLightsBlackThreshold) {
              sumRLeft += renderBuffer[currentRenderBuffer][pos];
              sumGLeft += renderBuffer[currentRenderBuffer][pos + 1];
              sumBLeft += renderBuffer[currentRenderBuffer][pos + 2];
              colorCountLeft++;
            } else {
              blackCountLeft++;
            }
          } else if (speakerLightsRightNumLeds > 0 &&
                     x >= (TOTAL_WIDTH - (TOTAL_WIDTH / 4))) {
            if (GetPixelBrightness(renderBuffer[currentRenderBuffer][pos],
                                   renderBuffer[currentRenderBuffer][pos + 1],
                                   renderBuffer[currentRenderBuffer][pos + 2]) >
                speakerLightsBlackThreshold) {
              sumRRight += renderBuffer[currentRenderBuffer][pos];
              sumGRight += renderBuffer[currentRenderBuffer][pos + 1];
              sumBRight += renderBuffer[currentRenderBuffer][pos + 2];
              colorCountRight++;
            } else {
              blackCountRight++;
            }
          }
        }
#endif
      }
    }

    if (renderAll) display->Render();

#ifdef SPEAKER_LIGHTS
    if (FX_MODE_AMBILIGHT == speakerLightsLeftMode) {
      if (colorCountLeft == 0) {
        speakerLightsLeft->setColor(0);  // All black → Turn off LEDs
      } else {
        // Integer-based black influence calculation
        uint16_t totalPixelsLeft = blackCountLeft + colorCountLeft;
        uint16_t blackRatio256 =
            (blackCountLeft * 256) / colorCountLeft;  // Scale to 0-256
        // brightnessFactor = 256 if all color, lower if black is present
        uint16_t brightnessFactor =
            (256 - ((blackRatio256 * (256 - speakerLightsGammaFactor)) >> 8));
        speakerLightsLeft->setColor(
            (sumRLeft / colorCountLeft * brightnessFactor) >> 8,
            (sumGLeft / colorCountLeft * brightnessFactor) >> 8,
            (sumBLeft / colorCountLeft * brightnessFactor) >> 8);
      }
    }
    if (FX_MODE_AMBILIGHT == speakerLightsRightMode) {
      if (colorCountRight == 0) {
        speakerLightsRight->setColor(0);  // All black → Turn off LEDs
      } else {
        // Integer-based black influence calculation
        uint16_t totalPixelsRight = blackCountRight + colorCountRight;
        uint16_t blackRatio256 =
            (blackCountRight * 256) / colorCountRight;  // Scale to 0-256
        // brightnessFactor = 256 if all color, lower if black is present
        uint16_t brightnessFactor =
            (256 - ((blackRatio256 * (256 - speakerLightsGammaFactor)) >> 8));
        speakerLightsRight->setColor(
            (sumRRight / colorCountRight * brightnessFactor) >> 8,
            (sumGRight / colorCountRight * brightnessFactor) >> 8,
            (sumBRight / colorCountRight * brightnessFactor) >> 8);
      }
    }
#endif
    if (NUM_RENDER_BUFFERS > 1) {
      lastRenderBuffer = currentRenderBuffer;
      currentRenderBuffer = (currentRenderBuffer + 1) % NUM_RENDER_BUFFERS;
      memcpy(renderBuffer[currentRenderBuffer], renderBuffer[lastRenderBuffer],
             TOTAL_BYTES);
    }
  }
#endif

#ifdef ZEDMD_CLIENT_DEBUG_FPS
  FpsUpdate();
#endif
}

void ClearScreen() {
  display->ClearScreen();
  display->Render();
  memset(renderBuffer[currentRenderBuffer], 0, TOTAL_BYTES);

  if (NUM_RENDER_BUFFERS > 1) {
    lastRenderBuffer = currentRenderBuffer;
    currentRenderBuffer = (currentRenderBuffer + 1) % NUM_RENDER_BUFFERS;
  }

#ifdef SPEAKER_LIGHTS
  if (FX_MODE_AMBILIGHT == speakerLightsLeftMode) {
    speakerLightsLeft->setColor(0);  // All black → Turn off LEDs
  }
  if (FX_MODE_AMBILIGHT == speakerLightsRightMode) {
    speakerLightsRight->setColor(0);  // All black → Turn off LEDs
  }
#endif
}

void DisplayLogo() {
  File f;

  if (TOTAL_WIDTH == 256 && TOTAL_HEIGHT == 64) {
    f = LittleFS.open("/logoHD.raw", "r");
  } else if (TOTAL_WIDTH == 192 && TOTAL_HEIGHT == 64) {
    f = LittleFS.open("/logoSEGAHD.raw", "r");
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

  Render(false);
  DisplayVersion(true);

  throbberColors[0] = 0;
  throbberColors[1] = 0;
  throbberColors[2] = 0;
  throbberColors[3] = 255;
  throbberColors[4] = 255;
  throbberColors[5] = 255;

  logoActive = true;
  logoWaitCounterClock.restart();
}

void DisplayId() {
  char id[5];
  sprintf(id, "%04X", shortId);
  display->DisplayText(id, TOTAL_WIDTH - 16, 0, 0, 0, 0, 1);
}

void DisplayUpdate() {
  File f;

  if (TOTAL_WIDTH == 256 && TOTAL_HEIGHT == 64) {
    f = LittleFS.open("/ppucHD.raw", "r");
  } else if (TOTAL_WIDTH == 192 && TOTAL_HEIGHT == 64) {
    // need to add some day
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

  DisplayId();

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
  display->DisplayText(transport->getTypeString(), 7 * (TOTAL_WIDTH / 128),
                       (TOTAL_HEIGHT / 2) - 3, 128, 128, 128);
  display->DisplayText("Debug:", 7 * (TOTAL_WIDTH / 128),
                       (TOTAL_HEIGHT / 2) - 10, 128, 128, 128);
  DisplayNumber(debug, 1, 7 * (TOTAL_WIDTH / 128) + (6 * 4),
                (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
  if (transport->isUsb()) {
    display->DisplayText("USB Packet Size:", 7 * (TOTAL_WIDTH / 128),
                         (TOTAL_HEIGHT / 2) + 4, 128, 128, 128);
    DisplayNumber(usbPackageSizeMultiplier * 32, 4,
                  7 * (TOTAL_WIDTH / 128) + (16 * 4), (TOTAL_HEIGHT / 2) + 4,
                  255, 191, 0);
  } else if (transport->isWifi()) {
    display->DisplayText("UDP Delay:", 7 * (TOTAL_WIDTH / 128),
                         (TOTAL_HEIGHT / 2) + 4, 128, 128, 128);
    DisplayNumber(transport->getDelay(), 1, 7 * (TOTAL_WIDTH / 128) + 10 * 4,
                  (TOTAL_HEIGHT / 2) + 4, 255, 191, 0);
  }
#ifdef DMDREADER
  else if (transport->isSpi()) {
    display->DisplayText("Color:", 7 * (TOTAL_WIDTH / 128),
                         (TOTAL_HEIGHT / 2) + 4, 128, 128, 128);
    display->DisplayText(ColorString(loopbackColor),
                         7 * (TOTAL_WIDTH / 128) + (6 * 4),
                         (TOTAL_HEIGHT / 2) + 4, 255, 191, 0);
  }
#endif
#ifdef ZEDMD_HD_HALF
  display->DisplayText("Y-Offset", TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 32,
                       (TOTAL_HEIGHT / 2) - 10, 128, 128, 128);
#endif
  display->DisplayText("Exit", TOTAL_WIDTH - (7 * (TOTAL_WIDTH / 128)) - 16,
                       (TOTAL_HEIGHT / 2) + 4, 128, 128, 128);
}

uint8_t HandleData(uint8_t *pData, size_t len) {
  uint16_t pos = 0;
  bool headerCompleted = false;

  while (pos < len ||
         (headerCompleted && command != 4 && command != 5 && command != 22 &&
          command != 23 && command != 27 && command != 28 && command != 29 &&
          command != 40 && command != 41 && command != 42 && command != 43 &&
          command != 44 && command != 45 && command != 46 && command != 47 &&
          command != 48)) {
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
#ifdef PICO_BUILD
        rp2040.wdt_reset();
#else
        esp_task_wdt_reset();
#endif
        if (payloadSize > BUFFER_SIZE) {
          if (debug) {
            display->DisplayText("Error, payloadSize > BUFFER_SIZE", 0, 0, 255,
                                 0, 0);
            DisplayNumber(payloadSize, 5, 0, 19, 255, 0, 0);
            DisplayNumber(BUFFER_SIZE, 5, 0, 25, 255, 0, 0);
            display->Render();
            while (1);
          }
          headerBytesReceived = 0;
          numCtrlCharsFound = 0;
          return 2;
        }

        if (debug) {
          display->DisplayText("Command:", 7 * (TOTAL_WIDTH / 128),
                               (TOTAL_HEIGHT / 2) - 10, 128, 128, 128);
          DisplayNumber(command, 2, 7 * (TOTAL_WIDTH / 128) + (8 * 4),
                        (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
          display->DisplayText("Payload:", 7 * (TOTAL_WIDTH / 128),
                               (TOTAL_HEIGHT / 2) - 4, 128, 128, 128);
          DisplayNumber(payloadSize, 2, 7 * (TOTAL_WIDTH / 128) + (8 * 4),
                        (TOTAL_HEIGHT / 2) - 4, 255, 191, 0);
          display->Render();
        }

        bool rgb888ZoneStream = false;
        switch (command) {
          case 12:  // handshake
          {
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;

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
            response[N_INTERMEDIATE_CTR_CHARS + 17] = transport->getDelay();
#ifdef ZEDMD_HD_HALF
            response[N_INTERMEDIATE_CTR_CHARS + 18] = 1;
#else
            response[N_INTERMEDIATE_CTR_CHARS + 18] = 0;
#endif
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED) || \
    defined(PICO_BUILD)
            response[N_INTERMEDIATE_CTR_CHARS + 18] += 0b00000010;
#endif
            response[N_INTERMEDIATE_CTR_CHARS + 19] = shortId & 0xff;
            response[N_INTERMEDIATE_CTR_CHARS + 20] = (shortId >> 8) & 0xff;
#if defined(ARDUINO_ESP32_S3_N16R8)
            response[N_INTERMEDIATE_CTR_CHARS + 21] = 1;  // ESP32 S3
#elif defined(DISPLAY_RM67162_AMOLED)
            response[N_INTERMEDIATE_CTR_CHARS + 21] = 2;  // ESP32 S3 with
                                                          // RM67162
#elif defined(PICO_BUILD)
#ifdef BOARD_HAS_PSRAM
            response[N_INTERMEDIATE_CTR_CHARS + 21] = 3;  // RP2350
#else
            response[N_INTERMEDIATE_CTR_CHARS + 21] = 4;  // RP2040
#endif
#else
            response[N_INTERMEDIATE_CTR_CHARS + 21] = 0;  // ESP32
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
            if (transport->isWifiAndActive()) break;
            return 1;
          }
#ifndef DISPLAY_RM67162_AMOLED
          case 23:  // set RGB order
          {
            rgbMode = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }
#endif
#if !defined(ZEDMD_NO_NETWORKING) && !defined(DMDREADER)
          case 26:  // set wifi power
          {
            wifiPower = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

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
                if (transport->isWifi()) {
                  ((WifiTransport *)transport)->ssid = String(tmpStringBuffer);
                  ((WifiTransport *)transport)->ssid_length = payloadSize;
                }
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
                if (transport->isWifi()) {
                  ((WifiTransport *)transport)->ssid = String(tmpStringBuffer);
                  ((WifiTransport *)transport)->ssid_length = payloadSize;
                }
                pos += payloadMissing;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            if (transport->isWifiAndActive()) break;
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
                if (transport->isWifi()) {
                  ((WifiTransport *)transport)->pwd = String(tmpStringBuffer);
                  ((WifiTransport *)transport)->pwd_length = payloadSize;
                }
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
                if (transport->isWifi()) {
                  ((WifiTransport *)transport)->pwd = String(tmpStringBuffer);
                  ((WifiTransport *)transport)->pwd_length = payloadSize;
                }
                pos += payloadMissing;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            if (transport->isWifiAndActive()) break;
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
                if (transport->isWifi()) {
                  memcpy(tmpStringBuffer, &pData[pos], payloadSize);
                  ((WifiTransport *)transport)->port = tmpStringBuffer[0] << 8;
                  ((WifiTransport *)transport)->port |= tmpStringBuffer[1];
                }
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
                if (transport->isWifi()) {
                  ((WifiTransport *)transport)->port = tmpStringBuffer[0] << 8;
                  ((WifiTransport *)transport)->port |= tmpStringBuffer[1];
                }
                pos += payloadMissing;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            if (transport->isWifiAndActive()) break;
            return 1;
          }
#endif
          case 30:  // save settings 0x1e
          {
            if (transport->getType() == Transport::USB) {
              // send fast ack
              Serial.write(CtrlChars, N_ACK_CHARS);
              Serial.flush();
            }
            display->DisplayText("Saving settings ...", 0, 0, 255, 0, 0);
            display->Render();
            SaveLum();
            SaveDebug();
            SaveTransport();
#ifndef DMDREADER
            SaveUsbPackageSizeMultiplier();
#endif
            transport->saveDelay();
            transport->saveConfig();
#if defined(DISPLAY_LED_MATRIX)
            SaveRgbOrder();
            SavePanelSettings();
#endif
#ifdef ZEDMD_HD_HALF
            SaveYOffset();
#endif
            display->DisplayText("Saving settings ... done", 0, 0, 255, 0, 0);
            display->Render();
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 3;
          }

          case 31:  // reset 0x1f
          {
            if (transport->getType() == Transport::USB) {
              Serial.write(CtrlChars, N_ACK_CHARS);
              Serial.flush();
            }
            Restart();
          }

#ifdef PICO_BUILD
          case 32:  // reboot to bootloader for easy updating
          {
            rebootToBootloader = true;
            Restart();
          }
#endif

#ifndef DISPLAY_RM67162_AMOLED
          case 40:  // set panelClkphase
          {
            panelClkphase = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 41:  // set panelI2sspeed
          {
            panelI2sspeed = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 42:  // set panelLatchBlanking
          {
            panelLatchBlanking = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 43:  // set panelMinRefreshRate
          {
            panelMinRefreshRate = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 44:  // set panelDriver
          {
            panelDriver = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }
#endif
          case 45:  // set transport
          {
            // TODO: verify this (need Save ? need Reset ?)
            transport->setType(pData[pos++]);
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 46:  // set udpDelay
          {
            transport->setDelay(pData[pos++]);
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 47:  // set usbPackageSizeMultiplier
          {
            usbPackageSizeMultiplier = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }
#ifndef DISPLAY_RM67162_AMOLED
          case 48:  // set yOffset
          {
            yOffset = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }
#endif

#ifdef SPEAKER_LIGHTS
          case 100:  // set speakerLightsBlackThreshold
          {
            speakerLightsBlackThreshold = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 101:  // set speakerLightsGammaFactor
          {
            speakerLightsGammaFactor = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }
          case 102:  // set speakerLightsLeftNumLeds
          {
            speakerLightsLeftNumLeds = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 103:  // set speakerLightsLeftLedType
          {
            speakerLightsLeftLedType = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 104:  // set speakerLightsLeftMode
          {
            speakerLightsLeftMode = pData[pos++];
            if (speakerLightsLeftNumLeds > 0) {
              speakerLightsLeft->setMode(speakerLightsLeftMode);
            }
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 105:  // set speakerLightsLeftColor
          {
            if (payloadMissing == payloadSize) {
              memset(tmpStringBuffer, 0, 3);
              if (payloadMissing > (len - pos)) {
                memcpy(tmpStringBuffer, &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(tmpStringBuffer, &pData[pos], payloadSize);
                speakerLightsLeftColor = tmpStringBuffer[0] +
                                         (tmpStringBuffer[1] << 8) +
                                         (tmpStringBuffer[2] << 16);
                if (speakerLightsLeftNumLeds > 0) {
                  speakerLightsLeft->setColor(speakerLightsLeftColor);
                }
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
                speakerLightsLeftColor = tmpStringBuffer[0] +
                                         (tmpStringBuffer[1] << 8) +
                                         (tmpStringBuffer[2] << 16);
                if (speakerLightsLeftNumLeds > 0) {
                  speakerLightsLeft->setColor(speakerLightsLeftColor);
                }
                pos += payloadMissing;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 106:  // set speakerLightsRightNumLeds
          {
            speakerLightsRightNumLeds = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 107:  // set speakerLightsRightLedType
          {
            speakerLightsRightLedType = pData[pos++];
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 108:  // set speakerLightsRightMode
          {
            speakerLightsRightMode = pData[pos++];
            if (speakerLightsRightNumLeds > 0) {
              speakerLightsRight->setMode(speakerLightsRightMode);
            }
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 109:  // set speakerLightsRightColor
          {
            if (payloadMissing == payloadSize) {
              memset(tmpStringBuffer, 0, 3);
              if (payloadMissing > (len - pos)) {
                memcpy(tmpStringBuffer, &pData[pos], len - pos);
                payloadMissing -= len - pos;
                pos += len - pos;
                break;
              } else {
                memcpy(tmpStringBuffer, &pData[pos], payloadSize);
                speakerLightsRightColor = tmpStringBuffer[0] +
                                          (tmpStringBuffer[1] << 8) +
                                          (tmpStringBuffer[2] << 16);
                if (speakerLightsRightNumLeds > 0) {
                  speakerLightsRight->setColor(speakerLightsRightColor);
                }
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
                speakerLightsRightColor = tmpStringBuffer[0] +
                                          (tmpStringBuffer[1] << 8) +
                                          (tmpStringBuffer[2] << 16);
                if (speakerLightsRightNumLeds > 0) {
                  speakerLightsRight->setColor(speakerLightsRightColor);
                }
                pos += payloadMissing;
                payloadMissing = 0;
                headerBytesReceived = 0;
                numCtrlCharsFound = 0;
              }
            }
            if (transport->isWifiAndActive()) break;
            return 1;
          }
#endif

          case 16: {
#ifndef DMDREADER
            if (transport->getType() == Transport::USB) {
              Serial.write(CtrlChars, N_ACK_CHARS);
              Serial.flush();
            }
#endif
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
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 11:  // KeepAlive
          {
            if (debug) {
              display->DisplayText("KEEP ALIVE RECEIVED",
                                   7 * (TOTAL_WIDTH / 128),
                                   (TOTAL_HEIGHT / 2) - 10, 128, 128, 128);
              display->Render();
            }
            lastDataReceivedClock.restart();
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 98:  // disable debug mode
          {
            debug = 0;
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 99:  // enable debug mode
          {
            debug = 1;
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          case 4:  // RGB888 Zones Stream
            rgb888ZoneStream = true;
            // no break; here, continue to case 5

          case 5: {  // RGB565 Zones Stream
            rgb565ZoneStream = !rgb888ZoneStream;
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
#if (defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)) || \
    defined(PICO_BUILD)
            AcquireNextBuffer();
            bufferCompressed[currentBuffer] = false;
            bufferSizes[currentBuffer] = 2;
            buffers[currentBuffer][0] = 255;
            buffers[currentBuffer][1] = 255;
            MarkCurrentBufferDone();
#endif
            lastDataReceivedClock.restart();
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }

          default: {
            headerBytesReceived = 0;
            numCtrlCharsFound = 0;
            if (transport->isWifiAndActive()) break;
            return 1;
          }
        }
      }
    }
  }

  return 0;
}

void setup() {
#ifndef PICO_BUILD
  esp_log_level_set("*", ESP_LOG_NONE);
#else
  // tested working on a lot of different devices
  vreg_set_voltage(VREG_VOLTAGE_1_15);
  busy_wait_at_least_cycles((SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US *
                             static_cast<uint64_t>(XOSC_HZ)) /
                            1000000);
  // overclock to achieve higher SPI transfer speed
  set_sys_clock_khz(SYS_CLK_MHZ * 1000, true);
#endif

#ifdef DMDREADER
  pinMode(LED_BUILTIN, OUTPUT);
#endif

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
  logoActive = true;
  transportActive = false;
  transportWaitCounter = 0;
  logoWaitCounterClock.restart();
  lastDataReceivedClock.restart();

  uint64_t chipId = ESP.getEfuseMac();
  shortId =
      (uint16_t)(chipId ^ (chipId >> 16) ^ (chipId >> 32) ^ (chipId >> 48));

#ifdef SPEAKER_LIGHTS
  speakerLightsLeftNumLeds = 0;
  speakerLightsRightNumLeds = 0;
  speakerLightsLeftLedType = NEO_GBR;
  speakerLightsRightLedType = NEO_GBR;
  speakerLightsLeftMode = FX_MODE_RAINBOW_CYCLE;
  speakerLightsRightMode = FX_MODE_RAINBOW_CYCLE;
  speakerLightsLeftColor = 0x555555;
  speakerLightsRightColor = 0x555555;

  speakerLightsBlackThreshold = 30;
  speakerLightsGammaFactor = 180;
#endif

  bool fileSystemOK;
  if ((fileSystemOK = LittleFS.begin())) {
    LoadSettingsMenu();
    LoadTransport();

#ifdef DMDREADER
    LoadColor();
#else
    LoadUsbPackageSizeMultiplier();
#endif
#if defined(DISPLAY_LED_MATRIX) || defined(DISPLAY_PICO_LED_MATRIX)
    LoadRgbOrder();
    LoadPanelSettings();
#endif
    LoadLum();
    LoadDebug();
#ifdef ZEDMD_HD_HALF
    LoadYOffset();
#endif
#ifdef SPEAKER_LIGHTS
    LoadSpeakerLightsSettings();
#endif
  } else {
    TransportCreate();
  }

#ifdef DISPLAY_RM67162_AMOLED
  display = (DisplayDriver *)new Rm67162Amoled();  // For AMOLED display
#elif defined(DISPLAY_LED_MATRIX)
#if PICO_BUILD
  display =
      (DisplayDriver *)new PicoLedMatrix();  // For pico LED matrix display
#else
  display =
      (DisplayDriver *)new Esp32LedMatrix();  // For ESP32 LED matrix display
#endif
#endif
  display->SetBrightness(brightness);

  if (!fileSystemOK) {
    display->DisplayText("Error reading file system!", 0, 0, 255, 0, 0);
    display->DisplayText("Try to flash the firmware again.", 0, 6, 255, 0, 0);
    display->Render();
    while (true);
  }

  switch (esp_reset_reason()) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_CPU_LOCKUP: {
      display->DisplayText("An unrecoverable error happend!", 0, 0, 255, 0, 0);
      display->DisplayText("Coredump has been written. See", 0, 6, 255, 0, 0);
      display->DisplayText("ppuc.org/ZeDMD how to download", 0, 12, 255, 0, 0);
      display->DisplayText("it. Error code: ", 0, 18, 255, 0, 0);
      DisplayNumber(esp_reset_reason(), 2, 16 * 4, 18, 255, 0, 0);
      if (debug) {
        display->DisplayText("Reboot in 30 seconds ...", 0, 24, 255, 0, 0);
        display->Render();
        for (uint8_t i = 29; i > 0; i--) {
          delay(1000);
          DisplayNumber(i, 2, 40, 24, 255, 0, 0);
          display->Render();
        }
        Restart();
      }
      break;
    }

    case ESP_RST_PWR_GLITCH: {
      display->DisplayText("A power glitch caused a restart!", 0, 0, 255, 0, 0);
      display->DisplayText("Check your power supply and", 0, 6, 255, 0, 0);
      display->DisplayText("hardware.", 0, 12, 255, 0, 0);
      display->DisplayText("Reboot in 30 seconds ...", 0, 24, 255, 0, 0);
      display->Render();
      for (uint8_t i = 29; i > 0; i--) {
        delay(1000);
        DisplayNumber(i, 2, 40, 24, 255, 0, 0);
        display->Render();
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
      display->Render();
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

    const auto forwardButton = new Bounce2::Button();
    forwardButton->attach(FORWARD_BUTTON_PIN, INPUT_PULLUP);
    forwardButton->interval(100);
    forwardButton->setPressedState(LOW);

    const auto upButton = new Bounce2::Button();
    upButton->attach(UP_BUTTON_PIN, INPUT_PULLUP);
    upButton->interval(100);
    upButton->setPressedState(LOW);

#if defined(ARDUINO_ESP32_S3_N16R8) || defined(PICO_BUILD)
    const auto backwardButton = new Bounce2::Button();
    backwardButton->attach(BACKWARD_BUTTON_PIN, INPUT_PULLUP);
    backwardButton->interval(100);
    backwardButton->setPressedState(LOW);

    const auto downButton = new Bounce2::Button();
    downButton->attach(DOWN_BUTTON_PIN, INPUT_PULLUP);
    downButton->interval(100);
    downButton->setPressedState(LOW);
#endif

    uint8_t position = 1;
    bool firstMenuRendering = true;
    while (1) {
      forwardButton->update();
      const bool forward = forwardButton->pressed();
      bool backward = false;
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(PICO_BUILD)
      backwardButton->update();
      backward = backwardButton->pressed();
#endif
      bool buttonPressed = forward || backward;
      if (buttonPressed) {
        if (forward && ++position > MENU_ITEMS_COUNT)
          position = 1;
        else if (backward && --position < 1)
          position = MENU_ITEMS_COUNT;
        // skip disabled transport menus
        if (transport->isUsb()) {
          if (position == 4) position = forward ? 5 : 3;
        } else if (transport->isSpi()) {
          if (position == 3) position = forward ? 4 : 2;
        } else {
          if (position == 3) position = forward ? 4 : 2;
        }

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
#ifdef DMDREADER
          case 4: {  // Color
            RefreshSetupScreen();
            display->DisplayText("Color:", 7 * (TOTAL_WIDTH / 128),
                                 TOTAL_HEIGHT / 2 + 4, 255, 191, 0);
            break;
          }
#else
          case 4: {  // UDP Delay
            RefreshSetupScreen();
            display->DisplayText("UDP Delay:", 7 * (TOTAL_WIDTH / 128),
                                 TOTAL_HEIGHT / 2 + 4, 255, 191, 0);
            break;
          }
#endif
          case 5: {  // Transport
            RefreshSetupScreen();
            display->DisplayText(transport->getTypeString(),
                                 7 * (TOTAL_WIDTH / 128),
                                 (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
            break;
          }
          case 6: {  // Debug
            RefreshSetupScreen();
            display->DisplayText("Debug:", 7 * (TOTAL_WIDTH / 128),
                                 (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
            break;
          }
          case 7: {  // RGB order
            RefreshSetupScreen();
            DisplayRGB(255, 191, 0);
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
      const bool up = upButton->pressed();
      bool down = false;
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(PICO_BUILD)
      downButton->update();
      down = downButton->pressed();
#endif
      buttonPressed = buttonPressed || up || down;
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
#ifdef DMDREADER
          case 4: {  // Color
            if (up && ++loopbackColor > ((uint8_t)Color::DMD_WHITE))
              loopbackColor = ((uint8_t)Color::DMD_ORANGE);
            else if (down &&
                     --loopbackColor >
                         ((uint8_t)
                              Color::DMD_WHITE))  // underflow will result in
                                                  // 255, set it to DMD_WHITE
              loopbackColor = ((uint8_t)Color::DMD_WHITE);

            display->DisplayText(ColorString(loopbackColor),
                                 7 * (TOTAL_WIDTH / 128) + (6 * 4),
                                 TOTAL_HEIGHT / 2 + 4, 255, 191, 0);
            SaveColor();
            break;
          }
#else
          case 4: {  // UDP Delay
            uint8_t delay = transport->getDelay();
            if (up && ++delay > 9)
              delay = 0;
            else if (down &&
                     --delay > 9)  // underflow will result in 255, set it to 9
              delay = 9;

            DisplayNumber(delay, 1, 7 * (TOTAL_WIDTH / 128) + 10 * 4,
                          TOTAL_HEIGHT / 2 + 4, 255, 191, 0);
            transport->setDelay(delay);
            transport->saveDelay();
            break;
          }
#endif
          case 5: {  // Transport
#ifdef ZEDMD_NO_NETWORKING
            const uint8_t type = transport->getType() == Transport::USB
                                     ? Transport::SPI
                                     : Transport::USB;
#else
            uint8_t type = transport->getType();
            if (up && ++type > Transport::SPI)
              type = Transport::USB;
            else if (down && --type > Transport::SPI)
              type = Transport::SPI;
#endif
            SaveTransport(type);
            RefreshSetupScreen();
            display->DisplayText(transport->getTypeString(),
                                 7 * (TOTAL_WIDTH / 128),
                                 (TOTAL_HEIGHT / 2) - 3, 255, 191, 0);
            break;
          }
          case 6: {  // Debug
            if (++debug > 1) debug = 0;
            DisplayNumber(debug, 1, 7 * (TOTAL_WIDTH / 128) + (6 * 4),
                          (TOTAL_HEIGHT / 2) - 10, 255, 191, 0);
            SaveDebug();
            break;
          }
          case 7: {  // RGB order
            if (rgbModeLoaded != 0) {
              rgbMode = 0;
              SaveRgbOrder();
              delay(10);
              Restart();
            }
            if (up && ++rgbMode > 5)
              rgbMode = 0;
            else if (down &&
                     --rgbMode >
                         5)  // underflow will result in 255, set it to 5
              rgbMode = 5;
            RefreshSetupScreen();
            DisplayRGB(255, 191, 0);
            SaveRgbOrder();
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
      if (buttonPressed || firstMenuRendering) {
        firstMenuRendering = false;
        display->Render();
      }
      delay(1);
    }
  }
#endif

  pinMode(FORWARD_BUTTON_PIN, INPUT_PULLUP);
#ifdef PICO_BUILD
  // do not leave some pin configured as adc / floating
  pinMode(BACKWARD_BUTTON_PIN, INPUT_PULLUP);
  pinMode(UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOWN_BUTTON_PIN, INPUT_PULLUP);
#endif

  InitRgbLuts();
  DisplayLogo();
  DisplayId();
  display->Render();

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
      display->Render();
      while (1);
    }
  }

#ifdef SPEAKER_LIGHTS
  if (speakerLightsLeftNumLeds > 0) {
    speakerLightsLeft =
        new WS2812FX(speakerLightsLeftNumLeds, SPEAKER_LIGHTS_LEFT_PIN,
                     speakerLightsLeftLedType);
    speakerLightsLeft->init();
    speakerLightsLeft->setBrightness(100);
    speakerLightsLeft->setSpeed(200);
    speakerLightsLeft->setMode(speakerLightsLeftMode);
    speakerLightsLeft->start();
  }

  if (speakerLightsRightNumLeds > 0) {
    speakerLightsRight =
        new WS2812FX(speakerLightsRightNumLeds, SPEAKER_LIGHTS_RIGHT_PIN,
                     speakerLightsRightLedType);
    speakerLightsRight->init();
    speakerLightsRight->setBrightness(100);
    speakerLightsRight->setSpeed(200);
    speakerLightsRight->setMode(speakerLightsRightMode);
    speakerLightsRight->start();
  }
#endif

#ifdef DMDREADER
  static_cast<SpiTransport *>(transport)->SetColor((Color)loopbackColor);
#endif
  transport->init();

#ifdef DMDREADER
  core_0_initialized = true;
#endif
}

void loop() {
  CheckMenuButton();

#ifdef SPEAKER_LIGHTS
  if (speakerLightsLeftNumLeds > 0) {
    speakerLightsLeft->service();
  }

  if (speakerLightsRightNumLeds > 0) {
    speakerLightsRight->service();
  }
#endif

#ifdef DMDREADER
  if (!core_1_initialized) {
    delay(1);
    return;
  }

  auto *spiTransport = static_cast<SpiTransport *>(transport);
  if (spiTransport->GetFrameReceived()) {
    const uint16_t *src =
        reinterpret_cast<const uint16_t *>(spiTransport->GetDataBuffer());
    uint8_t *dst = renderBuffer[currentRenderBuffer];
    const size_t pixelCount = RGB565_TOTAL_BYTES / 2;

    for (size_t i = 0; i < pixelCount; ++i) {
      const uint16_t rgb565 = src[i];
      *dst++ = Expand5To8[rgb565 >> 11];
      *dst++ = Expand6To8[(rgb565 >> 5) & 0x3f];
      *dst++ = Expand5To8[rgb565 & 0x1f];
    }
    Render();
  } else if (transport->isLoopback()) {
    uint8_t *buffer = dmdreader_loopback_render();
    if (buffer != nullptr) {
      if (TOTAL_WIDTH == 256 && TOTAL_HEIGHT == 64 &&
          dmdreader_get_source_width() * 2 == TOTAL_WIDTH &&
          dmdreader_get_source_height() * 2 == TOTAL_HEIGHT) {
        Scale2xLoopback(buffer, renderBuffer[currentRenderBuffer],
                        dmdreader_get_source_width(),
                        dmdreader_get_source_height());
      } else {
        memcpy(renderBuffer[currentRenderBuffer], buffer, TOTAL_BYTES);
      }
      Render();
    }
  }
  tight_loop_contents();

#else

  if (!transportActive) {
    if (!logoActive) logoActive = true;

    if (!updateActive &&
        logoWaitCounterClock.getElapsedTime().asSeconds() > 10) {
      updateActive = true;
      DisplayUpdate();
    }

    if (!saverActive &&
        logoWaitCounterClock.getElapsedTime().asSeconds() > 20) {
      saverActive = true;
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

    display->Render();

    transportWaitCounter = (transportWaitCounter + 1) % 8;

#ifndef DMDREADER
    vTaskDelay(pdMS_TO_TICKS(200));
#else
    // Code is usding interrupts, so delay is fine.
    delay(200);
#endif
  } else {
    if (lastDataReceivedClock.getElapsedTime().asMilliseconds() >
        CONNECTION_TIMEOUT) {
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
        Render();
      } else if (2 == bufferSizes[processingBuffer] &&
                 0 == buffers[processingBuffer][0] &&
                 0 == buffers[processingBuffer][1]) {
        ClearScreen();
      } else {
        if (bufferCompressed[processingBuffer]) {
          memset(uncompressBuffer, 0, 2048);
          uncompressedBufferSize = 2048;
          int minizStatus = mz_uncompress2(
              uncompressBuffer, &uncompressedBufferSize,
              buffers[processingBuffer], &bufferSizes[processingBuffer]);

          if (MZ_OK != minizStatus) {
            if (1 == debug) {
              display->DisplayText("miniz error: ", 0, 0, 255, 0, 0);
              DisplayNumber(minizStatus, 3, 13 * 4, 0, 255, 0, 0);
              display->DisplayText("free heap: ", 0, 6, 255, 0, 0);
              display->Render();
#ifdef PICO_BUILD
              DisplayNumber(rp2040.getFreeHeap(), 8, 11 * 4, 6, 255, 0, 0);
#else
              DisplayNumber(esp_get_free_heap_size(), 8, 11 * 4, 6, 255, 0, 0);
#endif
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
#if (defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)) || \
    defined(PICO_BUILD)
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
#if (defined(BOARD_HAS_PSRAM) && (NUM_RENDER_BUFFERS > 1)) || \
    defined(PICO_BUILD)
            uint8_t idx = uncompressBuffer[uncompressedBufferPosition++];
            const uint8_t yOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
            const uint8_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

            if (rgb565ZoneStream) {
              for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
                uint8_t *dst =
                    &renderBuffer[currentRenderBuffer]
                                 [((yOffset + y) * TOTAL_WIDTH + xOffset) * 3];
                for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
                  const uint16_t rgb565 =
                      uncompressBuffer[uncompressedBufferPosition++] +
                      (((uint16_t)
                            uncompressBuffer[uncompressedBufferPosition++])
                       << 8);
                  *dst++ = Expand5To8[rgb565 >> 11];
                  *dst++ = Expand6To8[(rgb565 >> 5) & 0x3f];
                  *dst++ = Expand5To8[rgb565 & 0x1f];
                }
              }
            } else {
              for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
                memcpy(&renderBuffer[currentRenderBuffer]
                                    [((yOffset + y) * TOTAL_WIDTH + xOffset)],
                       &uncompressBuffer[uncompressedBufferPosition],
                       3 * ZONE_WIDTH);
                uncompressedBufferPosition += 3 * ZONE_WIDTH;
              }
            }
#else
            if (rgb565ZoneStream) {
              display->FillZoneRaw565(
                  uncompressBuffer[uncompressedBufferPosition++],
                  &uncompressBuffer[uncompressedBufferPosition]);
              uncompressedBufferPosition += RGB565_ZONE_SIZE;
            } else {
              display->FillZoneRaw(
                  uncompressBuffer[uncompressedBufferPosition++],
                  &uncompressBuffer[uncompressedBufferPosition]);
              uncompressedBufferPosition += ZONE_SIZE;
            }
#endif
          }
        }
      }
    } else {
      // Avoid busy-waiting
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
#endif
}

#ifdef DMDREADER

void setup1() {
  while (!core_0_initialized) {
    delay(1);
  }

  static_cast<SpiTransport *>(transport)->initDmdReader();

  core_1_initialized = true;
}

void loop1() {
  if (!transport->isLoopback()) {
    dmdreader_spi_send();
    tight_loop_contents();
  } else {
    delay(1);
  }
}
#endif
