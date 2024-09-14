#ifndef LEDMATRIX_H
#define LEDMATRIX_H

#ifdef ARDUINO_ESP32_S3_N16R8
#define R1_PIN 4
#define G1_PIN 5
#define B1_PIN 6
#define R2_PIN 7
#define G2_PIN 15
#define B2_PIN 16
#define A_PIN 18
#define B_PIN 8
// #define C_PIN 3
#define C_PIN 46
#define D_PIN 42
#define E_PIN 1  // required for 1/32 scan panels, like 64x64.
#define LAT_PIN 40
#define OE_PIN 2
#define CLK_PIN 41
#else
// Pinout derived from ESP32-HUB75-MatrixPanel-I2S-DMA.h
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 19
#define C_PIN 5
#define D_PIN 17
#define E_PIN \
  22  // required for 1/32 scan panels, like 64x64. Any available pin would do,
      // i.e. IO32. If 1/16 scan panels, no connection to this pin needed
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

#endif

#include "displayDriver.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "panel.h"  // Include ZeDMD panel constants


class LedMatrix : public DisplayDriver {
private:
    MatrixPanel_I2S_DMA* dma_display;
    const uint8_t lumval[16] = {0,  2,  4,  7,  11,  18,  30,  40,
                                50, 65, 80, 100, 125, 160, 200, 255};

public:
    LedMatrix(); // Constructor
    virtual void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) override;
    virtual void DrawPixel(uint16_t x, uint16_t y, uint16_t color) override;
    virtual void ClearScreen() override;
    virtual void SetBrightness(uint8_t level) override;
    virtual void FillScreen(uint8_t r, uint8_t g, uint8_t b) override;
    virtual void UpdateDisplay() override;
    virtual void UpdateDisplayZone(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;
    virtual void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                 uint8_t b, bool transparent = false, bool inverted = false) override;
    virtual void FillZoneRaw(uint8_t idx, uint8_t *pBuffer) override;
    virtual void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) override;
    virtual void FillPanelRaw(uint8_t *pBuffer) override;
    virtual void FillPanelUsingPalette(uint8_t *pBuffer, uint8_t *palette) override;
#if !defined(ZEDMD_WIFI)
    virtual void FillPanelUsingChangedPalette(uint8_t *pBuffer, uint8_t *palette, bool *paletteAffected) override; 
#endif
    
    ~LedMatrix(); // Destructor
};

#endif // LEDMATRIX_H
