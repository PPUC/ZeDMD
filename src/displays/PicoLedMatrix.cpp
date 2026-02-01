#if defined(DISPLAY_LED_MATRIX) && defined(PICO_BUILD)

#include "PicoLedMatrix.h"

#include "pico/zedmd_pico.h"

static pimoroni::Hub75 *s_hub75;

// interrupt callback required function
static void __isr dma_complete() { s_hub75->dma_complete(); }

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

static uint16_t lut_table[256] = {
    0,   0,   1,   1,   1,   2,   2,   3,   4,   5,   5,   6,   7,   8,    9,
    10,  11,  12,  13,  14,  15,  17,  18,  19,  20,  22,  23,  24,  26,   27,
    29,  30,  32,  33,  35,  36,  38,  39,  41,  43,  44,  46,  48,  50,   51,
    53,  55,  57,  59,  60,  62,  64,  66,  68,  70,  72,  74,  76,  78,   80,
    82,  84,  87,  89,  91,  93,  95,  98,  100, 102, 104, 107, 109, 112,  114,
    116, 119, 121, 124, 126, 129, 131, 134, 136, 139, 142, 144, 147, 150,  152,
    155, 158, 161, 163, 166, 169, 172, 175, 178, 181, 184, 187, 190, 193,  196,
    199, 202, 205, 208, 211, 214, 218, 221, 224, 227, 231, 234, 237, 241,  244,
    248, 251, 254, 258, 262, 265, 269, 272, 276, 280, 283, 287, 291, 295,  298,
    302, 306, 310, 314, 318, 322, 326, 330, 334, 338, 342, 346, 350, 354,  359,
    363, 367, 372, 376, 380, 385, 389, 394, 398, 403, 407, 412, 416, 421,  426,
    431, 435, 440, 445, 450, 455, 460, 465, 470, 475, 480, 485, 490, 495,  500,
    506, 511, 516, 522, 527, 532, 538, 543, 549, 555, 560, 566, 572, 577,  583,
    589, 595, 601, 607, 613, 619, 625, 631, 637, 643, 649, 656, 662, 668,  675,
    681, 688, 694, 701, 708, 714, 721, 728, 735, 741, 748, 755, 762, 769,  776,
    784, 791, 798, 805, 813, 820, 828, 835, 843, 850, 858, 866, 874, 881,  889,
    897, 905, 913, 921, 929, 938, 946, 954, 963, 971, 980, 988, 997, 1005, 1014,
    1023};

PicoLedMatrix::PicoLedMatrix() {
  // rgb565 > rgb888 "fast" pixel conversion
  init_rgb_tables();

  s_hub75 = new pimoroni::Hub75(
      TOTAL_WIDTH, PANEL_HEIGHT, nullptr, pimoroni::PANEL_FM6126A, false,
      static_cast<pimoroni::Hub75::COLOR_ORDER>(color_order[rgbMode]),
      lut_table);
  s_hub75->start(dma_complete);
}

PicoLedMatrix::~PicoLedMatrix() {
  s_hub75->stop(dma_complete);
  delete s_hub75;
}

void IRAM_ATTR PicoLedMatrix::DrawPixel(const uint16_t x, const uint16_t y,
                                        const uint8_t r, const uint8_t g,
                                        const uint8_t b) {
  s_hub75->set_pixel(x, y + yOffset, r, g, b);
}

void IRAM_ATTR PicoLedMatrix::DrawPixel(const uint16_t x, const uint16_t y,
                                        const uint16_t color) {
  s_hub75->set_pixel(x, y + yOffset, r5_to_8[(color >> 11) & 0x1F],
                     g6_to_8[(color >> 5) & 0x3F], b5_to_8[color & 0x1F]);
}

void PicoLedMatrix::ClearScreen() { s_hub75->clear(); }

void PicoLedMatrix::SetBrightness(const uint8_t level) {
  s_hub75->brightness = level;
}

void PicoLedMatrix::Render() { s_hub75->render(); }
#endif  // PICO_BUILD
