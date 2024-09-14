#ifdef DISPLAY_LILYGO_S3_AMOLED
#include "LilygoS3Amoled.h"
#include "displayConfig.h"
#include "fonts/tiny4x6.h"

///// LILYGO S3 AMOLED DRIVER
///// No 24 BIT rendering supported, internally everything will be decoded to 16 bit.

/// @brief Intialization stuff
LilygoS3Amoled::LilygoS3Amoled() : tft(), sprite(&tft), zoneSprite(&tft)  {

  // Sprite for fullscreen stuff
  sprite.createSprite(536, 240);
  sprite.setSwapBytes(1);

  // Sprite for small screen updates
  zoneSprite.createSprite(ZONE_WIDTH * DISPLAY_SCALE, ZONE_HEIGHT * DISPLAY_SCALE);
  zoneSprite.setSwapBytes(1);
  
  rm67162_init();
  lcd_setRotation(1);
}

/// @brief Draw a scaled RGB888 converted to RGB565 pixel (default 4x4)
/// @param x X coordinate
/// @param y Y coordinate
/// @param r 8 bit red color
/// @param g 8 bit green color
/// @param b 8 bit blue color
void LilygoS3Amoled::DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                               uint8_t b) {
  // AMOLED works with 16 bit only; 24 bit gets converted
  uint16_t color =
      sprite.color565(r, g, b);

  sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE,
                  DISPLAY_SCALE, color);
}

/// @brief Draw a scaled RGB565 pixel (default 4x4)
/// @param x X coordinate
/// @param y Y coordinate
/// @param color 16 bit RGB565 color
void LilygoS3Amoled::DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
  sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE,
                  DISPLAY_SCALE, color);
}

/// @brief Clear screen
void LilygoS3Amoled::ClearScreen() {
  sprite.fillSprite(TFT_BLACK);
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

/// @brief Set brightness of display
/// @param level 0-15 levels
void LilygoS3Amoled::SetBrightness(uint8_t level) {
  lcd_brightness(lumval[level]);
}

/// @brief Fill entire screen with one color
/// @param r 8 bit red color
/// @param g 8 bit green color
/// @param b 8 bit blue color
void LilygoS3Amoled::FillScreen(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t color = sprite.color565(r,g,b);
  sprite.fillScreen(color);
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

/// @brief Write scaled text to display
/// @param text string of text
/// @param x X coordinate
/// @param y Y coordinate
/// @param r 8 bit red color
/// @param g 8 bit green color
/// @param b 8 bit blue color
/// @param transparent background transparent
/// @param inverted colors inverted
void LilygoS3Amoled::DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                 uint8_t b, bool transparent, bool inverted) {
  for (uint8_t ti = 0; ti < strlen(text); ti++) {
    for (uint8_t tj = 0; tj <= 5; tj++) {
      uint8_t fourPixels = getFontLine(text[ti], tj);
      for (uint8_t pixel = 0; pixel < 4; pixel++) {
        bool p = (fourPixels >> (3 - pixel)) & 0x1;
        if (inverted) {
          p = !p;
        }
        if (transparent && !p) {
          continue;
        }
        uint16_t color = sprite.color565(r*p, g*p, b*p);

        sprite.fillRect((x + pixel + (ti * 4)) * DISPLAY_SCALE, (y + tj) * DISPLAY_SCALE, DISPLAY_SCALE, DISPLAY_SCALE, color);
      }
    }
  }
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

/// @brief RGB888 24bit Zone fill
/// @param idx index
/// @param pBuffer buffer with pixel data [R,G,B]
/// @return 
void IRAM_ATTR LilygoS3Amoled::FillZoneRaw(uint8_t idx, uint8_t *pBuffer) {
  uint16_t yOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT * DISPLAY_SCALE;
  uint16_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH * DISPLAY_SCALE;

  for (uint16_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint16_t x = 0; x < ZONE_WIDTH; x++) {
      uint16_t pos = (y * ZONE_WIDTH + x) * 3;

        uint16_t color =
          zoneSprite.color565(pBuffer[pos], pBuffer[pos + 1], pBuffer[pos + 2]);

      // Draw the 4x4 block as a rectangle
      zoneSprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE, DISPLAY_SCALE, color);
    }
  }
  lcd_PushColors(xOffset, yOffset, ZONE_WIDTH * DISPLAY_SCALE, ZONE_HEIGHT * DISPLAY_SCALE, (uint16_t *)zoneSprite.getPointer());
}

/// @brief RGB565 16 bit Zone Fill
/// @param idx index
/// @param pBuffer buffer with pixel data 16 bits
/// @return 
void IRAM_ATTR LilygoS3Amoled::FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) {
   uint16_t yOffset = (idx / ZONES_PER_ROW) * ZONE_HEIGHT * DISPLAY_SCALE;
  uint16_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH * DISPLAY_SCALE;

  for (uint16_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint16_t x = 0; x < ZONE_WIDTH; x++) {
      uint16_t pos = (y * ZONE_WIDTH + x) * 2;

      uint16_t color = ((((uint16_t)pBuffer[pos + 1]) << 8) + pBuffer[pos]);

      zoneSprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE, DISPLAY_SCALE, color);
    }
  }
  lcd_PushColors(xOffset, yOffset, ZONE_WIDTH * DISPLAY_SCALE, ZONE_HEIGHT * DISPLAY_SCALE,
                 (uint16_t *)zoneSprite.getPointer());
}

/// @brief Fill fullscreen with current renderBuffer
/// @return 
void IRAM_ATTR LilygoS3Amoled::FillPanelRaw(uint8_t *pBuffer) {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = (y * TOTAL_WIDTH + x) * 3;

      uint16_t color = sprite.color565(pBuffer[pos], pBuffer[pos + 1], pBuffer[pos + 2]);

      sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE, DISPLAY_SCALE, color);
    }
  }
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

/// @brief Fill fullscreen with palette
/// @return 
void LilygoS3Amoled::FillPanelUsingPalette(uint8_t *pBuffer, uint8_t *palette) {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = pBuffer[y * TOTAL_WIDTH + x] * 3;

      uint16_t color =
          sprite.color565(palette[pos], palette[pos + 1], palette[pos + 2]);

      sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE, DISPLAY_SCALE, color);
    }
  }
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

#if !defined(ZEDMD_WIFI)
/// @brief Fill fullscreen with changed palette
/// @param pBuffer Pixel buffer RGB888
/// @param palette Palette
/// @param paletteAffected Affected palette 
void LilygoS3Amoled::FillPanelUsingChangedPalette(uint8_t *pBuffer, uint8_t *palette, bool *paletteAffected) {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = pBuffer[y * TOTAL_WIDTH + x];
      if (paletteAffected[pos]) {
        pos *= 3;

        uint16_t color =
          sprite.color565(palette[pos], palette[pos + 1], palette[pos + 2]);

        sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE, DISPLAY_SCALE, color);
      }
    }
  }
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}
#endif


LilygoS3Amoled::~LilygoS3Amoled() {
  // Clean up resources if necessary
}
#endif