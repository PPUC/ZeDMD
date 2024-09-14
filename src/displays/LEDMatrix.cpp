#include "LEDMatrix.h"
#include "displayConfig.h"


LedMatrix::LedMatrix() {
    const uint8_t colorPins1[3] = {R1_PIN, G1_PIN, B1_PIN};
    const uint8_t colorPins2[3] = {R2_PIN, G2_PIN, B2_PIN};
    const HUB75_I2S_CFG::i2s_pins pins = {
        colorPins1[rgbOrder[rgbMode * 3]],
        colorPins1[rgbOrder[rgbMode * 3 + 1]],
        colorPins1[rgbOrder[rgbMode * 3 + 2]],
        colorPins2[rgbOrder[rgbMode * 3]],
        colorPins2[rgbOrder[rgbMode * 3 + 1]],
        colorPins2[rgbOrder[rgbMode * 3 + 2]],
        A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
        LAT_PIN, OE_PIN, CLK_PIN
    };

    HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANELS_NUMBER, pins);
    mxconfig.clkphase = false;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
}

void LedMatrix::DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) {
    dma_display->drawPixelRGB888(x, y, r, g, b);
}

void LedMatrix::DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
    dma_display->drawPixel(x, y, color);
}

void LedMatrix::ClearScreen() {
    dma_display->clearScreen();
}

void LedMatrix::SetBrightness(uint8_t level) {
    dma_display->setBrightness8(lumval[level]);
}

void LedMatrix::FillScreen(uint8_t r, uint8_t g, uint8_t b) {
    dma_display->fillScreenRGB888(r, g, b);
}

void LedMatrix::UpdateDisplay() {
    // Implementation specific to updating the display
}

void LedMatrix::UpdateDisplayZone(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    // Implementation specific to updating display zones
}

LedMatrix::~LedMatrix() {
    delete dma_display;
}
