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

static inline void write_mapped_pixel(uint8_t *dst, const uint8_t r,
                                      const uint8_t g, const uint8_t b) {
  const uint8_t mode = rgbMode % 6;
  const uint8_t base = mode * 3;
  const uint8_t src[3] = {r, g, b};
  dst[0] = src[rgbOrder[base]];
  dst[1] = src[rgbOrder[base + 1]];
  dst[2] = src[rgbOrder[base + 2]];
}

PicoLedMatrix::PicoLedMatrix() {
  // rgb565 > rgb888 "fast" pixel conversion
  init_rgb_tables();

  create_hub75_driver(TOTAL_WIDTH, PANEL_HEIGHT, PANEL_TYPE, INVERTED_STB);
  start_hub75_driver();
}

PicoLedMatrix::~PicoLedMatrix() {}

void IRAM_ATTR PicoLedMatrix::DrawPixel(const uint16_t x, const uint16_t y,
                                        const uint8_t r, const uint8_t g,
                                        const uint8_t b) {
  const uint16_t pos = ((y + yOffset) * TOTAL_WIDTH + x) * 3;
  write_mapped_pixel(&drawBuffer[pos], r, g, b);
}

void IRAM_ATTR PicoLedMatrix::DrawPixel(const uint16_t x, const uint16_t y,
                                        const uint16_t color) {
  const uint16_t pos = ((y + yOffset) * TOTAL_WIDTH + x) * 3;
  write_mapped_pixel(&drawBuffer[pos], r5_to_8[(color >> 11) & 0x1F],
                     g6_to_8[(color >> 5) & 0x3F], b5_to_8[color & 0x1F]);
}

void PicoLedMatrix::ClearScreen() { memset(drawBuffer, 0, sizeof(drawBuffer)); }

void PicoLedMatrix::SetBrightness(const uint8_t level) {
  setBasisBrightness(level);
}

void PicoLedMatrix::Render() {
  // double buffering is handled inside the hub75 driver
  update_bgr(drawBuffer);
}

#endif  // PICO_BUILD
