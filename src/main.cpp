#define ZEDMD_VERSION_MAJOR 3  // X Digits
#define ZEDMD_VERSION_MINOR 6  // Max 2 Digits
#define ZEDMD_VERSION_PATCH 1  // Max 2 Digits

#ifdef ZEDMD_HD
#define PANEL_WIDTH 128  // Width: number of LEDs for 1 panel.
#define PANEL_HEIGHT 64  // Height: number of LEDs.
#define PANELS_NUMBER 2  // Number of horizontally chained panels.
#elif ZEDMD_HD_HALF
#define PANEL_WIDTH 128  // Width: number of LEDs for 1 panel.
#define PANEL_HEIGHT 64  // Height: number of LEDs.
#define PANELS_NUMBER 1  // Number of horizontally chained panels.
#endif
#ifndef PANEL_WIDTH
#define PANEL_WIDTH 64   // Width: number of LEDs for 1 panel.
#define PANEL_HEIGHT 32  // Height: number of LEDs.
#define PANELS_NUMBER 2  // Number of horizontally chained panels.
#endif

#ifdef ARDUINO_ESP32_S3_N16R8
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
#define BUFFER_OVERHEAD 4
#define R 0
#define G 1
#define B 2
#define DISPLAY_STATUS_SCREEN_SAVER 0      // screen saver
#define DISPLAY_STATUS_NORMAL_OPERATION 1  // normal operation mode
#define DISPLAY_STATUS_INFO 2              // PPUC info screen
#define DISPLAY_STATUS_CLEAR 3             // clear screen (command received)

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

#define TOTAL_WIDTH (PANEL_WIDTH * PANELS_NUMBER)
#define TOTAL_WIDTH_PLANE (TOTAL_WIDTH >> 3)
#ifdef ZEDMD_HD_HALF
#define TOTAL_HEIGHT (PANEL_HEIGHT / 2)
#endif
#ifndef TOTAL_HEIGHT
#define TOTAL_HEIGHT PANEL_HEIGHT
#endif
#define TOTAL_BYTES (TOTAL_WIDTH * TOTAL_HEIGHT * 3)
#define ZONE_WIDTH (TOTAL_WIDTH / 16)
#define ZONE_HEIGHT (TOTAL_HEIGHT / 8)
#define ZONES_PER_ROW (TOTAL_WIDTH / ZONE_WIDTH)
#define TOTAL_ZONES ((TOTAL_HEIGHT / ZONE_HEIGHT) * ZONES_PER_ROW)
#define ZONE_SIZE (ZONE_WIDTH * ZONE_HEIGHT * 3)
#define RGB565_ZONE_SIZE (ZONE_WIDTH * ZONE_HEIGHT * 2)
#define MAX_COLOR_ROTATIONS 8
#define LED_CHECK_DELAY 1000  // ms per color
#define DEBUG_DELAY 5000      // ms

#include <Arduino.h>
#include <Bounce2.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <LittleFS.h>

#include <cstring>

#include "fonts/tiny4x6.h"
#include "miniz/miniz.h"

#ifdef ZEDMD_WIFI
#include <AsyncUDP.h>
#include <WiFi.h>


String ssid;
String pwd;
uint16_t port;
uint8_t ssid_length;
uint8_t pwd_length;

AsyncUDP udp;
IPAddress ip;

unsigned long rotNextRotationTime[1];
#else
// color rotation
uint8_t rotFirstColor[MAX_COLOR_ROTATIONS];
uint8_t rotAmountColors[MAX_COLOR_ROTATIONS];
unsigned int rotStepDurationTime[MAX_COLOR_ROTATIONS];
unsigned long rotNextRotationTime[MAX_COLOR_ROTATIONS];
uint8_t tmpColor[3] = {0};

bool upscaling = true;
#endif

#ifdef ARDUINO_ESP32_S3_N16R8
#define R1_PIN 4
#define G1_PIN 5
#define B1_PIN 6
#define R2_PIN 7
#define G2_PIN 15
#define B2_PIN 16
#define A_PIN 18
#define B_PIN 8
// #define C_PIN 3
#define C_PIN 46
#define D_PIN 42
#define E_PIN 1  // required for 1/32 scan panels, like 64x64.
#define LAT_PIN 40
#define OE_PIN 2
#define CLK_PIN 41

#define RGB_ORDER_BUTTON_PIN 45
#define BRIGHTNESS_BUTTON_PIN 48
#else
// Pinout derived from ESP32-HUB75-MatrixPanel-I2S-DMA.h
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 19
#define C_PIN 5
#define D_PIN 17
#define E_PIN \
  22  // required for 1/32 scan panels, like 64x64. Any available pin would do,
      // i.e. IO32. If 1/16 scan panels, no connection to this pin needed
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

#define RGB_ORDER_BUTTON_PIN 21
#define BRIGHTNESS_BUTTON_PIN 33
#endif

#define N_CTRL_CHARS 6
#define N_INTERMEDIATE_CTR_CHARS 4

// !!!!! NE METTRE AUCUNE VALEURE IDENTIQUE !!!!!
uint8_t CtrlCharacters[6] = {0x5a, 0x65, 0x64, 0x72, 0x75, 0x6d};
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

Bounce2::Button *rgbOrderButton;
Bounce2::Button *brightnessButton;

const uint8_t lumval[16] = {
    0,  2,  4,  7,   11,  18,  30,  40,
    50, 65, 80, 100, 125, 160, 200, 255};  // Non-linear brightness progression

uint8_t rgbMode = 0;
uint8_t rgbModeLoaded = 0;
const uint8_t rgbOrder[3 * 6] = {
    R, G, B,  // rgbMode 0
    B, R, G,  // rgbMode 1
    G, B, R,  // rgbMode 2
    R, B, G,  // rgbMode 3
    G, R, B,  // rgbMode 4
    B, G, R   // rgbMode 5
};

MatrixPanel_I2S_DMA *dma_display;

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
uint8_t lumstep = 1;
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

void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                 uint8_t b, bool transparent = false, bool inverted = false) {
  for (uint8_t ti = 0; ti < strlen(text); ti++) {
    for (uint8_t tj = 0; tj <= 5; tj++) {
      uint8_t fourPixels = getFontLine(text[ti], tj);
      for (uint8_t pixel = 0; pixel < 4; pixel++) {
        bool p = (fourPixels >> (3 - pixel)) & 0x1;
        if (inverted) {
          p = !p;
        }
        if (transparent && !p) {
          continue;
        }
        dma_display->drawPixelRGB888(x + pixel + (ti * 4), y + tj, r * p, g * p,
                                     b * p);
      }
    }
  }
}

void DisplayNumber(uint32_t chf, uint8_t nc, uint16_t x, uint16_t y, uint8_t r,
                   uint8_t g, uint8_t b, bool transparent = false) {
  char text[nc];
  sprintf(text, "%d", chf);

  uint8_t i = 0;
  if (strlen(text) < nc) {
    for (; i < (nc - strlen(text)); i++) {
      DisplayText(" ", x + (4 * i), y, r, g, b, transparent);
    }
  }

  DisplayText(text, x + (4 * i), y, r, g, b, transparent);
}

void DisplayVersion(bool logo = false) {
  // display the version number to the lower right
  char version[10];
  snprintf(version, 9, "%d.%d.%d", ZEDMD_VERSION_MAJOR, ZEDMD_VERSION_MINOR,
           ZEDMD_VERSION_PATCH);
  DisplayText(version, TOTAL_WIDTH - (strlen(version) * 4), TOTAL_HEIGHT - 5,
              255 * !logo, 255 * !logo, 255 * !logo, logo);
}

void DisplayLum(void) {
  DisplayText(" ", (TOTAL_WIDTH / 2) - 26 - 1, TOTAL_HEIGHT - 6, 128, 128, 128);
  DisplayText("Brightness:", (TOTAL_WIDTH / 2) - 26, TOTAL_HEIGHT - 6, 128, 128,
              128);
  DisplayNumber(lumstep, 2, (TOTAL_WIDTH / 2) + 18, TOTAL_HEIGHT - 6, 255, 191,
                0);
}

void DisplayRGB(void) {
  DisplayText("red", 0, 0, 0, 0, 0, true, true);
  for (uint8_t i = 0; i < 6; i++) {
    dma_display->drawPixelRGB888(TOTAL_WIDTH - (4 * 4) - 1, i, 0, 0, 0);
    dma_display->drawPixelRGB888((TOTAL_WIDTH / 2) - (6 * 4) - 1, i, 0, 0, 0);
  }
  DisplayText("blue", TOTAL_WIDTH - (4 * 4), 0, 0, 0, 0, true, true);
  DisplayText("green", 0, TOTAL_HEIGHT - 6, 0, 0, 0, true, true);
  DisplayText("RGB Order:", (TOTAL_WIDTH / 2) - (6 * 4), 0, 128, 128, 128);
  DisplayNumber(rgbMode, 2, (TOTAL_WIDTH / 2) + (4 * 4), 0, 255, 191, 0);
}

void DisplayDebugInfo(void) {
  // WTF not comparing against true kills the debug mode
  if (debugMode == true) {
    DisplayText("Frames:", 0, 0, 255, 255, 255);
    DisplayNumber(frameCount, 5, 7 * 4, 0, 0, 255, 0);
    DisplayText("Transfer Buffer:", 0, 6, 255, 255, 255);
    DisplayNumber(transferBufferSize, 5, 16 * 4, 6, 255, 255, 255);
    DisplayText("Received Bytes: ", 0, 2 * 6, 255, 255, 255);
    DisplayNumber(receivedBytes, 5, 16 * 4, 2 * 6, 255, 255, 255);
    DisplayText("Miniz Status:", 0, 3 * 6, 255, 255, 255);
    DisplayNumber(minizStatus, 6, 13 * 4, 3 * 6, 255, 255, 255);
    DisplayText("Errors:", 0, 4 * 6, 255, 255, 255);
    DisplayNumber(errorCount, 5, 7 * 4, 4 * 6, 255, 0, 0);

    DisplayNumber(RomWidth, 3, TOTAL_WIDTH - 6 * 4, 0, 255, 255, 255);
    DisplayText("x", TOTAL_WIDTH - 3 * 4, 0, 255, 255, 255);
    DisplayNumber(RomHeight, 2, TOTAL_WIDTH - 2 * 4, 0, 255, 255, 255);
    DisplayNumber(flowControlCounter, 2, TOTAL_WIDTH - 5 * 4, TOTAL_HEIGHT - 6,
                  0, 0, 255);
    DisplayNumber(c4, 2, TOTAL_WIDTH - 2 * 4, TOTAL_HEIGHT - 6, 255, 255, 255);
  }
}

void ClearScreen() {
  dma_display->clearScreen();
  dma_display->setBrightness8(lumval[lumstep]);
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

void fillZoneRaw(uint8_t idx, uint8_t *pBuffer) {
  uint8_t yOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
  uint8_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

  for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
      uint16_t pos = (y * ZONE_WIDTH + x) * 3;

      dma_display->drawPixelRGB888(x + xOffset, y + yOffset, pBuffer[pos],
                                   pBuffer[pos + 1], pBuffer[pos + 2]);
    }
  }
}

void fillZoneRaw565(uint8_t idx, uint8_t *pBuffer) {
  uint8_t yOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
  uint8_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

  for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
      uint16_t pos = (y * ZONE_WIDTH + x) * 2;
      dma_display->drawPixel(
          x + xOffset, y + yOffset,
          (((uint16_t)pBuffer[pos + 1]) << 8) + pBuffer[pos]);
    }
  }
}

void fillPanelRaw() {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = (y * TOTAL_WIDTH + x) * 3;

      dma_display->drawPixelRGB888(x, y, renderBuffer[pos],
                                   renderBuffer[pos + 1],
                                   renderBuffer[pos + 2]);
    }
  }
}

void fillPanelUsingPalette() {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = renderBuffer[y * TOTAL_WIDTH + x] * 3;

      dma_display->drawPixelRGB888(x, y, palette[pos], palette[pos + 1],
                                   palette[pos + 2]);
    }
  }
}

#if !defined(ZEDMD_WIFI)
void fillPanelUsingChangedPalette(bool *paletteAffected) {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = renderBuffer[y * TOTAL_WIDTH + x];
      if (paletteAffected[pos]) {
        pos *= 3;
        dma_display->drawPixelRGB888(x, y, palette[pos], palette[pos + 1],
                                     palette[pos + 2]);
      }
    }
  }
}
#endif

void LoadRgbOrder() {
  File f = LittleFS.open("/ordrergb.val", "r");
  if (!f) return;
  rgbMode = rgbModeLoaded = f.read();
  f.close();
}

void SaveRgbOrder() {
  File f = LittleFS.open("/ordrergb.val", "w");
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
#endif

void ledTester(void) {
  dma_display->fillScreenRGB888(255, 0, 0);
  delay(LED_CHECK_DELAY);

  dma_display->fillScreenRGB888(0, 255, 0);
  delay(LED_CHECK_DELAY);

  dma_display->fillScreenRGB888(0, 0, 255);
  delay(LED_CHECK_DELAY);

  dma_display->clearScreen();
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

  fillPanelRaw();

  free(renderBuffer);

  DisplayVersion(true);

#ifdef ZEDMD_WIFI
  if (ip = WiFi.localIP()) {
    for (uint8_t i = 0; i < 4; i++) {
      DisplayNumber(ip[i], 3, i * 3 * 4 + i, 0, 200, 200, 200);
    }
  }
#endif

  displayStatus = DISPLAY_STATUS_NORMAL_OPERATION;
  MireActive = true;
  // Re-use this variable to save memory
  rotNextRotationTime[0] = millis();
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
  for (uint16_t tj = 0; tj < TOTAL_BYTES; tj++) {
    renderBuffer[tj] = f.read();
  }
  f.close();

  fillPanelRaw();

  free(renderBuffer);

  displayStatus = DISPLAY_STATUS_INFO;
  // Re-use this variable to save memory
  rotNextRotationTime[0] = millis() - (LOGO_TIMEOUT / 2);
}

void ScreenSaver(void) {
  ClearScreen();
  dma_display->setBrightness8(lumval[1]);
  DisplayVersion();

  displayStatus = DISPLAY_STATUS_SCREEN_SAVER;
}

#ifdef ZEDMD_WIFI
// wifi event handler
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      ip = WiFi.localIP();
      for (int i = 0; i < 4; i++) {
        DisplayNumber(ip[i], 3, i * 3 * 4 + i, 0, 200, 200, 200);
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      break;
    default:
      break;
  }
}

/// @brief Handles the UDP Packet parsing for ZeDMD WiFi and ZeDMD-HD WiFi
/// @param packet
void IRAM_ATTR handlePacket(AsyncUDPPacket packet) {
  uint8_t *pPacket = packet.data();
  
  if (packet.length() >= 1) {
    if (MireActive) {
      MireActive = false;
      ClearScreen();
    }

    switch (pPacket[0]) {
      case 10:  // clear screen
      {
        ClearScreen();
        break;
      }
      case 4:  // RGB24 Zones Stream
      {
        uint8_t compressed = pPacket[1] & 128;
        uint8_t numZones = pPacket[1] & 127;
        uint16_t size = (int)(pPacket[3]) + (((int)pPacket[2]) << 8);

        renderBuffer = (uint8_t *)malloc(ZONE_SIZE * numZones + numZones);

        if (compressed == 128) {
          mz_ulong uncompressedBufferSize = ZONE_SIZE * numZones + numZones;
          mz_ulong udpPayloadSize = (mz_ulong)size;

          minizStatus =
              mz_uncompress2(renderBuffer, &uncompressedBufferSize, &pPacket[4],
                             (mz_ulong *)&udpPayloadSize);

          if (minizStatus != MZ_OK ||
              uncompressedBufferSize != (ZONE_SIZE * numZones + numZones)) {
            DisplayDebugInfo();
            return;
          }
        } else {
          memcpy(renderBuffer, &pPacket[4], size);
        }

        for (uint8_t idx = 0; idx < numZones; idx++) {
          fillZoneRaw(renderBuffer[idx * ZONE_SIZE + idx],
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
            DisplayDebugInfo();
            return;
          }
        } else {
          memcpy(renderBuffer, &pPacket[4], size);
        }
       
        for (uint8_t idx = 0; idx < numZones; idx++) {
          fillZoneRaw565(renderBuffer[idx * RGB565_ZONE_SIZE + idx],
                         &renderBuffer[idx * RGB565_ZONE_SIZE + idx + 1]);
        }
        
        free(renderBuffer);
        break;
      }
    }

  }
}


#endif

void setup() {
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
  }

  const uint8_t colorPins1[3] = {R1_PIN, G1_PIN, B1_PIN};
  const uint8_t colorPins2[3] = {R2_PIN, G2_PIN, B2_PIN};
  const HUB75_I2S_CFG::i2s_pins _pins = {colorPins1[rgbOrder[rgbMode * 3]],
                                         colorPins1[rgbOrder[rgbMode * 3 + 1]],
                                         colorPins1[rgbOrder[rgbMode * 3 + 2]],
                                         colorPins2[rgbOrder[rgbMode * 3]],
                                         colorPins2[rgbOrder[rgbMode * 3 + 1]],
                                         colorPins2[rgbOrder[rgbMode * 3 + 2]],
                                         A_PIN,
                                         B_PIN,
                                         C_PIN,
                                         D_PIN,
                                         E_PIN,
                                         LAT_PIN,
                                         OE_PIN,
                                         CLK_PIN};

  HUB75_I2S_CFG mxconfig(PANEL_WIDTH,    // width
                         PANEL_HEIGHT,   // height
                         PANELS_NUMBER,  // chain length
                         _pins           // pin mapping
                         // HUB75_I2S_CFG::FM6126A  // driver chip
  );
  mxconfig.clkphase =
      false;  // change if you have some parts of the panel with a shift
              // #ifdef ARDUINO_ESP32_S3_N16R8
              //  mxconfig.double_buff = true; // Turn on double buffer
              // #endif
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();

  if (!fileSystemOK) {
    DisplayText("Error reading file system!", 4, 6, 255, 255, 255);
    DisplayText("Try to flash the firmware again.", 4, 14, 255, 255, 255);
    // #ifdef ARDUINO_ESP32_S3_N16R8
    //     display->flipDMABuffer();
    // #endif
    while (true);
  }

  Serial.setRxBufferSize(SERIAL_BUFFER);
#ifndef ARDUINO_ESP32_S3_N16R8
  Serial.setTimeout(SERIAL_TIMEOUT);
#endif
  Serial.begin(SERIAL_BAUD);
  while (!Serial);

  DisplayLogo();

#ifdef ZEDMD_WIFI
  if (LoadWiFiConfig()) {
    WiFi.disconnect(true);
    // register event handler
    WiFi.onEvent(WiFiEvent);

    // Initiate connection
    WiFi.begin(ssid.substring(0, ssid_length).c_str(),
               pwd.substring(0, pwd_length).c_str());

    uint8_t result = WiFi.waitForConnectResult();

    // Eventhandler for incomming UDP packets for DMD traffic
    if (udp.listen(port)) {
      udp.onPacket(handlePacket);
    }
  }
#endif
}

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

    transferBuffer = (uint8_t *)malloc(transferBufferSize + BUFFER_OVERHEAD);
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
    mz_ulong uncompressed_buffer_size = (mz_ulong)BufferSize + BUFFER_OVERHEAD;
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

void updateColorRotations(void) {
#if !defined(ZEDMD_WIFI)
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

  if (rotfound == true) fillPanelUsingChangedPalette(rotPaletteAffected);
#endif
}

bool wait_for_ctrl_chars(void) {
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
      updateColorRotations();
    } else if (displayStatus == DISPLAY_STATUS_CLEAR &&
               (millis() - rotNextRotationTime[0]) > LOGO_TIMEOUT) {
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

void loop() {
  while (MireActive) {
    rgbOrderButton->update();
    if (rgbOrderButton->pressed()) {
      if (rgbModeLoaded != 0) {
        rgbMode = 0;
        SaveRgbOrder();
        ESP.restart();
      }

      if (displayStatus != DISPLAY_STATUS_NORMAL_OPERATION) {
        DisplayLogo();
        DisplayRGB();
        DisplayLum();
        continue;
      }

      rotNextRotationTime[0] = millis();
      rgbMode++;
      if (rgbMode > 5) rgbMode = 0;
      SaveRgbOrder();
      DisplayLogo();
      DisplayRGB();
      DisplayLum();
    }

    brightnessButton->update();
    if (brightnessButton->pressed()) {
      if (displayStatus != DISPLAY_STATUS_NORMAL_OPERATION) {
        DisplayLogo();
        DisplayRGB();
        DisplayLum();
        continue;
      }

      rotNextRotationTime[0] = millis();
      lumstep++;
      if (lumstep >= 16) lumstep = 1;
      dma_display->setBrightness8(lumval[lumstep]);
      SaveLum();
      DisplayRGB();
      DisplayLum();
    }

    if (Serial.available() > 0) {
      if (rgbMode != rgbModeLoaded) {
        ESP.restart();
      }
      ClearScreen();
      MireActive = false;
    } else if ((millis() - rotNextRotationTime[0]) > LOGO_TIMEOUT) {
      if (displayStatus == DISPLAY_STATUS_NORMAL_OPERATION) {
        if (rgbMode != rgbModeLoaded) {
          ESP.restart();
        }
        DisplayUpdate();
      } else if (displayStatus != DISPLAY_STATUS_SCREEN_SAVER) {
        ScreenSaver();
      }
    }
  }

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

  if (wait_for_ctrl_chars()) {
    // Updates to mode64 color rotations have been handled within
    // wait_for_ctrl_chars(), now reset it to false.
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
        uint8_t tbuf[4 + BUFFER_OVERHEAD];
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
          DisplayText("Unsupported chunk size:", 0, 0, 255, 0, 0);
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
        ledTester();
        break;
      }

#if !defined(ZEDMD_WIFI)
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
#endif

      case 22:  // set brightness
      {
        uint8_t tbuf[1 + BUFFER_OVERHEAD];
        if (SerialReadBuffer(tbuf, 1)) {
          if (tbuf[0] > 0 && tbuf[0] < 16) {
            lumstep = tbuf[0];
            dma_display->setBrightness8(lumval[lumstep]);
            Serial.write('A');
          } else {
            Serial.write('E');
          }
        }
        break;
      }

      case 23:  // set RGB order
      {
        uint8_t tbuf[1 + BUFFER_OVERHEAD];
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

#ifdef ZEDMD_WIFI
      // These are one time programmig options over USB until ZeDMD could
      // establish the WiFi Connection at startup. When WiFi is finally active,
      // USB is turned off.
      case 27:  // set WiFi SSID
      {
        uint8_t tbuf[33 + BUFFER_OVERHEAD];
        if (SerialReadBuffer(tbuf, 33, false)) {
          // tbuf[0] now contains the length of the string
          ssid_length = tbuf[0];
          char *tmp = (char *)malloc(ssid_length);
          memcpy(tmp, &tbuf[1], ssid_length);
          ssid = String(tmp);
          free(tmp);
          Serial.write('A');
        } else {
          Serial.write('E');
        }
        break;
      }

      case 28:  // set WiFi password
      {
        uint8_t tbuf[33 + BUFFER_OVERHEAD];
        if (SerialReadBuffer(tbuf, 33, false)) {
          // tbuf[0] now contains the length of the string
          pwd_length = tbuf[0];
          char *tmp = (char *)malloc(pwd_length);
          memcpy(tmp, &tbuf[1], pwd_length);
          pwd = String(tmp);
          free(tmp);
          Serial.write('A');
        } else {
          Serial.write('E');
        }
        break;
      }

      case 29:  // set WiFi port
      {
        uint8_t tbuf[2 + BUFFER_OVERHEAD];
        if (SerialReadBuffer(tbuf, 2)) {
          port = ((((unsigned int)tbuf[0]) << 8) + ((unsigned int)tbuf[1]));
          Serial.write('A');
        } else {
          Serial.write('E');
        }
        break;
      }
#endif

      case 30:  // save settings
      {
        Serial.write('A');
        SaveLum();
        SaveRgbOrder();
#ifdef ZEDMD_WIFI
        SaveWiFiConfig();
#endif
        break;
      }

      case 31:  // reset
      {
        ESP.restart();
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
        rotNextRotationTime[0] = millis();
        break;
      }

#if !defined(ZEDMD_WIFI)
      case 4:  // mode RGB24 zones streaming
      {
        renderBuffer = (uint8_t *)malloc(TOTAL_ZONES * ZONE_SIZE +
                                         ZONES_PER_ROW + BUFFER_OVERHEAD);
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
            fillZoneRaw(renderBuffer[idx], &renderBuffer[++idx]);
            idx += ZONE_SIZE;
          }
        }

        free(renderBuffer);
        break;
      }

      case 5:  // mode RGB565 zones streaming
      {
        renderBuffer = (uint8_t *)malloc(TOTAL_ZONES * RGB565_ZONE_SIZE +
                                         ZONES_PER_ROW + BUFFER_OVERHEAD);
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
            fillZoneRaw565(renderBuffer[idx], &renderBuffer[++idx]);
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
        renderBuffer = (uint8_t *)malloc(renderBufferSize + BUFFER_OVERHEAD);

        if (SerialReadBuffer(renderBuffer, RomHeight * RomWidth * 3)) {
          mode64 = false;
          ScaleImage(3);
          fillPanelRaw();
        }

        free(renderBuffer);
        break;
      }

      case 8:  // mode 4 couleurs avec 1 palette 4 couleurs (4*3 bytes) suivis
               // de 4 pixels par byte
      {
        uint16_t bufferSize = 12 + 2 * RomWidthPlane * RomHeight;
        uint8_t *buffer = (uint8_t *)malloc(bufferSize + BUFFER_OVERHEAD);

        if (SerialReadBuffer(buffer, bufferSize)) {
          // We need to cover downscaling, too.
          uint16_t renderBufferSize =
              (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                  ? TOTAL_WIDTH * TOTAL_HEIGHT
                  : RomWidth * RomHeight;
          renderBuffer = (uint8_t *)malloc(renderBufferSize);
          memset(renderBuffer, 0, renderBufferSize);
          palette = (uint8_t *)malloc(12);
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
          fillPanelUsingPalette();

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
        uint8_t *buffer = (uint8_t *)malloc(bufferSize + BUFFER_OVERHEAD);

        if (SerialReadBuffer(buffer, bufferSize)) {
          // We need to cover downscaling, too.
          uint16_t renderBufferSize =
              (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                  ? TOTAL_WIDTH * TOTAL_HEIGHT
                  : RomWidth * RomHeight;
          renderBuffer = (uint8_t *)malloc(renderBufferSize);
          memset(renderBuffer, 0, renderBufferSize);
          palette = (uint8_t *)malloc(48);
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
          fillPanelUsingPalette();

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
        uint8_t *buffer = (uint8_t *)malloc(bufferSize + BUFFER_OVERHEAD);

        if (SerialReadBuffer(buffer, bufferSize)) {
          // We need to cover downscaling, too.
          uint16_t renderBufferSize =
              (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                  ? TOTAL_WIDTH * TOTAL_HEIGHT
                  : RomWidth * RomHeight;
          renderBuffer = (uint8_t *)malloc(renderBufferSize);
          memset(renderBuffer, 0, renderBufferSize);
          palette = (uint8_t *)malloc(48);
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
          fillPanelUsingPalette();

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
        uint8_t *buffer = (uint8_t *)malloc(bufferSize + BUFFER_OVERHEAD);

        if (SerialReadBuffer(buffer, bufferSize)) {
          // We need to cover downscaling, too.
          uint16_t renderBufferSize =
              (RomWidth < TOTAL_WIDTH || RomHeight < TOTAL_HEIGHT)
                  ? TOTAL_WIDTH * TOTAL_HEIGHT
                  : RomWidth * RomHeight;
          renderBuffer = (uint8_t *)malloc(renderBufferSize);
          memset(renderBuffer, 0, renderBufferSize);
          palette = (uint8_t *)malloc(192);
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
          fillPanelUsingPalette();

          free(renderBuffer);
          free(palette);
        } else {
          free(buffer);
        }
        break;
      }
#endif
      default: {
        DisplayText("Unsupported render mode:", 0, 0, 255, 0, 0);
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
}
