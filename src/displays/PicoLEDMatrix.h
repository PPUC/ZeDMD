#ifndef PICO_LED_MATRIX_H
#define PICO_LED_MATRIX_H

#include "displayDriver.h"
#include "hub75.hpp"
#include "panel.h"  // Include ZeDMD panel constants

extern uint8_t rgbMode;
extern int8_t yOffset;

class PicoLedMatrix final : public DisplayDriver {
public:
    PicoLedMatrix(); // Constructor

    bool HasScalingModes() override;

    const char **GetScalingModes() override;

    uint8_t GetScalingModeCount() override;

    uint8_t GetCurrentScalingMode() override;

    void SetCurrentScalingMode(uint8_t mode) override;

    void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) override;

    void DrawPixel(uint16_t x, uint16_t y, uint16_t color) override;

    void ClearScreen() override;

    void SetBrightness(uint8_t level) override;

    void FillScreen(uint8_t r, uint8_t g, uint8_t b) override;

    void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r,
                     uint8_t g, uint8_t b, bool transparent = false,
                     bool inverted = false) override;

    void FillZoneRaw(uint8_t idx, uint8_t *pBuffer) override;

    void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) override;

    void ClearZone(uint8_t idx) override;

    void FillPanelRaw(uint8_t *pBuffer) override;

    ~PicoLedMatrix() override; // Destructor

private:
    uint8_t color_order[6] = {
        Hub75::COLOR_ORDER::RGB,
        Hub75::COLOR_ORDER::BRG,
        Hub75::COLOR_ORDER::GBR,
        Hub75::COLOR_ORDER::RBG,
        Hub75::COLOR_ORDER::GRB,
        Hub75::COLOR_ORDER::BGR
    };
};

#endif  // PICO_LED_MATRIX_H
