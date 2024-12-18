#include <Arduino.h>
#include <LittleFS.h>

#include "displayConfig.h"  // Variables shared by main and displayDrivers
#include "displayDriver.h"  // Base class for all display drivers
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "miniz/miniz.h"
#include "panel.h"
#include "version.h"

// To save RAM only include the driver we want to use.
#ifdef DISPLAY_RM67162_AMOLED
#include "displays/Rm67162Amoled.h"
#elif defined(DISPLAY_LED_MATRIX)
#include "displays/LEDMatrix.h"
#endif

#define N_CTRL_CHARS 6
#define N_INTERMEDIATE_CTR_CHARS 4
#define NUM_BUFFERS 8  // Number of buffers
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#define SERIAL_BAUD 2000000  // Serial baud rate.
#define SERIAL_BUFFER 1024   // Serial buffer size in byte.
#else
#define SERIAL_BAUD 921600  // Serial baud rate.
#define SERIAL_BUFFER 4096  // Serial buffer size in byte.
#endif
#define SERIAL_TIMEOUT \
  8  // Time in milliseconds to wait for the next data chunk.

// Specific improvements and #define for the ESP32 S3 series
#if defined(ARDUINO_ESP32_S3_N16R8) || defined(DISPLAY_RM67162_AMOLED)
#include "S3Specific.h"
#endif

DisplayDriver *display;

// Buffers for storing data
u_int8_t buffers[NUM_BUFFERS][RGB565_ZONE_SIZE * ZONES_PER_ROW]
    __attribute__((aligned(4)));
uint16_t bufferSizes[NUM_BUFFERS] = {0};
uint16_t bufferCommands[NUM_BUFFERS] = {0};

// Semaphores
SemaphoreHandle_t xBufferFilled[NUM_BUFFERS];
SemaphoreHandle_t xBufferProcessed[NUM_BUFFERS];

// @todo zu gross
uint8_t uncompressBuffer[TOTAL_BYTES] __attribute__((aligned(4)));
uint8_t renderBuffer[TOTAL_BYTES] __attribute__((aligned(4)));
uint8_t processingBuffer = 0;

uint8_t lumstep = 5;  // Init display on medium brightness, otherwise it starts
                      // up black on some displays

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

/// @brief Get DisplayDriver object, required for webserver
DisplayDriver *GetDisplayObject() { return display; }

void ClearScreen() {
  display->ClearScreen();
  display->SetBrightness(lumstep);
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

void Task_ReadSerial(void *pvParameters) {
  const uint8_t CtrlChars[6] = {0x5a, 0x65, 0x64, 0x72, 0x75, 0x6d};
  uint8_t numCtrlCharsFound = 0;
  uint16_t payloadSize = 0;
  uint8_t command = 0;
  uint8_t currentBuffer = NUM_BUFFERS - 1;

  Serial.setRxBufferSize(SERIAL_BUFFER);
  Serial.setTimeout(SERIAL_TIMEOUT);
  Serial.begin(SERIAL_BAUD);
  while (!Serial);

  while (1) {
    // Wait for data to be ready

    if (Serial.available()) {
      uint8_t byte = Serial.read();
      // DisplayNumber(byte, 2, TOTAL_WIDTH - 2 * 4, TOTAL_HEIGHT - 6, 255, 255,
      // 255);

      if (numCtrlCharsFound < N_CTRL_CHARS) {
        // Detect 6 consecutive start bits
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

          case 10:  // clear screen
          {
            Serial.write('A');
            // ClearScreen();
            numCtrlCharsFound = 0;
            break;
          }

          case 2:  // set rom frame size
          case 5:  // mode RGB565 zones streaming
          {
            // Read payload size (next 2 bytes)
            payloadSize = (Serial.read() << 8) | Serial.read();

            if (payloadSize > RGB565_ZONE_SIZE * ZONES_PER_ROW ||
                payloadSize == 0) {
              Serial.write('E');
              numCtrlCharsFound = 0;
              break;
            }

            numCtrlCharsFound++;

            // Move to the next buffer
            currentBuffer = (currentBuffer + 1) % NUM_BUFFERS;
            xSemaphoreTake(xBufferProcessed[currentBuffer], portMAX_DELAY);
            bufferSizes[currentBuffer] = payloadSize;
            bufferCommands[currentBuffer] = command;

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

void setup() {
  bool fileSystemOK;
  if (fileSystemOK = LittleFS.begin()) {
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

  DisplayLogo();

  // Create synchronization primitives
  for (uint8_t i = 0; i < NUM_BUFFERS; i++) {
    xBufferFilled[i] = xSemaphoreCreateBinary();
    xBufferProcessed[i] = xSemaphoreCreateBinary();
  }

  // Create FreeRTOS tasks
  xTaskCreatePinnedToCore(Task_ReadSerial, "Task_ReadSerial", 1024, NULL, 1,
                          NULL, 0);

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
      display->DisplayText("miniz error ", 0, 0, 255, 0, 0);
      DisplayNumber(minizStatus, 3, 0, 6, 255, 0, 0);
    }

    // Move to the next buffer
    processingBuffer = (processingBuffer + 1) % NUM_BUFFERS;
  }
}
