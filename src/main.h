#ifndef ZEDMD_MAIN_H
#define ZEDMD_MAIN_H

#include <cstdint>

#include "displayDriver.h"
#include "miniz/miniz.h"
#include "panel.h"
#include "version.h"

#define N_FRAME_CHARS 5
#define N_CTRL_CHARS 5
#define N_ACK_CHARS (N_CTRL_CHARS + 1)
#define N_INTERMEDIATE_CTR_CHARS 4

#if defined(DMDREADER)
#ifdef ZEDMD_HD
#define NUM_BUFFERS 4  // Number of buffers
#else
#define NUM_BUFFERS 4  // Number of buffers
#endif
#define NUM_RENDER_BUFFERS 1
#define BUFFER_SIZE TOTAL_BYTES
#elif defined(PICO_BUILD)
#if PICO_RP2350
#define NUM_BUFFERS 128  // Number of buffers
#else
#define NUM_BUFFERS 64  // Number of buffers
#endif
#define NUM_RENDER_BUFFERS 1
#define BUFFER_SIZE 1152
#elif defined(DISPLAY_RM67162_AMOLED)
#define NUM_BUFFERS 128  // Number of buffers
// @fixme double buffering doesn't work on Lilygo Amoled
#define NUM_RENDER_BUFFERS 1
#define BUFFER_SIZE 1152
#elif defined(BOARD_HAS_PSRAM)
#define NUM_BUFFERS 128  // Number of buffers
#define NUM_RENDER_BUFFERS 2
#define BUFFER_SIZE 1152
#else
#define NUM_BUFFERS 12  // Number of buffers
#define NUM_RENDER_BUFFERS 1
#define BUFFER_SIZE 1152
#endif

#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED) || \
    defined(PICO_BUILD)
// USB CDC
#define SERIAL_BAUD 115200
#define USB_PACKAGE_SIZE 512
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
#elif defined(PICO_BUILD)
#define UP_BUTTON_PIN 27
#define DOWN_BUTTON_PIN 28
#define FORWARD_BUTTON_PIN 26
#define BACKWARD_BUTTON_PIN 29
#elif defined(DISPLAY_RM67162_AMOLED)
#define UP_BUTTON_PIN 0
#define FORWARD_BUTTON_PIN 21
#else
#define UP_BUTTON_PIN 21
#define FORWARD_BUTTON_PIN 33
#endif

#define LED_CHECK_DELAY 1000  // ms per color

#ifdef ZEDMD_HD_HALF
#define MENU_ITEMS_COUNT 8
#else
#define MENU_ITEMS_COUNT 7
#endif

#define RC 0
#define GC 1
#define BC 2

extern uint8_t debug;
extern bool transportActive;

extern uint8_t *buffers[];
extern mz_ulong bufferSizes[];
extern uint8_t currentBuffer;

extern uint16_t payloadMissing;
extern uint8_t headerBytesReceived;
extern uint8_t numCtrlCharsFound;
extern const uint8_t FrameChars[];
extern const uint8_t CtrlChars[6];

extern const uint8_t rgbOrder[];
extern uint16_t shortId;
extern uint8_t wifiPower;
class Clock;
extern Clock lastDataReceivedClock;
extern uint8_t usbPackageSizeMultiplier;
extern uint8_t rgbMode;
extern uint8_t rgbModeLoaded;
extern uint8_t brightness;
extern int8_t yOffset;
extern uint8_t panelClkphase;
extern uint8_t panelDriver;
extern uint8_t panelI2sspeed;
extern uint8_t panelLatchBlanking;
extern uint8_t panelMinRefreshRate;

extern uint8_t HandleData(uint8_t *pData, size_t len);

extern void MarkCurrentBufferDone();

extern bool AcquireNextProcessingBuffer();

extern void AcquireNextBuffer();

extern DisplayDriver *GetDisplayDriver();

extern void CheckMenuButton();

extern void ClearScreen();

extern void DisplayNumber(uint32_t chf, uint8_t nc, uint16_t x, uint16_t y,
                          uint8_t r, uint8_t g, uint8_t b,
                          bool transparent = false);

extern void DisplayLogo();

extern void DisplayId();

extern void Restart();

extern void SaveRgbOrder();

extern void RefreshSetupScreen();

extern void SaveLum();

extern void SaveScale();

#endif  // ZEDMD_MAIN_H
