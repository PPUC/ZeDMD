#ifndef AMOLED_H
#define AMOLED_H

#include "DisplayDriver.h"

class LilygoS3Amoled : public DisplayDriver {
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