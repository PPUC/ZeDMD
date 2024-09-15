#ifndef AMOLED_H
#define AMOLED_H

#include "displayDriver.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include "rm67162.h"
#include "panel.h"  // Include ZeDMD panel constants

// Scale pixels 4x for almost full display coverage
#define DISPLAY_SCALE 4

class LilygoS3Amoled : public DisplayDriver {
private:
  TFT_eSPI tft;
  TFT_eSprite sprite;
  TFT_eSprite zoneSprite;
  const uint8_t lumval[16] = {0,  50,  66,  82,  98,  114,  130,  146,  
                            162, 178, 194, 210, 226, 242, 250, 254};
public:
    LilygoS3Amoled();
    virtual void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) override;
    virtual void DrawPixel(uint16_t x, uint16_t y, uint16_t color) override;
    virtual void ClearScreen() override;
    virtual void SetBrightness(uint8_t level) override;
    virtual void FillScreen(uint8_t r, uint8_t g, uint8_t b) override;
    virtual void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                 uint8_t b, bool transparent = false, bool inverted = false) override;
    virtual void FillZoneRaw(uint8_t idx, uint8_t *pBuffer) override;
    virtual void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) override;
    virtual void FillPanelRaw(uint8_t *pBuffer) override;
    virtual void FillPanelUsingPalette(uint8_t *pBuffer, uint8_t *palette) override;
#if !defined(ZEDMD_WIFI)
    virtual void FillPanelUsingChangedPalette(uint8_t *pBuffer, uint8_t *palette, bool *paletteAffected) override; 
#endif

    ~LilygoS3Amoled();
};

#endif // AMOLED_H