#ifndef DISPLAYDRIVER_H
#define DISPLAYDRIVER_H

#include <stdint.h>



class DisplayDriver {
public:
    /// @brief Draw a RGB888 pixel
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param r 8 bit red color
    /// @param g 8 bit green color
    /// @param b 8 bit blue color
    virtual void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b) = 0;

    /// @brief Draw a RGB565 pixel
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param color 16 bit RGB565 color
    virtual void DrawPixel(uint16_t x, uint16_t y, uint16_t color) = 0;

    /// @brief Clear screen
    virtual void ClearScreen() = 0;

    /// @brief Set brightness of display
    /// @param level 0-15 levels   
    virtual void SetBrightness(uint8_t level) = 0;
    
    /// @brief Fill entire screen with one color
    /// @param r 8 bit red color
    /// @param g 8 bit green color
    /// @param b 8 bit blue color
    virtual void FillScreen(uint8_t r, uint8_t g, uint8_t b) = 0;
    
    /// @brief Write text to display
    /// @param text string of text
    /// @param x X coordinate
    /// @param y Y coordinate
    /// @param r 8 bit red color
    /// @param g 8 bit green color
    /// @param b 8 bit blue color
    /// @param transparent background transparent
    /// @param inverted colors inverted
    virtual void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r, uint8_t g, 
                 uint8_t b, bool transparent = false, bool inverted = false) = 0;

    /// @brief RGB888 24bit Zone fill
    /// @param idx index
    /// @param pBuffer buffer with pixel data [R,G,B]
    /// @return 
    virtual void FillZoneRaw(uint8_t idx, uint8_t *pBuffer) = 0;

    /// @brief RGB565 16 bit Zone Fill
    /// @param idx index
    /// @param pBuffer buffer with pixel data 16 bits
    /// @return 
    virtual void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) = 0;

    /// @brief Fill fullscreen with current renderBuffer
    /// @return 
    virtual void FillPanelRaw(uint8_t *pBuffer) = 0;

    /// @brief Fill fullscreen with pallete
    /// @return 
    virtual void FillPanelUsingPalette(uint8_t *pBuffer, uint8_t *palette) = 0;
#if !defined(ZEDMD_WIFI)
    /// @brief Fill fullscreen with changed palette
    /// @param pBuffer Pixel buffer RGB888
    /// @param palette Palette
    /// @param paletteAffected Affected palette 
    virtual void FillPanelUsingChangedPalette(uint8_t *pBuffer, uint8_t *palette, bool *paletteAffected) = 0; 
#endif
    
    virtual ~DisplayDriver() {} // Virtual destructor
};

#endif // DISPLAYDRIVER_H