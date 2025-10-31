#ifdef DISPLAY_LED_MATRIX
#include "LEDMatrix.h"

#include "fonts/tiny4x6.h"

LedMatrix::LedMatrix() {
  int8_t colorPins1[3] = {R1_PIN, G1_PIN, B1_PIN};
  int8_t colorPins2[3] = {R2_PIN, G2_PIN, B2_PIN};
  const HUB75_I2S_CFG::i2s_pins pins = {colorPins1[rgbOrder[rgbMode * 3]],
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

  HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANELS_NUMBER, pins);
  // Without setting clkphase to false, HD panels seem to flicker.
  mxconfig.clkphase = (panelClkphase == 1);
  mxconfig.i2sspeed =
      panelI2sspeed == 20
          ? HUB75_I2S_CFG::clk_speed::HZ_20M
          : (panelI2sspeed == 16 ? HUB75_I2S_CFG::clk_speed::HZ_16M
                                 : HUB75_I2S_CFG::clk_speed::HZ_8M);
  mxconfig.latch_blanking = panelLatchBlanking;
  mxconfig.min_refresh_rate = panelMinRefreshRate;
  mxconfig.driver = (HUB75_I2S_CFG::shift_driver)panelDriver;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
}

bool LedMatrix::HasScalingModes() {
  return false;  // This display does not support subpixel scaling
}

const char **LedMatrix::GetScalingModes() { return nullptr; }

uint8_t LedMatrix::GetScalingModeCount() { return 0; }

uint8_t LedMatrix::GetCurrentScalingMode() { return 0; }

void LedMatrix::SetCurrentScalingMode(uint8_t mode) {}

void LedMatrix::DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                          uint8_t b) {
  dma_display->drawPixelRGB888(x, y + yOffset, r, g, b);
}

void LedMatrix::DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
  dma_display->drawPixel(x, y + yOffset, color);
}

void LedMatrix::ClearScreen() { dma_display->clearScreen(); }

void LedMatrix::SetBrightness(uint8_t level) {
  dma_display->setBrightness8(lumval[level]);
}

void LedMatrix::FillScreen(uint8_t r, uint8_t g, uint8_t b) {
  dma_display->fillScreenRGB888(r, g, b);
}

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

void IRAM_ATTR LedMatrix::FillZoneRaw(uint8_t idx, uint8_t *pBuffer) {
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

void IRAM_ATTR LedMatrix::FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) {
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

void IRAM_ATTR LedMatrix::ClearZone(uint8_t idx) {
  const uint8_t zoneYOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
  const uint8_t zoneXOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

  for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
      DrawPixel(x + zoneXOffset, y + zoneYOffset, 0, 0, 0);
    }
  }
}

void IRAM_ATTR LedMatrix::FillPanelRaw(uint8_t *pBuffer) {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = (y * TOTAL_WIDTH + x) * 3;

      DrawPixel(x, y, pBuffer[pos], pBuffer[pos + 1], pBuffer[pos + 2]);
    }
  }
}

LedMatrix::~LedMatrix() { delete dma_display; }
#endif