#ifndef AMOLED_H
#define AMOLED_H

#include "displayDriver.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include "rm67162.h"

// Scale pixels 4x for almost full display coverage
#define SCALING_FACTOR 4

class LilygoS3Amoled : public DisplayDriver {
private:
  TFT_eSPI tft;
  TFT_eSprite sprite;
  TFT_eSprite sprite2;
  const uint8_t lumval[16] = {0,  10,  20,  30,  40,  50,  60,  80,
                                100, 120, 140, 160, 180, 200, 225, 254};
public:
    LilygoS3Amoled();
    virtual void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) override;
    virtual void DrawPixel(uint16_t x, uint16_t y, uint16_t color) override;
    virtual void ClearScreen() override;
    virtual void SetBrightness(uint8_t level) override;
    virtual void FillScreen(uint8_t r, uint8_t g, uint8_t b) override;
    virtual void UpdateDisplay() override;
    virtual void UpdateDisplayZone(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;

    ~LilygoS3Amoled();
};

#endif // AMOLED_H