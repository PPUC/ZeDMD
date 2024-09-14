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
    virtual void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r, uint8_t g, 
                 uint8_t b, bool transparent = false, bool inverted = false) = 0;
    virtual void FillZoneRaw(uint8_t idx, uint8_t *pBuffer) = 0;
    virtual void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) = 0;
    virtual void FillPanelRaw(uint8_t *pBuffer) = 0;
    virtual void FillPanelUsingPalette(uint8_t *pBuffer, uint8_t *palette) = 0;
#if !defined(ZEDMD_WIFI)
    virtual void FillPanelUsingChangedPalette(uint8_t *pBuffer, uint8_t *palette, bool *paletteAffected) = 0; 
#endif
    
    virtual ~DisplayDriver() {} // Virtual destructor
};

#endif // DISPLAYDRIVER_H