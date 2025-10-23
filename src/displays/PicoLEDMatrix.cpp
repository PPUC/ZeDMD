#ifdef DISPLAY_PICO_LED_MATRIX

#include <cstring>
#include <SerialUART.h>
#include <hardware/vreg.h>
#include "PicoLEDMatrix.h"
#include "fonts/tiny4x6.h"
#include "pico/hub75.hpp"
#include "pico/zedmd_pico.h"

static Hub75 *s_hub75;

// interrupt callback required function
static void __isr dma_complete() {
    //Serial1.println("dma_complete");
    s_hub75->dma_complete();
}

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
#if 0
    Serial1.setTX(16);
    Serial1.setRX(17);
    Serial1.begin();
    Serial1.println("PicoLedMatrix");
#endif

    // tested working on a lot of different devices
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    busy_wait_at_least_cycles((SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US * static_cast<uint64_t>(XOSC_HZ)) / 1000000);
    set_sys_clock_khz(266000, false);

    // rgb565 > rgb888 "fast" pixel conversion
    init_rgb_tables();

    // TODO: handle rgbOrder/rgbMode
    s_hub75 = new Hub75(TOTAL_WIDTH, PANEL_HEIGHT, nullptr,
                        PANEL_FM6126A, false, static_cast<Hub75::COLOR_ORDER>(color_order[rgbMode]));
    s_hub75->start(dma_complete);
}

bool PicoLedMatrix::HasScalingModes() {
    return false; // This display does not support subpixel scaling
}

const char **PicoLedMatrix::GetScalingModes() { return nullptr; }

uint8_t PicoLedMatrix::GetScalingModeCount() { return 0; }

uint8_t PicoLedMatrix::GetCurrentScalingMode() { return 0; }

void PicoLedMatrix::SetCurrentScalingMode(uint8_t mode) {
}

void PicoLedMatrix::DrawPixel(const uint16_t x, const uint16_t y, const uint8_t r, const uint8_t g, const uint8_t b) {
    s_hub75->set_pixel(x, y + yOffset, r, g, b);
}

void PicoLedMatrix::DrawPixel(const uint16_t x, const uint16_t y, const uint16_t color) {
    //Serial1.println("DrawPixel(color)");
    s_hub75->set_pixel(x, y + yOffset,
                       r5_to_8[(color >> 11) & 0x1F],
                       g6_to_8[(color >> 5) & 0x3F],
                       b5_to_8[color & 0x1F]);
}

void PicoLedMatrix::ClearScreen() {
    //Serial1.println("ClearScreen");
    s_hub75->clear();
}

void PicoLedMatrix::SetBrightness(const uint8_t level) {
    // TODO: verify this (compare with an "esp board" ?
    s_hub75->set_brightness(level * 2);
}

void PicoLedMatrix::FillScreen(const uint8_t r, const uint8_t g, const uint8_t b) {
    //Serial1.println("FillScreen");
    for (auto x = 0; x < TOTAL_WIDTH; x++) {
        for (auto y = 0; y < TOTAL_HEIGHT; y++) {
            s_hub75->set_pixel(x, y, r, g, b);
        }
    }
}

void PicoLedMatrix::DisplayText(const char *text, const uint16_t x, const uint16_t y,
                                const uint8_t r, const uint8_t g, const uint8_t b,
                                const bool transparent, const bool inverted) {
    //Serial1.printf("PicoLedMatrix::DisplayText: %s\r\n", text);
    for (uint8_t ti = 0; ti < strlen(text); ti++) {
        for (uint8_t tj = 0; tj <= 5; tj++) {
            const uint8_t fourPixels = getFontLine(text[ti], tj);
            for (uint8_t pixel = 0; pixel < 4; pixel++) {
                bool p = (fourPixels >> (3 - pixel)) & 0x1;
                if (inverted) {
                    p = !p;
                }
                if (transparent && !p) {
                    continue;
                }
                s_hub75->set_pixel(x + pixel + (ti * 4), y + yOffset + tj, r * p, g * p, b * p);
            }
        }
    }
}

void IRAM_ATTR PicoLedMatrix::FillZoneRaw(const uint8_t idx, uint8_t *pBuffer) {
    //Serial1.println("FillZoneRaw");
    const uint8_t zoneYOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
    const uint8_t zoneXOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

    for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
        for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
            const uint16_t pos = (y * ZONE_WIDTH + x) * 3;
            s_hub75->set_pixel(x + zoneXOffset, y + zoneYOffset + yOffset,
                               pBuffer[pos], pBuffer[pos + 1], pBuffer[pos + 2]);
        }
    }
}

void IRAM_ATTR PicoLedMatrix::FillZoneRaw565(const uint8_t idx, uint8_t *pBuffer) {
    //Serial1.println("PicoLedMatrix::FillZoneRaw565");
    const uint8_t zoneYOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
    const uint8_t zoneXOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

    for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
        for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
            const uint16_t pos = (y * ZONE_WIDTH + x) * 2;
            const uint16_t c = static_cast<uint16_t>(pBuffer[pos + 1] << 8) + pBuffer[pos];
            s_hub75->set_pixel(x + zoneXOffset, y + zoneYOffset + yOffset,
                               r5_to_8[(c >> 11) & 0x1F], g6_to_8[(c >> 5) & 0x3F], b5_to_8[c & 0x1F]);
        }
    }
}

void IRAM_ATTR PicoLedMatrix::ClearZone(const uint8_t idx) {
    //Serial1.println("PicoLedMatrix::ClearZone");
    const uint8_t zoneYOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT;
    const uint8_t zoneXOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH;

    for (uint8_t y = 0; y < ZONE_HEIGHT; y++) {
        for (uint8_t x = 0; x < ZONE_WIDTH; x++) {
            s_hub75->set_pixel(x + zoneXOffset, y + zoneYOffset + yOffset, 0, 0, 0);
        }
    }
}

void IRAM_ATTR PicoLedMatrix::FillPanelRaw(uint8_t *pBuffer) {
    //Serial1.println("PicoLedMatrix::FillPanelRaw");

    for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
        for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
            const uint16_t pos = (y * TOTAL_WIDTH + x) * 3;
            s_hub75->set_pixel(x, y + yOffset, pBuffer[pos], pBuffer[pos + 1], pBuffer[pos + 2]);
        }
    }
}

PicoLedMatrix::~PicoLedMatrix() {
    //Serial1.println("PicoLedMatrix::PicoLedMatrix");
    s_hub75->stop(dma_complete);
    delete s_hub75;
}

#endif  // DISPLAY_PICO_LED_MATRIX
