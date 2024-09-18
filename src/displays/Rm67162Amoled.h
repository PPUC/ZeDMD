#ifndef AMOLED_H
#define AMOLED_H

#include "displayDriver.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include "rm67162.h"
#include "panel.h"  // Include ZeDMD panel constants

// Scale pixels 4x for almost full display coverage
#define DISPLAY_SCALE 4

#define AMOLED_SCALE_MODE 3 // 1 = 4x4 pixel blocks
                            // 2 = 2x2 pixel blocks, other pixels black (DMD style)
                            // 3 = 3x3 pixel blocks, other pixels black (DMD style #2)

class Rm67162Amoled : public DisplayDriver {
private:
  TFT_eSPI tft;
  TFT_eSprite sprite;
  const uint8_t lumval[16] = {0,  50,  66,  82,  98,  114,  130,  146,  
                            162, 178, 194, 210, 226, 242, 250, 254};
public:
    Rm67162Amoled();
    void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);
    void DrawPixel(uint16_t x, uint16_t y, uint16_t color);
    void ClearScreen();
    void SetBrightness(uint8_t level);
    void FillScreen(uint8_t r, uint8_t g, uint8_t b);
    void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                 uint8_t b, bool transparent = false, bool inverted = false);
    void FillZoneRaw(uint8_t idx, uint8_t *pBuffer);
    void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer);
    void FillPanelRaw(uint8_t *pBuffer);
    void FillPanelUsingPalette(uint8_t *pBuffer, uint8_t *palette);
#if !defined(ZEDMD_WIFI)
    void FillPanelUsingChangedPalette(uint8_t *pBuffer, uint8_t *palette, bool *paletteAffected); 
#endif

    ~Rm67162Amoled();
};

#endif // AMOLED_H