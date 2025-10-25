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
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
    7, 8, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15,
    15, 16, 17, 17, 18, 19, 19, 20, 21, 22, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 42, 43, 44,
    45, 47, 48, 50, 51, 52, 54, 55, 57, 58, 60, 61, 63, 65, 66, 68,
    70, 71, 73, 75, 77, 79, 81, 83, 84, 86, 88, 90, 93, 95, 97, 99,
    101, 103, 106, 108, 110, 113, 115, 118, 120, 123, 125, 128, 130, 133, 136, 138,
    141, 144, 147, 149, 152, 155, 158, 161, 164, 167, 171, 174, 177, 180, 183, 187,
    190, 194, 197, 200, 204, 208, 211, 215, 218, 222, 226, 230, 234, 237, 241, 245,
    249, 254, 258, 262, 266, 270, 275, 279, 283, 288, 292, 297, 301, 306, 311, 315,
    320, 325, 330, 335, 340, 345, 350, 355, 360, 365, 370, 376, 381, 386, 392, 397,
    403, 408, 414, 420, 425, 431, 437, 443, 449, 455, 461, 467, 473, 480, 486, 492,
    499, 505, 512, 518, 525, 532, 538, 545, 552, 559, 566, 573, 580, 587, 594, 601,
    609, 616, 624, 631, 639, 646, 654, 662, 669, 677, 685, 693, 701, 709, 717, 726,
    734, 742, 751, 759, 768, 776, 785, 794, 802, 811, 820, 829, 838, 847, 857, 866,
    875, 885, 894, 903, 913, 923, 932, 942, 952, 962, 972, 982, 992, 1002, 1013, 1023
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
