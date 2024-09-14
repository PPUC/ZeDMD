#ifndef DISPLAYDRIVER_H
#define DISPLAYDRIVER_H

#include <stdint.h>

class DisplayDriver {
public:
    virtual void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void DrawPixel(uint16_t x, uint16_t y, uint16_t color) = 0;
    virtual void ClearScreen() = 0;
    virtual void SetBrightness(uint8_t level) = 0;
    virtual void FillScreen(uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void UpdateDisplay() = 0;
    virtual void UpdateDisplayZone(uint16_t x, uint16_t y, uint16_t w, uint16_t h) = 0;
    
    virtual ~DisplayDriver() {} // Virtual destructor
};

#endif // DISPLAYDRIVER_H