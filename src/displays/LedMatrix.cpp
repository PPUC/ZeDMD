#ifdef DISPLAY_LED_MATRIX
#include "LedMatrix.h"

#include <cstring>

#include "fonts/tiny4x6.h"

bool LedMatrix::HasScalingModes() {
  return false;  // This display does not support subpixel scaling
}

const char **LedMatrix::GetScalingModes() { return nullptr; }

uint8_t LedMatrix::GetScalingModeCount() { return 0; }

uint8_t LedMatrix::GetCurrentScalingMode() { return 0; }

void LedMatrix::SetCurrentScalingMode(uint8_t mode) {}

void LedMatrix::SetBrightness(uint8_t level) {}

void LedMatrix::FillScreen(uint8_t r, uint8_t g, uint8_t b) {
  for (auto x = 0; x < TOTAL_WIDTH; x++) {
    for (auto y = 0; y < TOTAL_HEIGHT; y++) {
      DrawPixel(x, y, r, g, b);
    }
  }
}

void LedMatrix::ClearScreen() { FillScreen(0, 0, 0); }

void LedMatrix::DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r,
                            uint8_t g, uint8_t b, bool transparent,
                            bool inverted) {
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
        DrawPixel(x + pixel + (ti * 4), y + tj, r * p, g * p, b * p);
      }
    }
  }
}

void LedMatrix::FillZoneRaw(uint8_t idx, uint8_t *pBuffer) {
  const uint8_t zoneYOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
  const uint8_t zoneXOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

  for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
      uint16_t pos = (y * ZONE_WIDTH + x) * 3;

      DrawPixel(x + zoneXOffset, y + zoneYOffset, pBuffer[pos],
                pBuffer[pos + 1], pBuffer[pos + 2]);
    }
  }
}

void LedMatrix::FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) {
  const uint8_t zoneYOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
  const uint8_t zoneXOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

  for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
      uint16_t pos = (y * ZONE_WIDTH + x) * 2;
      DrawPixel(x + zoneXOffset, y + zoneYOffset,
                (((uint16_t)pBuffer[pos + 1]) << 8) + pBuffer[pos]);
    }
  }
}

void LedMatrix::ClearZone(uint8_t idx) {
  const uint8_t zoneYOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
  const uint8_t zoneXOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

  for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
      DrawPixel(x + zoneXOffset, y + zoneYOffset, 0, 0, 0);
    }
  }
}

void LedMatrix::FillPanelRaw(uint8_t *pBuffer) {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = (y * TOTAL_WIDTH + x) * 3;

      DrawPixel(x, y, pBuffer[pos], pBuffer[pos + 1], pBuffer[pos + 2]);
    }
  }
}

#endif