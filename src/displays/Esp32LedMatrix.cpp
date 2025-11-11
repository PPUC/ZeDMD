#if defined(DISPLAY_LED_MATRIX) && defined(ESP_BUILD)

#include "Esp32LedMatrix.h"

Esp32LedMatrix::Esp32LedMatrix() {
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

Esp32LedMatrix::~Esp32LedMatrix() { delete dma_display; }

void IRAM_ATTR Esp32LedMatrix::DrawPixel(uint16_t x, uint16_t y, uint8_t r,
                                         uint8_t g, uint8_t b) {
  dma_display->drawPixelRGB888(x, y + yOffset, r, g, b);
}

void IRAM_ATTR Esp32LedMatrix::DrawPixel(uint16_t x, uint16_t y,
                                         uint16_t color) {
  dma_display->drawPixel(x, y + yOffset, color);
}

void Esp32LedMatrix::ClearScreen() { dma_display->clearScreen(); }

void Esp32LedMatrix::SetBrightness(uint8_t level) {
  dma_display->setBrightness8(lumval[level]);
}

void Esp32LedMatrix::FillScreen(uint8_t r, uint8_t g, uint8_t b) {
  dma_display->fillScreenRGB888(r, g, b);
}

#endif