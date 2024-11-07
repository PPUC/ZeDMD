
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#define SERIAL_BAUD 8000000  // Serial baud rate.
#else
#define SERIAL_BAUD 921600  // Serial baud rate.
#endif
#define SERIAL_TIMEOUT \
  8  // Time in milliseconds to wait for the next data chunk.
#define SERIAL_BUFFER 2048  // Serial buffer size in byte.
#define SERIAL_CHUNK_SIZE_MAX 1888
#define LOGO_TIMEOUT 20000  // Time in milliseconds before the logo vanishes.
#define FLOW_CONTROL_TIMEOUT \
  4  // Time in milliseconds to wait before sending a new ready signal.
#define DISPLAY_STATUS_SCREEN_SAVER 0      // screen saver
#define DISPLAY_STATUS_NORMAL_OPERATION 1  // ZeDMD logo / normal operation mode
#define DISPLAY_STATUS_INFO 2              // PPUC info screen
#define DISPLAY_STATUS_CLEAR 3             // clear screen (command received)
#define DISPLAY_STATUS_DIM 4               // dim screen
#define SCREENSAVER_MODE_CLEAR_SCREEN \
  0  // Screensaver is set to clear screen mode
#define SCREENSAVER_MODE_SHOW_IMAGE 1  // Screensaver is set to show image mode
#define SCREENSAVER_DEFAULT_DIM_TIMEOUT 60000  // Default dim timeout

// ----------------- ZeDMD by MK47 & Zedrummer (http://ppuc.org) ---------------
// - If you have blurry pictures, the display is not clean, try to reduce the
//   input voltage of your LED matrix panels, often, 5V panels need
//   between 4V and 4.5V to display clean pictures, you often have a screw in
//   switch-mode power supplies to change the output voltage a little bit
// - While the initial pattern logo is displayed, check you have red in the
//   upper left, green in the lower left and blue in the upper right,
//   if not, make contact between the RGB_ORDER_BUTTON_PIN (default 21, but you
//   can change below) pin and a ground pin several times until the display is
//   correct (automatically saved, no need to do it again)
// -----------------------------------------------------------------------------
// By sending command 99, you can enable the "Debug Mode". The output will be:
// number of frames received, regardless if any error happened, size of
// compressed frame if compression is enabled, size of currently received bytes
// of frame (compressed or decompressed), error code if the decompression if
// compression is enabled, number of incomplete frames, number of resets
// because of communication freezes
// -----------------------------------------------------------------------------
// Commands:
//  2: set rom frame size as (int16) width, (int16) height
//  3: render raw data, RGB24
//  4: render raw data, RGB24 zones streaming
//  5: render raw data, RGB565 zones streaming
//  6: init palette (deprectated, backward compatibility)
//  7: render 16 colors using a 4 color palette (3*4 bytes), 2 pixels per byte
//  8: render 4 colors using a 4 color palette (3*4 bytes), 4 pixels per byte
//  9: render 16 colors using a 16 color palette (3*16 bytes), 4 bytes per group
//     of 8 pixels (encoded as 4*512 bytes planes)
// 10: clear screen
// 11: render 64 colors using a 64 color palette (3*64 bytes), 6 bytes per group
//     of 8 pixels (encoded as 6*512 bytes planes) 12: handshake + report
//     resolution, returns (int16) width, (int16) height 13: set serial transfer
//     chunk size as (int8) value, the value will be multiplied with 32
//     internally
// 12: handshake
// 13: set serial transfer chunk size
// 14: enable serial transfer compression
// 15: disable serial transfer compression
// 16: panel LED check, screen full red, then full green, then full blue
// 20: turn off upscaling
// 21: turn on upscaling
// 22: set brightness as (int8) value between 1 and 15
// 23: set RGB order
// 24: get brightness, returns (int8) brigtness value between 1 and 15
// 25: get RGB order, returns (int8) major, (int8) minor, (int8) patch level
// 26: turn on flow control version 2
// 27: set WiFi SSID
// 28: set WiFi password
// 29: set WiFi port
// 30: save settings
// 31: reset
// 32: get version string, returns (int8) major, (int8) minor, (int8) patch
//     level
// 33: get panel resolution, returns (int16) width, (int16) height
// 98: disable debug mode
// 99: enable debug mode

#define MAX_COLOR_ROTATIONS 8
#define LED_CHECK_DELAY 1000  // ms per color
#define DEBUG_DELAY 5000      // ms

#include <Arduino.h>
#include <Bounce2.h>
#include <LittleFS.h>

#include <cstring>

#include "displayConfig.h"  // Variables shared by main and displayDrivers
#include "displayDriver.h"  // Base class for all display drivers
#include "esp_task_wdt.h"
#include "miniz/miniz.h"
#include "panel.h"    // ZeDMD panel constants
#include "version.h"  // Version constants

// To save RAM only include the driver we want to use.
#ifdef DISPLAY_RM67162_AMOLED
#include "displays/Rm67162Amoled.h"
#elif defined(DISPLAY_LED_MATRIX)
#include "displays/LEDMatrix.h"
#endif

// Specific improvements and #define for the ESP32 S3 series
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#include "S3Specific.h"
#endif

#ifdef ZEDMD_WIFI
#include <AsyncUDP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <JPEGDEC.h>
#include <WiFi.h>

#include "webserver.h"

String ssid;
String pwd;
uint16_t port = 3333;
uint8_t ssid_length;
uint8_t pwd_length;

AsyncUDP udp;
JPEGDEC jpeg;

const char *apSSID = "ZeDMD-WiFi";
const char *apPassword = "zedmd1234";

uint8_t screensaverMode = SCREENSAVER_MODE_CLEAR_SCREEN;
uint32_t dimTimeout =
    SCREENSAVER_DEFAULT_DIM_TIMEOUT;  // Timeout for dimming the screen
bool enableDimAfterTimeout = false;   // Should dim after timeout
bool drawingInProgress = false;
#else
// color rotation
uint8_t rotFirstColor[MAX_COLOR_ROTATIONS];
uint8_t rotAmountColors[MAX_COLOR_ROTATIONS];
unsigned int rotStepDurationTime[MAX_COLOR_ROTATIONS];
unsigned long rotNextRotationTime[MAX_COLOR_ROTATIONS];
uint8_t tmpColor[3] = {0};

bool upscaling = true;
#endif

unsigned long displayTimeout = 0;

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

#define N_CTRL_CHARS 6
#define N_INTERMEDIATE_CTR_CHARS 4

// !!!!! DO NOT PUT ANY IDENTICAL VALUE !!!!!
uint8_t CtrlCharacters[6] = {0x5a, 0x65, 0x64, 0x72, 0x75, 0x6d};
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

Bounce2::Button *rgbOrderButton;
Bounce2::Button *brightnessButton;

DisplayDriver *display;

bool debugMode = false;
bool debugDelayOnError = false;
uint8_t c4;
int16_t transferBufferSize = 0;
int16_t receivedBytes = 0;
int16_t minizStatus = 0;
uint8_t *palette;
uint8_t *renderBuffer;
bool mode64 = false;
uint16_t RomWidth = 128, RomHeight = 32;
uint8_t RomWidthPlane = 128 >> 3;
uint8_t lumstep = 5;  // Init display on medium brightness, otherwise it starts
                      // up black on some displays
bool MireActive = false;
uint8_t displayStatus = DISPLAY_STATUS_NORMAL_OPERATION;
bool handshakeSucceeded = false;
bool compression = false;
// 256 is the default buffer size of the CP210x linux kernel driver, we should
// not exceed it as default.
uint16_t serialTransferChunkSize = 256;
uint16_t frameCount = 0;
uint16_t errorCount = 0;
uint8_t flowControlCounter = 0;

void DoRestart(int sec) {
#ifdef ZEDMD_WIFI
  MDNS.end();
  WiFi.disconnect(true);
#endif
  sleep(1);
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

void DisplayLum(void) {
  display->DisplayText(" ", (TOTAL_WIDTH / 2) - 26 - 1, TOTAL_HEIGHT - 6, 128,
                       128, 128);
  display->DisplayText("Brightness:", (TOTAL_WIDTH / 2) - 26, TOTAL_HEIGHT - 6,
                       128, 128, 128);
  DisplayNumber(lumstep, 2, (TOTAL_WIDTH / 2) + 18, TOTAL_HEIGHT - 6, 255, 191,
                0);
}

void DisplayRGB(void) {
  display->DisplayText("red", 0, 0, 0, 0, 0, true, true);
  for (uint8_t i = 0; i < 6; i++) {
    display->DrawPixel(TOTAL_WIDTH - (4 * 4) - 1, i, 0, 0, 0);
    display->DrawPixel((TOTAL_WIDTH / 2) - (6 * 4) - 1, i, 0, 0, 0);
  }
  display->DisplayText("blue", TOTAL_WIDTH - (4 * 4), 0, 0, 0, 0, true, true);
  display->DisplayText("green", 0, TOTAL_HEIGHT - 6, 0, 0, 0, true, true);
  display->DisplayText("RGB Order:", (TOTAL_WIDTH / 2) - (6 * 4), 0, 128, 128,
                       128);
  DisplayNumber(rgbMode, 2, (TOTAL_WIDTH / 2) + (4 * 4), 0, 255, 191, 0);
}

void DisplayDebugInfo(void) {
  // WTF not comparing against true kills the debug mode
  if (debugMode == true) {
    display->DisplayText("Frames:", 0, 0, 255, 255, 255);
    DisplayNumber(frameCount, 5, 7 * 4, 0, 0, 255, 0);
    display->DisplayText("Transfer Buffer:", 0, 6, 255, 255, 255);
    DisplayNumber(transferBufferSize, 5, 16 * 4, 6, 255, 255, 255);
    display->DisplayText("Received Bytes: ", 0, 2 * 6, 255, 255, 255);
    DisplayNumber(receivedBytes, 5, 16 * 4, 2 * 6, 255, 255, 255);
    display->DisplayText("Miniz Status:", 0, 3 * 6, 255, 255, 255);
    DisplayNumber(minizStatus, 6, 13 * 4, 3 * 6, 255, 255, 255);
    display->DisplayText("Errors:", 0, 4 * 6, 255, 255, 255);
    DisplayNumber(errorCount, 5, 7 * 4, 4 * 6, 255, 0, 0);

    DisplayNumber(RomWidth, 3, TOTAL_WIDTH - 6 * 4, 0, 255, 255, 255);
    display->DisplayText("x", TOTAL_WIDTH - 3 * 4, 0, 255, 255, 255);
    DisplayNumber(RomHeight, 2, TOTAL_WIDTH - 2 * 4, 0, 255, 255, 255);
    DisplayNumber(flowControlCounter, 2, TOTAL_WIDTH - 5 * 4, TOTAL_HEIGHT - 6,
                  0, 0, 255);
    DisplayNumber(c4, 2, TOTAL_WIDTH - 2 * 4, TOTAL_HEIGHT - 6, 255, 255, 255);
  }
}

/// @brief Get DisplayDriver object, required for webserver
DisplayDriver *GetDisplayObject() { return display; }

void ClearScreen() {
  display->ClearScreen();
  display->SetBrightness(lumstep);
}

#if !defined(ZEDMD_WIFI)
bool CmpColor(uint8_t *px1, uint8_t *px2, uint8_t colors) {
  if (colors == 3) {
    return (px1[0] == px2[0]) && (px1[1] == px2[1]) && (px1[2] == px2[2]);
  }

  return px1[0] == px2[0];
}

void SetColor(uint8_t *px1, uint8_t *px2, uint8_t colors) {
  px1[0] = px2[0];

  if (colors == 3) {
    px1[1] = px2[1];
    px1[2] = px2[2];
  }
}

void ScaleImage(uint8_t colors) {
  uint16_t xoffset = 0;
  uint16_t yoffset = 0;
  uint8_t scale = 0;  // 0 - no scale, 1 - half scale, 2 - double scale

  if (RomWidth == 192 && TOTAL_WIDTH == 256) {
    xoffset = 32;
  } else if (RomWidth == 192) {
    xoffset = 16;
    scale = 1;
  } else if (RomHeight == 16 && TOTAL_HEIGHT == 32) {
    yoffset = 8;
  } else if (RomHeight == 16 && TOTAL_HEIGHT == 64) {
    if (upscaling) {
      yoffset = 16;
      scale = 2;
    } else {
      // Just center the DMD.
      xoffset = 64;
      yoffset = 24;
    }
  } else if (RomWidth == 256 && TOTAL_WIDTH == 128) {
    scale = 1;
  } else if (RomWidth == 128 && TOTAL_WIDTH == 256) {
    if (upscaling) {
      // Scaling doesn't look nice for real RGB tables like Diablo.
      scale = 2;
    } else {
      // Just center the DMD.
      xoffset = 64;
      yoffset = 16;
    }
  } else {
    return;
  }

  uint8_t *panel = (uint8_t *)malloc(RomWidth * RomHeight * colors);
  if (panel == nullptr) {
    display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
    display->DisplayText("ScaleImage", 4, 14, 255, 255, 255);
    RestartAfterError();
  }

  memcpy(panel, renderBuffer, RomWidth * RomHeight * colors);
  memset(renderBuffer, 0, TOTAL_WIDTH * TOTAL_HEIGHT);

  if (scale == 1) {
    // for half scaling we take the 4 points and look if there is one colour
    // repeated
    for (uint16_t y = 0; y < RomHeight; y += 2) {
      for (uint16_t x = 0; x < RomWidth; x += 2) {
        uint16_t upper_left = y * RomWidth * colors + x * colors;
        uint16_t upper_right = upper_left + colors;
        uint16_t lower_left = upper_left + RomWidth * colors;
        uint16_t lower_right = lower_left + colors;
        uint16_t target = (xoffset + (x / 2) + (y / 2) * TOTAL_WIDTH) * colors;

        // Prefer most outer upper_lefts.
        if (x < RomWidth / 2) {
          if (y < RomHeight / 2) {
            if (CmpColor(&panel[upper_left], &panel[upper_right], colors) ||
                CmpColor(&panel[upper_left], &panel[lower_left], colors) ||
                CmpColor(&panel[upper_left], &panel[lower_right], colors)) {
              SetColor(&renderBuffer[target], &panel[upper_left], colors);
            } else if (CmpColor(&panel[upper_right], &panel[lower_left],
                                colors) ||
                       CmpColor(&panel[upper_right], &panel[lower_right],
                                colors)) {
              SetColor(&renderBuffer[target], &panel[upper_right], colors);
            } else if (CmpColor(&panel[lower_left], &panel[lower_right],
                                colors)) {
              SetColor(&renderBuffer[target], &panel[lower_left], colors);
            } else {
              SetColor(&renderBuffer[target], &panel[upper_left], colors);
            }
          } else {
            if (CmpColor(&panel[lower_left], &panel[lower_right], colors) ||
                CmpColor(&panel[lower_left], &panel[upper_left], colors) ||
                CmpColor(&panel[lower_left], &panel[upper_right], colors)) {
              SetColor(&renderBuffer[target], &panel[lower_left], colors);
            } else if (CmpColor(&panel[lower_right], &panel[upper_left],
                                colors) ||
                       CmpColor(&panel[lower_right], &panel[upper_right],
                                colors)) {
              SetColor(&renderBuffer[target], &panel[lower_right], colors);
            } else if (CmpColor(&panel[upper_left], &panel[upper_right],
                                colors)) {
              SetColor(&renderBuffer[target], &panel[upper_left], colors);
            } else {
              SetColor(&renderBuffer[target], &panel[lower_left], colors);
            }
          }
        } else {
          if (y < RomHeight / 2) {
            if (CmpColor(&panel[upper_right], &panel[upper_left], colors) ||
                CmpColor(&panel[upper_right], &panel[lower_right], colors) ||
                CmpColor(&panel[upper_right], &panel[lower_left], colors)) {
              SetColor(&renderBuffer[target], &panel[upper_right], colors);
            } else if (CmpColor(&panel[upper_left], &panel[lower_right],
                                colors) ||
                       CmpColor(&panel[upper_left], &panel[lower_left],
                                colors)) {
              SetColor(&renderBuffer[target], &panel[upper_left], colors);
            } else if (CmpColor(&panel[lower_right], &panel[lower_left],
                                colors)) {
              SetColor(&renderBuffer[target], &panel[lower_right], colors);
            } else {
              SetColor(&renderBuffer[target], &panel[upper_right], colors);
            }
          } else {
            if (CmpColor(&panel[lower_right], &panel[lower_left], colors) ||
                CmpColor(&panel[lower_right], &panel[upper_right], colors) ||
                CmpColor(&panel[lower_right], &panel[upper_left], colors)) {
              SetColor(&renderBuffer[target], &panel[lower_right], colors);
            } else if (CmpColor(&panel[lower_left], &panel[upper_right],
                                colors) ||
                       CmpColor(&panel[lower_left], &panel[upper_left],
                                colors)) {
              SetColor(&renderBuffer[target], &panel[lower_left], colors);
            } else if (CmpColor(&panel[upper_right], &panel[upper_left],
                                colors)) {
              SetColor(&renderBuffer[target], &panel[upper_right], colors);
            } else {
              SetColor(&renderBuffer[target], &panel[lower_right], colors);
            }
          }
        }
      }
    }
  } else if (scale == 2) {
    // we implement scale2x http://www.scale2x.it/algorithm
    uint16_t row = RomWidth * colors;
    for (uint16_t x = 0; x < RomHeight; x++) {
      for (uint16_t y = 0; y < RomWidth; y++) {
        uint8_t a[colors], b[colors], c[colors], d[colors], e[colors],
            f[colors], g[colors], h[colors], i[colors];
        for (uint8_t tc = 0; tc < colors; tc++) {
          if (y == 0 && x == 0) {
            a[tc] = b[tc] = d[tc] = e[tc] = panel[tc];
            c[tc] = f[tc] = panel[colors + tc];
            g[tc] = h[tc] = panel[row + tc];
            i[tc] = panel[row + colors + tc];
          } else if ((y == 0) && (x == RomHeight - 1)) {
            a[tc] = b[tc] = panel[(x - 1) * row + tc];
            c[tc] = panel[(x - 1) * row + colors + tc];
            d[tc] = g[tc] = h[tc] = e[tc] = panel[x * row + tc];
            f[tc] = i[tc] = panel[x * row + colors + tc];
          } else if ((y == RomWidth - 1) && (x == 0)) {
            a[tc] = d[tc] = panel[y * colors - colors + tc];
            b[tc] = c[tc] = f[tc] = e[tc] = panel[y * colors + tc];
            g[tc] = panel[row + y * colors - colors + tc];
            h[tc] = i[tc] = panel[row + y * colors + tc];
          } else if ((y == RomWidth - 1) && (x == RomHeight - 1)) {
            a[tc] = panel[x * row - 2 * colors + tc];
            b[tc] = c[tc] = panel[x * row - colors + tc];
            d[tc] = g[tc] = panel[RomHeight * row - 2 * colors + tc];
            e[tc] = f[tc] = h[tc] = i[tc] =
                panel[RomHeight * row - colors + tc];
          } else if (y == 0) {
            a[tc] = b[tc] = panel[(x - 1) * row + tc];
            c[tc] = panel[(x - 1) * row + colors + tc];
            d[tc] = e[tc] = panel[x * row + tc];
            f[tc] = panel[x * row + colors + tc];
            g[tc] = h[tc] = panel[(x + 1) * row + tc];
            i[tc] = panel[(x + 1) * row + colors + tc];
          } else if (y == RomWidth - 1) {
            a[tc] = panel[x * row - 2 * colors + tc];
            b[tc] = c[tc] = panel[x * row - colors + tc];
            d[tc] = panel[(x + 1) * row - 2 * colors + tc];
            e[tc] = f[tc] = panel[(x + 1) * row - colors + tc];
            g[tc] = panel[(x + 2) * row - 2 * colors + tc];
            h[tc] = i[tc] = panel[(x + 2) * row - colors + tc];
          } else if (x == 0) {
            a[tc] = d[tc] = panel[y * colors - colors + tc];
            b[tc] = e[tc] = panel[y * colors + tc];
            c[tc] = f[tc] = panel[y * colors + colors + tc];
            g[tc] = panel[row + y * colors - colors + tc];
            h[tc] = panel[row + y * colors + tc];
            i[tc] = panel[row + y * colors + colors + tc];
          } else if (x == RomHeight - 1) {
            a[tc] = panel[(x - 1) * row + y * colors - colors + tc];
            b[tc] = panel[(x - 1) * row + y * colors + tc];
            c[tc] = panel[(x - 1) * row + y * colors + colors + tc];
            d[tc] = g[tc] = panel[x * row + y * colors - colors + tc];
            e[tc] = h[tc] = panel[x * row + y * colors + tc];
            f[tc] = i[tc] = panel[x * row + y * colors + colors + tc];
          } else {
            a[tc] = panel[(x - 1) * row + y * colors - colors + tc];
            b[tc] = panel[(x - 1) * row + y * colors + tc];
            c[tc] = panel[(x - 1) * row + y * colors + colors + tc];
            d[tc] = panel[x * row + y * colors - colors + tc];
            e[tc] = panel[x * row + y * colors + tc];
            f[tc] = panel[x * row + y * colors + colors + tc];
            g[tc] = panel[(x + 1) * row + y * colors - colors + tc];
            h[tc] = panel[(x + 1) * row + y * colors + tc];
            i[tc] = panel[(x + 1) * row + y * colors + colors + tc];
          }
        }

        if (!CmpColor(b, h, colors) && !CmpColor(d, f, colors)) {
          SetColor(&renderBuffer[(yoffset * TOTAL_WIDTH + x * 2 * TOTAL_WIDTH +
                                  y * 2 + xoffset) *
                                 colors],
                   CmpColor(d, b, colors) ? d : e, colors);
          SetColor(&renderBuffer[(yoffset * TOTAL_WIDTH + x * 2 * TOTAL_WIDTH +
                                  y * 2 + 1 + xoffset) *
                                 colors],
                   CmpColor(b, f, colors) ? f : e, colors);
          SetColor(&renderBuffer[(yoffset * TOTAL_WIDTH +
                                  (x * 2 + 1) * TOTAL_WIDTH + y * 2 + xoffset) *
                                 colors],
                   CmpColor(d, h, colors) ? d : e, colors);
          SetColor(
              &renderBuffer[(yoffset * TOTAL_WIDTH + (x * 2 + 1) * TOTAL_WIDTH +
                             y * 2 + 1 + xoffset) *
                            colors],
              CmpColor(h, f, colors) ? f : e, colors);
        } else {
          SetColor(&renderBuffer[(yoffset * TOTAL_WIDTH + x * 2 * TOTAL_WIDTH +
                                  y * 2 + xoffset) *
                                 colors],
                   e, colors);
          SetColor(&renderBuffer[(yoffset * TOTAL_WIDTH + x * 2 * TOTAL_WIDTH +
                                  y * 2 + 1 + xoffset) *
                                 colors],
                   e, colors);
          SetColor(&renderBuffer[(yoffset * TOTAL_WIDTH +
                                  (x * 2 + 1) * TOTAL_WIDTH + y * 2 + xoffset) *
                                 colors],
                   e, colors);
          SetColor(
              &renderBuffer[(yoffset * TOTAL_WIDTH + (x * 2 + 1) * TOTAL_WIDTH +
                             y * 2 + 1 + xoffset) *
                            colors],
              e, colors);
        }
      }
    }
  } else  // offset!=0
  {
    for (uint16_t y = 0; y < RomHeight; y++) {
      for (uint16_t x = 0; x < RomWidth; x++) {
        for (uint8_t c = 0; c < colors; c++) {
          renderBuffer[((yoffset + y) * TOTAL_WIDTH + xoffset + x) * colors +
                       c] = panel[(y * RomWidth + x) * colors + c];
        }
      }
    }
  }

  free(panel);
}
#endif

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

#ifdef ZEDMD_WIFI
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

bool LoadScreensaverConfig() {
  File ssConfig = LittleFS.open("/screensaver_config.txt", "r");
  if (!ssConfig) return false;

  while (ssConfig.available()) {
    screensaverMode = ssConfig.readStringUntil('\n').toInt();
    enableDimAfterTimeout = ssConfig.readStringUntil('\n').toInt();
    dimTimeout = ssConfig.readStringUntil('\n').toInt();
  }
  ssConfig.close();
  return true;
}

bool SaveScreensaverConfig() {
  File ssConfig = LittleFS.open("/screensaver_config.txt", "w");
  if (!ssConfig) return false;

  ssConfig.println(String(screensaverMode));
  ssConfig.println(String(enableDimAfterTimeout));
  ssConfig.println(String(dimTimeout));
  ssConfig.close();
  return true;
}
#endif

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
    // Serial.println("Failed to open file for reading");
    return;
  }

  renderBuffer = (uint8_t *)malloc(TOTAL_BYTES);
  if (renderBuffer == nullptr) {
    display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
    display->DisplayText("DisplayLogo", 4, 14, 255, 255, 255);
    RestartAfterError();
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

  free(renderBuffer);

  DisplayVersion(true);

  displayStatus = DISPLAY_STATUS_NORMAL_OPERATION;
  MireActive = true;
  displayTimeout = millis();
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

  renderBuffer = (uint8_t *)malloc(TOTAL_BYTES);
  if (renderBuffer == nullptr) {
    display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
    display->DisplayText("DisplayUpdate", 4, 14, 255, 255, 255);
    RestartAfterError();
  }

  for (uint16_t tj = 0; tj < TOTAL_BYTES; tj++) {
    renderBuffer[tj] = f.read();
  }
  f.close();

  display->FillPanelRaw(renderBuffer);

  free(renderBuffer);

  displayStatus = DISPLAY_STATUS_INFO;
  displayTimeout = millis() - (LOGO_TIMEOUT / 2);
}

/// @brief Refreshes screen after color change, needed for webserver
void RefreshSetupScreen() {
  DisplayLogo();
  DisplayRGB();
  DisplayLum();
}

#if !defined(ZEDMD_WIFI)
void ScreenSaver(void) {
  ClearScreen();
  display->SetBrightness(1);
  DisplayVersion();

  displayStatus = DISPLAY_STATUS_SCREEN_SAVER;
}
#else
/// @brief Handles the UDP Packet parsing for ZeDMD WiFi and ZeDMD-HD WiFi
/// @param packet
void IRAM_ATTR HandlePacket(AsyncUDPPacket packet) {
  if (drawingInProgress) return;
  drawingInProgress = true;

  uint8_t *pPacket = packet.data();
  receivedBytes = packet.length();
  if (receivedBytes >= 1) {
    c4 = pPacket[0];
    if (MireActive) {
      MireActive = false;
      ClearScreen();
    }

    displayTimeout = millis();  // Update timer on every packet
                                // received.

    if (displayStatus == DISPLAY_STATUS_DIM ||
        displayStatus == DISPLAY_STATUS_SCREEN_SAVER) {
      display->SetBrightness(lumstep);
      displayStatus = DISPLAY_STATUS_NORMAL_OPERATION;
    }

    switch (c4) {
      case 2:  // set rom frame size
      {
        RomWidth = (int)(pPacket[4]) + (int)(pPacket[5] << 8);
        RomHeight = (int)(pPacket[6]) + (int)(pPacket[7] << 8);
        RomWidthPlane = RomWidth >> 3;
        break;
      }

      case 10:  // clear screen
      {
        ClearScreen();
        displayStatus = DISPLAY_STATUS_CLEAR;
        break;
      }

      case 98:  // disable debug mode
      {
        debugMode = false;
        break;
      }

      case 99:  // enable debug mode
      {
        debugMode = true;
        debugDelayOnError = true;
        break;
      }

      case 4:  // RGB24 Zones Stream
      {
        uint8_t compressed = pPacket[1] & 128;
        uint8_t numZones = pPacket[1] & 127;
        uint16_t size = (int)(pPacket[3]) + (((int)pPacket[2]) << 8);

        renderBuffer = (uint8_t *)malloc(ZONE_SIZE * numZones + numZones);
        if (renderBuffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("HandlePacket", 4, 14, 255, 255, 255);
          RestartAfterError();
        }

        if (compressed == 128) {
          mz_ulong uncompressedBufferSize = ZONE_SIZE * numZones + numZones;
          mz_ulong udpPayloadSize = (mz_ulong)size;

          minizStatus =
              mz_uncompress2(renderBuffer, &uncompressedBufferSize, &pPacket[4],
                             (mz_ulong *)&udpPayloadSize);

          if (minizStatus != MZ_OK ||
              uncompressedBufferSize != (ZONE_SIZE * numZones + numZones)) {
            free(renderBuffer);
            DisplayDebugInfo();
            if (debugDelayOnError) {
              delay(DEBUG_DELAY);
            }
            drawingInProgress = false;
            return;
          }
        } else {
          memcpy(renderBuffer, &pPacket[4], size);
        }

        for (uint8_t idx = 0; idx < numZones; idx++) {
          display->FillZoneRaw(renderBuffer[idx * ZONE_SIZE + idx],
                               &renderBuffer[idx * ZONE_SIZE + idx + 1]);
        }

        free(renderBuffer);
        break;
      }

      case 5:  // RGB565 Zones Stream
      {
        uint8_t compressed = pPacket[1] & 128;
        uint8_t numZones = pPacket[1] & 127;
        uint16_t size = (int)(pPacket[3]) + (((int)pPacket[2]) << 8);

        renderBuffer =
            (uint8_t *)malloc(RGB565_ZONE_SIZE * numZones + numZones);
        if (renderBuffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("HandlePacket", 4, 14, 255, 255, 255);
          RestartAfterError();
        }

        if (compressed == 128) {
          mz_ulong uncompressedBufferSize =
              RGB565_ZONE_SIZE * numZones + numZones;
          mz_ulong udpPayloadSize = (mz_ulong)size;

          minizStatus =
              mz_uncompress2(renderBuffer, &uncompressedBufferSize, &pPacket[4],
                             (mz_ulong *)&udpPayloadSize);

          if (minizStatus != MZ_OK ||
              uncompressedBufferSize !=
                  (RGB565_ZONE_SIZE * numZones + numZones)) {
            free(renderBuffer);
            DisplayDebugInfo();
            if (debugDelayOnError) {
              delay(DEBUG_DELAY);
            }
            drawingInProgress = false;
            return;
          }
        } else {
          memcpy(renderBuffer, &pPacket[4], size);
        }

        for (uint8_t idx = 0; idx < numZones; idx++) {
          display->FillZoneRaw565(
              renderBuffer[idx * RGB565_ZONE_SIZE + idx],
              &renderBuffer[idx * RGB565_ZONE_SIZE + idx + 1]);
        }

        free(renderBuffer);
        break;
      }
    }
  }

  // An overflow of the unsigned int counters should not be an issue, they
  // just reset to 0.
  frameCount++;

  DisplayDebugInfo();

  drawingInProgress = false;
}

/// @brief Handles the mDNS Packets for ZeDMD WiFi, this allows autodiscovery
void RunMDNS() {
  if (!MDNS.begin("ZeDMD-WiFi")) {
    return;
  }
  MDNS.addService("zedmd-wifi", "udp", port);
}

/// @brief Callback method for Verify JPEG Image
/// @param pDraw
/// @return Always return 1
int JPEGDrawNothing(JPEGDRAW *pDraw) {
  return 1;  // Return 1 (success), we are only verifying the image
}

/// @brief Verify JPEG image
bool VerifyImage(const char *filename) {
  File jpegFile = LittleFS.open(filename, "r");
  if (!jpegFile) {
    return false;
  }

  if (!jpeg.open(jpegFile, JPEGDrawNothing)) {
    jpegFile.close();
    return false;
  }

  if (jpeg.getWidth() != TOTAL_WIDTH || jpeg.getHeight() != TOTAL_HEIGHT) {
    jpeg.close();
    jpegFile.close();
    return false;
  }

  jpeg.close();
  jpegFile.close();
  return true;
}

/// @brief Callback method for DisplayImage
/// @param pDraw
/// @return 1 is success, 0 is failure
int JPEGDraw(JPEGDRAW *pDraw) {
  for (int y = 0; y < pDraw->iHeight; y++) {
    uint16_t *pSrc = (uint16_t *)(pDraw->pPixels + (y * pDraw->iWidth));

    uint8_t *pDest = renderBuffer + ((pDraw->y + y) * 128 * 3) + (pDraw->x * 3);

    for (int x = 0; x < pDraw->iWidth; x++) {
      // Extract RGB values from RGB565
      uint16_t color = pSrc[x];
      uint8_t r = (color >> 11) & 0x1F;
      uint8_t g = (color >> 5) & 0x3F;
      uint8_t b = color & 0x1F;

      // Convert 5-bit/6-bit values to 8-bit
      r = (r << 3) | (r >> 2);
      g = (g << 2) | (g >> 4);
      b = (b << 3) | (b >> 2);

      // Store the RGB888 values in the renderBuffer
      pDest[0] = r;
      pDest[1] = g;
      pDest[2] = b;

      // Move to the next pixel in the destination
      pDest += 3;
    }
  }
  return 1;  // Success
}

/// @brief Display JPEG image
bool DisplayImage(const char *filename) {
  bool status = false;
  File jpegFile = LittleFS.open(filename, "r");
  if (!jpegFile) {
    return false;
  }

  renderBuffer = (uint8_t *)malloc(TOTAL_WIDTH * TOTAL_HEIGHT * 3);
  if (renderBuffer == nullptr) {
    display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
    display->DisplayText("DisplayImage", 4, 14, 255, 255, 255);
    RestartAfterError();
  }

  if (!jpeg.open(jpegFile, JPEGDraw)) {
    free(renderBuffer);
    jpegFile.close();
    return false;
  }

  if (jpeg.getWidth() == TOTAL_WIDTH && jpeg.getHeight() == TOTAL_HEIGHT &&
      jpeg.decode(0, 0, 0) == 1) {
    display->FillPanelRaw(renderBuffer);
    status = true;
  }

  jpeg.close();
  jpegFile.close();
  free(renderBuffer);

  return status;
}

/// @brief Screensaver method for ZeDMD-WiFi
/// @param
void ScreenSaver(void) {
  if (screensaverMode == SCREENSAVER_MODE_CLEAR_SCREEN) {
    ClearScreen();
    display->SetBrightness(1);
    DisplayVersion();
  } else if (screensaverMode == SCREENSAVER_MODE_SHOW_IMAGE) {
    ClearScreen();
    DisplayImage("/screensaver.jpg");
  }
  displayStatus = DISPLAY_STATUS_SCREEN_SAVER;
  displayTimeout = millis();
}
#endif

void setup() {
  esp_task_wdt_deinit();

  rgbOrderButton = new Bounce2::Button();
  rgbOrderButton->attach(RGB_ORDER_BUTTON_PIN, INPUT_PULLUP);
  rgbOrderButton->interval(100);
  rgbOrderButton->setPressedState(LOW);

  brightnessButton = new Bounce2::Button();
  brightnessButton->attach(BRIGHTNESS_BUTTON_PIN, INPUT_PULLUP);
  brightnessButton->interval(100);
  brightnessButton->setPressedState(LOW);

  bool fileSystemOK;
  if (fileSystemOK = LittleFS.begin()) {
    LoadRgbOrder();
    LoadLum();
    LoadScale();
  }

#ifdef DISPLAY_RM67162_AMOLED
  display = new Rm67162Amoled();  // For AMOLED display
#elif defined(DISPLAY_LED_MATRIX)
  display = new LedMatrix();  // For LED matrix display
#endif

  if (!fileSystemOK) {
    display->DisplayText("Error reading file system!", 4, 6, 255, 255, 255);
    display->DisplayText("Try to flash the firmware again.", 4, 14, 255, 255,
                         255);
    // #ifdef ARDUINO_ESP32_S3_N16R8
    //     display->flipDMABuffer();
    // #endif
    while (true);
  }

  DisplayLogo();

#ifdef ZEDMD_WIFI
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
    runWebServer();                   // Start web server for AP clients
  }

#if !defined(ZEDMD_HD) || defined(ARDUINO_ESP32_S3_N16R8) || \
    defined(DISPLAY_RM67162_AMOLED)
  runWebServer();  // Start the web server
  RunMDNS();       // Start the MDNS server for easy detection
#endif

  IPAddress ip;
  if (WiFi.getMode() == WIFI_AP) {
    ip = WiFi.softAPIP();
  } else if (WiFi.getMode() == WIFI_STA) {
    ip = WiFi.localIP();
  }

  for (uint8_t i = 0; i < 4; i++) {
    DisplayNumber(ip[i], 3, i * 3 * 4 + i, 0, 200, 200, 200);
  }

  if (udp.listen(ip, port)) {
    udp.onPacket(HandlePacket);  // Start listening to ZeDMD UDP traffic
  }

  LoadScreensaverConfig();  // Load Screensaver config, this should be moved to
                            // all build configs but we will leave it in WiFi
                            // only for now.
#else
  Serial.setRxBufferSize(SERIAL_BUFFER);
#if !defined(ARDUINO_ESP32_S3_N16R8) || !defined(DISPLAY_RM67162_AMOLED)
  Serial.setTimeout(SERIAL_TIMEOUT);
#endif
  Serial.begin(SERIAL_BAUD);
  while (!Serial);
#endif
}

#if !defined(ZEDMD_WIFI)
bool SerialReadBuffer(uint8_t *pBuffer, uint16_t BufferSize,
                      bool fixedSize = true) {
  memset(pBuffer, 0, BufferSize);

  transferBufferSize = BufferSize;
  uint8_t *transferBuffer;

  if (compression) {
    uint8_t byteArray[2];
    Serial.readBytes(byteArray, 2);
    transferBufferSize =
        ((((uint16_t)byteArray[0]) << 8) + ((uint16_t)byteArray[1]));

    transferBuffer = (uint8_t *)malloc(transferBufferSize);
    if (transferBuffer == nullptr) {
      display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
      display->DisplayText("SerialReadBuffer", 4, 14, 255, 255, 255);
      RestartAfterError();
    }
  } else {
    transferBuffer = pBuffer;
  }

  // We always receive chunks of "serialTransferChunkSize" bytes (maximum).
  // At this point, the control chars and the one byte command have been read
  // already. So we only need to read the remaining bytes of the first chunk and
  // full chunks afterwards.
  uint16_t chunkSize =
      serialTransferChunkSize - N_CTRL_CHARS - 1 - (compression ? 2 : 0);
  uint16_t remainingBytes = transferBufferSize;
  while (remainingBytes > 0) {
    receivedBytes = Serial.readBytes(
        transferBuffer + transferBufferSize - remainingBytes,
        (remainingBytes > chunkSize) ? chunkSize : remainingBytes);

    DisplayDebugInfo();

    if (receivedBytes != remainingBytes && receivedBytes != chunkSize) {
      errorCount++;
      DisplayDebugInfo();
      if (debugDelayOnError) {
        delay(DEBUG_DELAY);
      }

      // Send an (E)rror signal to tell the client that no more chunks should be
      // send or to repeat the entire frame from the beginning.
      Serial.write('E');

      return false;
    }

    // Send an (A)cknowledge signal to tell the client that we successfully read
    // the chunk.
    Serial.write('A');

    DisplayDebugInfo();

    remainingBytes -= receivedBytes;

    // From now on read full amount of byte chunks.
    chunkSize = serialTransferChunkSize;
  }

  if (compression) {
    mz_ulong uncompressed_buffer_size = (mz_ulong)BufferSize;
    minizStatus =
        mz_uncompress2(pBuffer, &uncompressed_buffer_size, transferBuffer,
                       (mz_ulong *)&transferBufferSize);
    free(transferBuffer);

    if ((MZ_OK == minizStatus) &&
        (!fixedSize || (fixedSize && uncompressed_buffer_size == BufferSize))) {
      return true;
    }

    if (debugMode && (MZ_OK == minizStatus)) {
      // uncrompessed data isn't of expected size
      minizStatus = 99;
    }

    errorCount++;
    DisplayDebugInfo();
    if (debugDelayOnError) {
      delay(DEBUG_DELAY);
    }

    Serial.write('E');
    return false;
  }

  return true;
}

void UpdateColorRotations(void) {
  bool rotPaletteAffected[64] = {0};
  unsigned long actime = millis();
  bool rotfound = false;
  for (uint8_t ti = 0; ti < MAX_COLOR_ROTATIONS; ti++) {
    if (rotFirstColor[ti] == 255) continue;

    if (actime >= rotNextRotationTime[ti]) {
      memcpy(tmpColor, &palette[rotFirstColor[ti] * 3], 3);
      memmove(&palette[rotFirstColor[ti] * 3],
              &palette[(rotFirstColor[ti] + 1) * 3],
              (rotAmountColors[ti] - 1) * 3);
      memcpy(&palette[(rotFirstColor[ti] + rotAmountColors[ti] - 1) * 3],
             tmpColor, 3);
      for (uint8_t tj = rotFirstColor[ti];
           tj < (rotFirstColor[ti] + rotAmountColors[ti]); tj++) {
        rotPaletteAffected[tj] = true;
      }

      rotfound = true;
      rotNextRotationTime[ti] += rotStepDurationTime[ti];
    }
  }

  if (rotfound == true)
    display->FillPanelUsingChangedPalette(renderBuffer, palette,
                                          rotPaletteAffected);
}

bool WaitForCtrlChars(void) {
  unsigned long ms = millis();
  uint8_t nCtrlCharFound = 0;

  while (nCtrlCharFound < N_CTRL_CHARS) {
    if (Serial.available()) {
      if (Serial.read() != CtrlCharacters[nCtrlCharFound++]) {
        nCtrlCharFound = 0;
        // There's garbage on the line.
        minizStatus = 666;
        DisplayDebugInfo();
      }
    }

    if (displayStatus == DISPLAY_STATUS_NORMAL_OPERATION && mode64 &&
        nCtrlCharFound == 0) {
      // While waiting for the next frame, perform in-frame color rotations.
      UpdateColorRotations();
    } else if (displayStatus == DISPLAY_STATUS_CLEAR &&
               (millis() - displayTimeout) > LOGO_TIMEOUT) {
      ScreenSaver();
    }

    if (flowControlCounter > 0 && handshakeSucceeded &&
        ((millis() - ms) > FLOW_CONTROL_TIMEOUT)) {
      Serial.write(flowControlCounter);
      ms = millis();
    }
  }

  if (flowControlCounter > 0) {
    if (++flowControlCounter > 32) {
      flowControlCounter = 1;
    }
  }

  return true;
}
#endif

void loop() {
  while (MireActive) {
    rgbOrderButton->update();
    if (rgbOrderButton->pressed()) {
      if (rgbModeLoaded != 0) {
        rgbMode = 0;
        SaveRgbOrder();
        Restart();
      }

      if (displayStatus != DISPLAY_STATUS_NORMAL_OPERATION) {
        RefreshSetupScreen();
        continue;
      }

      displayTimeout = millis();
      rgbMode++;
      if (rgbMode > 5) rgbMode = 0;
      SaveRgbOrder();
      RefreshSetupScreen();
    }

    brightnessButton->update();
    if (brightnessButton->pressed()) {
      if (displayStatus != DISPLAY_STATUS_NORMAL_OPERATION) {
        RefreshSetupScreen();
        continue;
      }

      displayTimeout = millis();
      lumstep++;
      if (lumstep >= 16) lumstep = 1;
      display->SetBrightness(lumstep);
      SaveLum();
      DisplayRGB();
      DisplayLum();
    }

#if !defined(ZEDMD_WIFI)
    if (Serial.available() > 0) {
      if (rgbMode != rgbModeLoaded) {
        Restart();
      }
      ClearScreen();
      MireActive = false;
    } else
#endif
        if ((millis() - displayTimeout) > LOGO_TIMEOUT) {
      if (displayStatus == DISPLAY_STATUS_NORMAL_OPERATION) {
#if !defined(ZEDMD_WIFI)
        if (rgbMode != rgbModeLoaded) {
          Restart();
        }
#endif
        DisplayUpdate();
      } else if (displayStatus != DISPLAY_STATUS_SCREEN_SAVER &&
                 displayStatus != DISPLAY_STATUS_DIM) {
        ScreenSaver();
      }
#ifdef ZEDMD_WIFI
      else if (enableDimAfterTimeout &&
               displayStatus == DISPLAY_STATUS_SCREEN_SAVER &&
               screensaverMode == SCREENSAVER_MODE_SHOW_IMAGE) {
        if ((millis() - displayTimeout) > dimTimeout) {
          displayStatus = DISPLAY_STATUS_DIM;
          display->SetBrightness(1);
        }
      }
#endif
    }
  }

#if !defined(ZEDMD_WIFI)
  // After handshake, send a (R)eady signal to indicate that a new command could
  // be sent. The client has to wait for it to avoid buffer issues. The
  // handshake itself works without it.
  // The new flow control replaces the Ready signal by a counter.
  if (handshakeSucceeded) {
    if (flowControlCounter == 0) {
      Serial.write('R');
    } else {
      Serial.write(flowControlCounter);
    }
  }

  if (WaitForCtrlChars()) {
    // Updates to mode64 color rotations have been handled within
    // WaitForCtrlChars(), now reset it to false.
    mode64 = false;

    while (Serial.available() == 0);
    c4 = Serial.read();

    if (displayStatus != DISPLAY_STATUS_NORMAL_OPERATION) {
      // Exit screen saver.
      ClearScreen();
      displayStatus = DISPLAY_STATUS_NORMAL_OPERATION;
    }

    switch (c4) {
      case 12:  // ask for resolution (and shake hands)
      {
        for (int i = 0; i < N_INTERMEDIATE_CTR_CHARS; i++) {
          Serial.write(CtrlCharacters[i]);
        }
        Serial.write(TOTAL_WIDTH & 0xff);
        Serial.write((TOTAL_WIDTH >> 8) & 0xff);
        Serial.write(TOTAL_HEIGHT & 0xff);
        Serial.write((TOTAL_HEIGHT >> 8) & 0xff);
        handshakeSucceeded = true;
        break;
      }

      case 2:  // set rom frame size
      {
        uint8_t tbuf[4];
        if (SerialReadBuffer(tbuf, 4)) {
          RomWidth = (int)(tbuf[0]) + (int)(tbuf[1] << 8);
          RomHeight = (int)(tbuf[2]) + (int)(tbuf[3] << 8);
          RomWidthPlane = RomWidth >> 3;
        }
        break;
      }

      case 13:  // set serial transfer chunk size
      {
        while (Serial.available() == 0);
        int tmpSerialTransferChunkSize = ((int)Serial.read()) * 32;
        if (tmpSerialTransferChunkSize <= SERIAL_CHUNK_SIZE_MAX) {
          serialTransferChunkSize = tmpSerialTransferChunkSize;
          // Send an (A)cknowledge signal to tell the client that we
          // successfully read the chunk.
          Serial.write('A');
        } else {
          display->DisplayText("Unsupported chunk size:", 0, 0, 255, 0, 0);
          DisplayNumber(tmpSerialTransferChunkSize, 5, 24 * 4, 0, 255, 0, 0);
          delay(5000);

          Serial.write('E');
        }
        break;
      }

      case 14:  // enable serial transfer compression
      {
        compression = true;
        Serial.write('A');
        break;
      }

      case 15:  // disable serial transfer compression
      {
        compression = false;
        Serial.write('A');
        break;
      }

      case 16: {
        Serial.write('A');
        LedTester();
        break;
      }

      case 20:  // turn off upscaling
      {
        upscaling = false;
        Serial.write('A');
        break;
      }

      case 21:  // turn on upscaling
        upscaling = true;
        Serial.write('A');
        break;

      case 22:  // set brightness
      {
        uint8_t tbuf[1];
        if (SerialReadBuffer(tbuf, 1)) {
          if (tbuf[0] > 0 && tbuf[0] < 16) {
            lumstep = tbuf[0];
            display->SetBrightness(lumstep);
            Serial.write('A');
          } else {
            Serial.write('E');
          }
        }
        break;
      }

      case 23:  // set RGB order
      {
        uint8_t tbuf[1];
        if (SerialReadBuffer(tbuf, 1)) {
          if (tbuf[0] >= 0 && tbuf[0] < 6) {
            rgbMode = tbuf[0];
            Serial.write('A');
          } else {
            Serial.write('E');
          }
        }
        break;
      }

      case 24:  // get brightness
      {
        Serial.write(lumstep);
        break;
      }

      case 25:  // get RGB order
      {
        Serial.write(rgbMode);
        break;
      }

      case 26:  // turn on flow control version 2
      {
        flowControlCounter = 1;
        Serial.write('A');
        break;
      }

      case 30:  // save settings
      {
        Serial.write('A');
        SaveLum();
        SaveRgbOrder();
        break;
      }

      case 31:  // reset
      {
        Restart();
        break;
      }

      case 32:  // get version
      {
        Serial.write(ZEDMD_VERSION_MAJOR);
        Serial.write(ZEDMD_VERSION_MINOR);
        Serial.write(ZEDMD_VERSION_PATCH);
        break;
      }

      case 33:  // get panel resolution
      {
        Serial.write(TOTAL_WIDTH & 0xff);
        Serial.write((TOTAL_WIDTH >> 8) & 0xff);
        Serial.write(TOTAL_HEIGHT & 0xff);
        Serial.write((TOTAL_HEIGHT >> 8) & 0xff);
        break;
      }

      case 98:  // disable debug mode
      {
        debugMode = false;
        Serial.write('A');
        break;
      }

      case 99:  // enable debug mode
      {
        debugMode = true;
        debugDelayOnError = true;
        Serial.write('A');
        break;
      }

      case 6:  // reinit palette (deprecated)
      {
        // Just backward compatibility. We don't need that command anymore.
        Serial.write('A');
        break;
      }

      case 10:  // clear screen
      {
        Serial.write('A');
        ClearScreen();
        displayStatus = DISPLAY_STATUS_CLEAR;
        displayTimeout = millis();
        break;
      }

      case 4:  // mode RGB24 zones streaming
      {
        renderBuffer =
            (uint8_t *)malloc(TOTAL_ZONES * ZONE_SIZE + ZONES_PER_ROW);
        if (renderBuffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("Command 4", 4, 14, 255, 255, 255);
          RestartAfterError();
        }
        if (SerialReadBuffer(renderBuffer,
                             TOTAL_ZONES * ZONE_SIZE + ZONES_PER_ROW, false)) {
          uint16_t idx = 0;
          // SerialReadBuffer prefills buffer with zeros. That will fill Zone 0
          // black if buffer is not used entirely. Ensure that Zone 0 is only
          // allowed at the beginning of the buffer.
          while (idx <= ((TOTAL_ZONES * ZONE_SIZE + ZONES_PER_ROW) -
                         (ZONE_SIZE + 1)) &&
                 (renderBuffer[idx] < TOTAL_ZONES) &&
                 (idx == 0 || renderBuffer[idx] > 0)) {
            display->FillZoneRaw(renderBuffer[idx], &renderBuffer[++idx]);
            idx += ZONE_SIZE;
          }
        }

        free(renderBuffer);
        break;
      }

      case 5:  // mode RGB565 zones streaming
      {
        renderBuffer =
            (uint8_t *)malloc(TOTAL_ZONES * RGB565_ZONE_SIZE + ZONES_PER_ROW);
        if (renderBuffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("Command 5", 4, 14, 255, 255, 255);
          RestartAfterError();
        }
        if (SerialReadBuffer(renderBuffer,
                             TOTAL_ZONES * RGB565_ZONE_SIZE + ZONES_PER_ROW,
                             false)) {
          uint16_t idx = 0;
          // SerialReadBuffer prefills buffer with zeros. That will fill Zone 0
          // black if buffer is not used entirely. Ensure that Zone 0 is only
          // allowed at the beginning of the buffer.
          while (idx <= ((TOTAL_ZONES * RGB565_ZONE_SIZE + ZONES_PER_ROW) -
                         (ZONE_SIZE + 1)) &&
                 (renderBuffer[idx] < TOTAL_ZONES) &&
                 (idx == 0 || renderBuffer[idx] > 0)) {
            display->FillZoneRaw565(renderBuffer[idx], &renderBuffer[++idx]);
            idx += RGB565_ZONE_SIZE;
          }
        }

        free(renderBuffer);
        break;
      }

      case 3:  // mode RGB24
      {
        // We need to cover downscaling, too.
        uint16_t renderBufferSize =
            (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                ? TOTAL_BYTES
                : RomWidth * RomHeight * 3;
        renderBuffer = (uint8_t *)malloc(renderBufferSize);
        if (renderBuffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("Command 3", 4, 14, 255, 255, 255);
          RestartAfterError();
        }

        if (SerialReadBuffer(renderBuffer, RomHeight * RomWidth * 3)) {
          mode64 = false;
          ScaleImage(3);
          display->FillPanelRaw(renderBuffer);
        }

        free(renderBuffer);
        break;
      }

      case 8:  // mode 4 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis
               // de 4 pixels par byte
      {
        uint16_t bufferSize = 12 + 2 * RomWidthPlane * RomHeight;
        uint8_t *buffer = (uint8_t *)malloc(bufferSize);
        if (buffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("Command 8", 4, 14, 255, 255, 255);
          RestartAfterError();
        }

        if (SerialReadBuffer(buffer, bufferSize)) {
          // We need to cover downscaling, too.
          uint16_t renderBufferSize =
              (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                  ? TOTAL_WIDTH * TOTAL_HEIGHT
                  : RomWidth * RomHeight;
          renderBuffer = (uint8_t *)malloc(renderBufferSize);
          if (renderBuffer == nullptr) {
            display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
            display->DisplayText("Command 8", 4, 14, 255, 255, 255);
            RestartAfterError();
          }
          memset(renderBuffer, 0, renderBufferSize);
          palette = (uint8_t *)malloc(12);
          if (palette == nullptr) {
            display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
            display->DisplayText("Command 8", 4, 14, 255, 255, 255);
            RestartAfterError();
          }
          memcpy(palette, buffer, 12);

          uint8_t *frame = &buffer[12];
          for (uint16_t tj = 0; tj < RomHeight; tj++) {
            for (uint16_t ti = 0; ti < RomWidthPlane; ti++) {
              uint8_t mask = 1;
              uint8_t planes[2];
              planes[0] = frame[ti + tj * RomWidthPlane];
              planes[1] =
                  frame[RomWidthPlane * RomHeight + ti + tj * RomWidthPlane];
              for (int tk = 0; tk < 8; tk++) {
                uint8_t idx = 0;
                if ((planes[0] & mask) > 0) idx |= 1;
                if ((planes[1] & mask) > 0) idx |= 2;
                renderBuffer[(ti * 8 + tk) + tj * RomWidth] = idx;
                mask <<= 1;
              }
            }
          }
          free(buffer);

          mode64 = false;

          ScaleImage(1);
          display->FillPanelUsingPalette(renderBuffer, palette);

          free(renderBuffer);
          free(palette);
        } else {
          free(buffer);
        }
        break;
      }

      case 7:  // mode 16 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis
               // de 2 pixels par byte
      {
        uint16_t bufferSize = 12 + 4 * RomWidthPlane * RomHeight;
        uint8_t *buffer = (uint8_t *)malloc(bufferSize);
        if (buffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("Command 7", 4, 14, 255, 255, 255);
          RestartAfterError();
        }

        if (SerialReadBuffer(buffer, bufferSize)) {
          // We need to cover downscaling, too.
          uint16_t renderBufferSize =
              (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                  ? TOTAL_WIDTH * TOTAL_HEIGHT
                  : RomWidth * RomHeight;
          renderBuffer = (uint8_t *)malloc(renderBufferSize);
          if (renderBuffer == nullptr) {
            display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
            display->DisplayText("Command 7", 4, 14, 255, 255, 255);
            RestartAfterError();
          }
          memset(renderBuffer, 0, renderBufferSize);
          palette = (uint8_t *)malloc(48);
          if (palette == nullptr) {
            display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
            display->DisplayText("Command 7", 4, 14, 255, 255, 255);
            RestartAfterError();
          }
          memcpy(palette, buffer, 48);

          palette[0] = palette[1] = palette[2] = 0;
          palette[3] = palette[3 * 3] / 3;
          palette[4] = palette[3 * 3 + 1] / 3;
          palette[5] = palette[3 * 3 + 2] / 3;
          palette[6] = 2 * (palette[3 * 3] / 3);
          palette[7] = 2 * (palette[3 * 3 + 1] / 3);
          palette[8] = 2 * (palette[3 * 3 + 2] / 3);

          palette[12] = palette[3 * 3] + (palette[7 * 3] - palette[3 * 3]) / 4;
          palette[13] = palette[3 * 3 + 1] +
                        (palette[7 * 3 + 1] - palette[3 * 3 + 1]) / 4;
          palette[14] = palette[3 * 3 + 2] +
                        (palette[7 * 3 + 2] - palette[3 * 3 + 2]) / 4;
          palette[15] =
              palette[3 * 3] + 2 * ((palette[7 * 3] - palette[3 * 3]) / 4);
          palette[16] = palette[3 * 3 + 1] +
                        2 * ((palette[7 * 3 + 1] - palette[3 * 3 + 1]) / 4);
          palette[17] = palette[3 * 3 + 2] +
                        2 * ((palette[7 * 3 + 2] - palette[3 * 3 + 2]) / 4);
          palette[18] =
              palette[3 * 3] + 3 * ((palette[7 * 3] - palette[3 * 3]) / 4);
          palette[19] = palette[3 * 3 + 1] +
                        3 * ((palette[7 * 3 + 1] - palette[3 * 3 + 1]) / 4);
          palette[20] = palette[3 * 3 + 2] +
                        3 * ((palette[7 * 3 + 2] - palette[3 * 3 + 2]) / 4);

          palette[24] = palette[7 * 3] + (palette[11 * 3] - palette[7 * 3]) / 4;
          palette[25] = palette[7 * 3 + 1] +
                        (palette[11 * 3 + 1] - palette[7 * 3 + 1]) / 4;
          palette[26] = palette[7 * 3 + 2] +
                        (palette[11 * 3 + 2] - palette[7 * 3 + 2]) / 4;
          palette[27] =
              palette[7 * 3] + 2 * ((palette[11 * 3] - palette[7 * 3]) / 4);
          palette[28] = palette[7 * 3 + 1] +
                        2 * ((palette[11 * 3 + 1] - palette[7 * 3 + 1]) / 4);
          palette[29] = palette[7 * 3 + 2] +
                        2 * ((palette[11 * 3 + 2] - palette[7 * 3 + 2]) / 4);
          palette[30] =
              palette[7 * 3] + 3 * ((palette[11 * 3] - palette[7 * 3]) / 4);
          palette[31] = palette[7 * 3 + 1] +
                        3 * ((palette[11 * 3 + 1] - palette[7 * 3 + 1]) / 4);
          palette[32] = palette[7 * 3 + 2] +
                        3 * ((palette[11 * 3 + 2] - palette[7 * 3 + 2]) / 4);

          palette[36] =
              palette[11 * 3] + (palette[15 * 3] - palette[11 * 3]) / 4;
          palette[37] = palette[11 * 3 + 1] +
                        (palette[15 * 3 + 1] - palette[11 * 3 + 1]) / 4;
          palette[38] = palette[11 * 3 + 2] +
                        (palette[15 * 3 + 2] - palette[11 * 3 + 2]) / 4;
          palette[39] =
              palette[11 * 3] + 2 * ((palette[15 * 3] - palette[11 * 3]) / 4);
          palette[40] = palette[11 * 3 + 1] +
                        2 * ((palette[15 * 3 + 1] - palette[11 * 3 + 1]) / 4);
          palette[41] = palette[11 * 3 + 2] +
                        2 * ((palette[15 * 3 + 2] - palette[11 * 3 + 2]) / 4);
          palette[42] =
              palette[11 * 3] + 3 * ((palette[15 * 3] - palette[11 * 3]) / 4);
          palette[43] = palette[11 * 3 + 1] +
                        3 * ((palette[15 * 3 + 1] - palette[11 * 3 + 1]) / 4);
          palette[44] = palette[11 * 3 + 2] +
                        3 * ((palette[15 * 3 + 2] - palette[11 * 3 + 2]) / 4);

          uint8_t *pBuffer = &buffer[12];
          for (uint16_t tj = 0; tj < RomHeight; tj++) {
            for (uint16_t ti = 0; ti < RomWidthPlane; ti++) {
              uint8_t mask = 1;
              uint8_t planes[4];
              planes[0] = pBuffer[ti + tj * RomWidthPlane];
              planes[1] =
                  pBuffer[RomWidthPlane * RomHeight + ti + tj * RomWidthPlane];
              planes[2] = pBuffer[2 * RomWidthPlane * RomHeight + ti +
                                  tj * RomWidthPlane];
              planes[3] = pBuffer[3 * RomWidthPlane * RomHeight + ti +
                                  tj * RomWidthPlane];
              for (uint8_t tk = 0; tk < 8; tk++) {
                uint8_t idx = 0;
                if ((planes[0] & mask) > 0) idx |= 1;
                if ((planes[1] & mask) > 0) idx |= 2;
                if ((planes[2] & mask) > 0) idx |= 4;
                if ((planes[3] & mask) > 0) idx |= 8;
                renderBuffer[(ti * 8 + tk) + tj * RomWidth] = idx;
                mask <<= 1;
              }
            }
          }
          free(buffer);

          mode64 = false;

          ScaleImage(1);
          display->FillPanelUsingPalette(renderBuffer, palette);

          free(renderBuffer);
          free(palette);
        } else {
          free(buffer);
        }
        break;
      }

      case 9:  // mode 16 couleurs avec 1 palette 16 couleurs (16*3 bytes)
               // suivis de 4 bytes par groupe de 8 points (séparés en plans de
               // bits 4*512 bytes)
      {
        uint16_t bufferSize = 48 + 4 * RomWidthPlane * RomHeight;
        uint8_t *buffer = (uint8_t *)malloc(bufferSize);
        if (buffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("Command 9", 4, 14, 255, 255, 255);
          RestartAfterError();
        }

        if (SerialReadBuffer(buffer, bufferSize)) {
          // We need to cover downscaling, too.
          uint16_t renderBufferSize =
              (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                  ? TOTAL_WIDTH * TOTAL_HEIGHT
                  : RomWidth * RomHeight;
          renderBuffer = (uint8_t *)malloc(renderBufferSize);
          if (renderBuffer == nullptr) {
            display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
            display->DisplayText("Command 9", 4, 14, 255, 255, 255);
            RestartAfterError();
          }
          memset(renderBuffer, 0, renderBufferSize);
          palette = (uint8_t *)malloc(48);
          if (palette == nullptr) {
            display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
            display->DisplayText("Command 9", 4, 14, 255, 255, 255);
            RestartAfterError();
          }
          memcpy(palette, buffer, 48);

          uint8_t *pBuffer = &buffer[48];
          for (uint16_t tj = 0; tj < RomHeight; tj++) {
            for (uint16_t ti = 0; ti < RomWidthPlane; ti++) {
              // on reconstitue un indice à partir des plans puis une couleur à
              // partir de la palette
              uint8_t mask = 1;
              uint8_t planes[4];
              planes[0] = pBuffer[ti + tj * RomWidthPlane];
              planes[1] =
                  pBuffer[RomWidthPlane * RomHeight + ti + tj * RomWidthPlane];
              planes[2] = pBuffer[2 * RomWidthPlane * RomHeight + ti +
                                  tj * RomWidthPlane];
              planes[3] = pBuffer[3 * RomWidthPlane * RomHeight + ti +
                                  tj * RomWidthPlane];
              for (uint8_t tk = 0; tk < 8; tk++) {
                uint8_t idx = 0;
                if ((planes[0] & mask) > 0) idx |= 1;
                if ((planes[1] & mask) > 0) idx |= 2;
                if ((planes[2] & mask) > 0) idx |= 4;
                if ((planes[3] & mask) > 0) idx |= 8;
                renderBuffer[(ti * 8 + tk) + tj * RomWidth] = idx;
                mask <<= 1;
              }
            }
          }
          free(buffer);

          mode64 = false;

          ScaleImage(1);
          display->FillPanelUsingPalette(renderBuffer, palette);

          free(renderBuffer);
          free(palette);
        } else {
          free(buffer);
        }
        break;
      }

      case 11:  // mode 64 couleurs avec 1 palette 64 couleurs (64*3 bytes)
                // suivis de 6 bytes par groupe de 8 points (séparés en plans de
                // bits 6*512 bytes) suivis de 3*8 bytes de rotations de
                // couleurs
      {
        uint16_t bufferSize =
            192 + 6 * RomWidthPlane * RomHeight + 3 * MAX_COLOR_ROTATIONS;
        uint8_t *buffer = (uint8_t *)malloc(bufferSize);
        if (buffer == nullptr) {
          display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
          display->DisplayText("Command 11", 4, 14, 255, 255, 255);
          RestartAfterError();
        }

        if (SerialReadBuffer(buffer, bufferSize)) {
          // We need to cover downscaling, too.
          uint16_t renderBufferSize =
              (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                  ? TOTAL_WIDTH * TOTAL_HEIGHT
                  : RomWidth * RomHeight;
          renderBuffer = (uint8_t *)malloc(renderBufferSize);
          if (renderBuffer == nullptr) {
            display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
            display->DisplayText("Command 11", 4, 14, 255, 255, 255);
            RestartAfterError();
          }
          memset(renderBuffer, 0, renderBufferSize);
          palette = (uint8_t *)malloc(192);
          if (palette == nullptr) {
            display->DisplayText("Error, out of memory:", 4, 6, 255, 255, 255);
            display->DisplayText("Command 11", 4, 14, 255, 255, 255);
            RestartAfterError();
          }
          memcpy(palette, buffer, 192);

          uint8_t *pBuffer = &buffer[192];
          for (uint16_t tj = 0; tj < RomHeight; tj++) {
            for (uint16_t ti = 0; ti < RomWidthPlane; ti++) {
              // on reconstitue un indice à partir des plans puis une couleur à
              // partir de la palette
              uint8_t mask = 1;
              uint8_t planes[6];
              planes[0] = pBuffer[ti + tj * RomWidthPlane];
              planes[1] =
                  pBuffer[RomWidthPlane * RomHeight + ti + tj * RomWidthPlane];
              planes[2] = pBuffer[2 * RomWidthPlane * RomHeight + ti +
                                  tj * RomWidthPlane];
              planes[3] = pBuffer[3 * RomWidthPlane * RomHeight + ti +
                                  tj * RomWidthPlane];
              planes[4] = pBuffer[4 * RomWidthPlane * RomHeight + ti +
                                  tj * RomWidthPlane];
              planes[5] = pBuffer[5 * RomWidthPlane * RomHeight + ti +
                                  tj * RomWidthPlane];
              for (uint8_t tk = 0; tk < 8; tk++) {
                uint8_t idx = 0;
                if ((planes[0] & mask) > 0) idx |= 1;
                if ((planes[1] & mask) > 0) idx |= 2;
                if ((planes[2] & mask) > 0) idx |= 4;
                if ((planes[3] & mask) > 0) idx |= 8;
                if ((planes[4] & mask) > 0) idx |= 0x10;
                if ((planes[5] & mask) > 0) idx |= 0x20;
                renderBuffer[(ti * 8 + tk) + tj * RomWidth] = idx;
                mask <<= 1;
              }
            }
          }

          // Handle up to 8 different rotations for each frame.
          // first byte: first color in the rotation
          // second byte: number of contiguous colors in the rotation
          // third byte: delay between 2 rotations (5 -> 50ms, 12 -> 120ms)
          pBuffer = &buffer[192 + 6 * RomWidthPlane * RomHeight];
          unsigned long actime = millis();

          for (int ti = 0; ti < MAX_COLOR_ROTATIONS; ti++) {
            rotFirstColor[ti] = pBuffer[ti * 3];
            rotAmountColors[ti] = pBuffer[ti * 3 + 1];
            rotStepDurationTime[ti] = 10 * pBuffer[ti * 3 + 2];
            rotNextRotationTime[ti] = actime + rotStepDurationTime[ti];
          }

          free(buffer);

          mode64 = true;

          ScaleImage(1);
          display->FillPanelUsingPalette(renderBuffer, palette);

          free(renderBuffer);
          free(palette);
        } else {
          free(buffer);
        }
        break;
      }
      default: {
        display->DisplayText("Unsupported render mode:", 0, 0, 255, 0, 0);
        DisplayNumber(c4, 3, 24 * 4, 0, 255, 0, 0);
        delay(5000);
        Serial.write('E');
      }
    }

    // An overflow of the unsigned int counters should not be an issue, they
    // just reset to 0.
    frameCount++;

    DisplayDebugInfo();
  }
#endif
}
