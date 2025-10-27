#ifndef PICO_HUB75_H
#define PICO_HUB75_H

// from https://github.com/pimoroni/pimoroni-pico/blob/main/drivers/hub75

#include <cstdint>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

#ifndef NO_QSTR
#include "hub75.pio.h"
#endif

constexpr uint DATA_BASE_PIN = 0;
constexpr uint DATA_N_PINS = 6;
constexpr uint ROWSEL_BASE_PIN = 6;
constexpr uint ROWSEL_N_PINS = 5;
constexpr uint CLK_PIN = 11;
constexpr uint STB_PIN = 12;
constexpr uint OE_PIN = 13;

constexpr uint BIT_DEPTH = 10;

/*
10-bit gamma table, allowing us to gamma correct our 8-bit colour values up
to 10-bit without losing dynamic range.

Calculated with the following Python code:

gamma_lut = [int(round(1024 * (x / (256 - 1)) ** 2.2)) for x in range(256)]

Post-processed to enforce a minimum difference of 1 between adjacent values,
and no leading zeros:

for i in range(1, len(gamma_lut)):
    if gamma_lut[i] <= gamma_lut[i - 1]:
        gamma_lut[i] = gamma_lut[i - 1] + 1
*/
constexpr uint16_t GAMMA_10BIT[256] = {
    0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 17, 18, 19, 20, 22, 23, 24, 26, 27, 29, 30,
    32, 33, 35, 36, 38, 39, 41, 43, 44, 46, 48, 50, 51, 53, 55, 57,
    59, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 87, 89,
    91, 93, 95, 98, 100, 102, 104, 107, 109, 112, 114, 116, 119, 121, 124, 126,
    129, 131, 134, 136, 139, 142, 144, 147, 150, 152, 155, 158, 161, 163, 166, 169,
    172, 175, 178, 181, 184, 187, 190, 193, 196, 199, 202, 205, 208, 211, 214, 218,
    221, 224, 227, 231, 234, 237, 241, 244, 248, 251, 254, 258, 262, 265, 269, 272,
    276, 280, 283, 287, 291, 295, 298, 302, 306, 310, 314, 318, 322, 326, 330, 334,
    338, 342, 346, 350, 354, 359, 363, 367, 372, 376, 380, 385, 389, 394, 398, 403,
    407, 412, 416, 421, 426, 431, 435, 440, 445, 450, 455, 460, 465, 470, 475, 480,
    485, 490, 495, 500, 506, 511, 516, 522, 527, 532, 538, 543, 549, 555, 560, 566,
    572, 577, 583, 589, 595, 601, 607, 613, 619, 625, 631, 637, 643, 649, 656, 662,
    668, 675, 681, 688, 694, 701, 708, 714, 721, 728, 735, 741, 748, 755, 762, 769,
    776, 784, 791, 798, 805, 813, 820, 828, 835, 843, 850, 858, 866, 874, 881, 889,
    897, 905, 913, 921, 929, 938, 946, 954, 963, 971, 980, 988, 997, 1005, 1014, 1023
};

struct Pixel {
    uint32_t color;

    Pixel() : color(0) {
    };

    Pixel(const uint32_t color) : color(color) {
    };

    Pixel(const uint8_t r, const uint8_t g, const uint8_t b) : color(
        (GAMMA_10BIT[b] << 20) | (GAMMA_10BIT[g] << 10) | GAMMA_10BIT[r]) {
    };
};

Pixel hsv_to_rgb(float h, float s, float v);

enum PanelType {
    PANEL_GENERIC = 0,
    PANEL_FM6126A,
};

class Hub75 {
public:
    enum COLOR_ORDER {
        RGB, RBG, GRB, GBR, BRG, BGR
    };

    uint width;
    uint height;
    uint r_shift = 0;
    uint g_shift = 10;
    uint b_shift = 20;
    Pixel *back_buffer;
    bool managed_buffer = false;
    PanelType panel_type;
    bool inverted_stb = false;
    COLOR_ORDER color_order;

    // DMA & PIO
    int dma_channel = -1;
    uint bit = 0;
    uint row = 0;

    PIO pio = pio0;
    uint sm_data = 0;
    uint sm_row = 1;

    uint data_prog_offs = 0;
    uint row_prog_offs = 0;

    uint brightness = 4;

    // Top half of display - 16 rows on a 32x32 panel
    unsigned int pin_r0 = DATA_BASE_PIN;
    unsigned int pin_g0 = DATA_BASE_PIN + 1;
    unsigned int pin_b0 = DATA_BASE_PIN + 2;

    // Bottom half of display - 16 rows on a 64x64 panel
    unsigned int pin_r1 = DATA_BASE_PIN + 3;
    unsigned int pin_g1 = DATA_BASE_PIN + 4;
    unsigned int pin_b1 = DATA_BASE_PIN + 5;

    // Address pins, 5 lines = 2^5 = 32 values (max 64x64 display)
    unsigned int pin_row_a = ROWSEL_BASE_PIN;
    unsigned int pin_row_b = ROWSEL_BASE_PIN + 1;
    unsigned int pin_row_c = ROWSEL_BASE_PIN + 2;
    unsigned int pin_row_d = ROWSEL_BASE_PIN + 3;
    unsigned int pin_row_e = ROWSEL_BASE_PIN + 4;

    // Sundry things
    unsigned int pin_clk = CLK_PIN; // Clock
    unsigned int pin_stb = STB_PIN; // Strobe/Latch
    unsigned int pin_oe = OE_PIN; // Output Enable

    const bool clk_polarity = true;
    const bool stb_polarity = true;
    const bool oe_polarity = false;

    Hub75(const uint width, const uint height) : Hub75(width, height, nullptr) {
    };

    Hub75(const uint width, const uint height, Pixel *buffer)
        : Hub75(width, height, buffer, PANEL_GENERIC) {
    };

    Hub75(const uint width, const uint height, Pixel *buffer, const PanelType panel_type) : Hub75(
        width, height, buffer, panel_type, false) {
    };

    Hub75(uint width, uint height, Pixel *buffer, PanelType panel_type, bool inverted_stb,
          COLOR_ORDER color_order = COLOR_ORDER::RGB);

    ~Hub75();

    void FM6126A_write_register(uint16_t value, uint8_t position);

    void FM6126A_setup();

    void set_brightness(const uint8_t value) {
        brightness = value;
    }

    void set_color(uint x, uint y, Pixel c);

    void set_pixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b);

    void copy_to_back_buffer(void *data, size_t len, int start_x, int start_y, int g_width, int g_height);

    void clear();

    void start(irq_handler_t handler);

    void stop(irq_handler_t handler);

    void dma_complete();
};

#endif // PICO_HUB75_H
