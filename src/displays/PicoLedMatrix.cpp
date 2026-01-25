#if defined(DISPLAY_LED_MATRIX) && defined(PICO_BUILD)

#include "PicoLedMatrix.h"

#include "pico/zedmd_pico.h"

static uint8_t r5_to_8[32];
static uint8_t g6_to_8[64];
static uint8_t b5_to_8[32];

static void init_rgb_tables() {
  for (int i = 0; i < 32; i++) {
    // replicate high bits into low bits
    r5_to_8[i] = (i << 3) | (i >> 2);
    b5_to_8[i] = (i << 3) | (i >> 2);
  }
  for (int i = 0; i < 64; i++) {
    g6_to_8[i] = (i << 2) | (i >> 4);
  }
}

PicoLedMatrix::PicoLedMatrix() {
  // rgb565 > rgb888 "fast" pixel conversion
  init_rgb_tables();

  create_hub75_driver(TOTAL_WIDTH, PANEL_HEIGHT, PanelType::PANEL_RUL6024,
                      false);
  start_hub75_driver();
}

PicoLedMatrix::~PicoLedMatrix() {}

void IRAM_ATTR PicoLedMatrix::DrawPixel(const uint16_t x, const uint16_t y,
                                        const uint8_t r, const uint8_t g,
                                        const uint8_t b) {
  uint16_t pos = ((y + yOffset) * TOTAL_WIDTH + x) * 3;
  drawBuffer[pos] = b;
  drawBuffer[++pos] = g;
  drawBuffer[++pos] = r;
}

void IRAM_ATTR PicoLedMatrix::DrawPixel(const uint16_t x, const uint16_t y,
                                        const uint16_t color) {
  uint16_t pos = ((y + yOffset) * TOTAL_WIDTH + x) * 3;
  drawBuffer[pos] = r5_to_8[(color >> 11) & 0x1F];
  drawBuffer[++pos] = g6_to_8[(color >> 5) & 0x3F];
  drawBuffer[++pos] = b5_to_8[color & 0x1F];
}

void PicoLedMatrix::ClearScreen() { memset(drawBuffer, 0, sizeof(drawBuffer)); }

void PicoLedMatrix::SetBrightness(const uint8_t level) {
  // TODO: verify this (compare with an "esp board") ?
  const auto b = static_cast<uint8_t>(static_cast<float>(level) * 1.5f);
  setBasisBrightness(b);
}

void PicoLedMatrix::Render() {
  // double buffering is handled inside the hub75 driver
  update_bgr(drawBuffer);
}

#endif  // PICO_BUILD
