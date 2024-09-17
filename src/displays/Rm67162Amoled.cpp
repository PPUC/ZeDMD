#ifdef DISPLAY_RM67162_AMOLED
#include "Rm67162Amoled.h"
#include "displayConfig.h"
#include "fonts/tiny4x6.h"

///// LILYGO S3 AMOLED DRIVER
///// No 24 BIT rendering supported, internally everything will be decoded to 16 bit.

Rm67162Amoled::Rm67162Amoled() : tft(), sprite(&tft), zoneSprite(&tft)  {
  
  // On a LilyGo S3 Amoled V2 we need to enable these pins, 
  // but on V1 we want to disable this; because of the nasty green led
  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);

  // Sprite for fullscreen stuff
  sprite.createSprite(536, 240);
  sprite.setSwapBytes(1);

  // Sprite for small screen updates
  zoneSprite.createSprite(ZONE_WIDTH * DISPLAY_SCALE, ZONE_HEIGHT * DISPLAY_SCALE);
  zoneSprite.setSwapBytes(1);
  
  rm67162_init();
  lcd_setRotation(1);
}

void Rm67162Amoled::DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                               uint8_t b) {
  // AMOLED works with 16 bit only; 24 bit gets converted
  uint16_t color =
      sprite.color565(r, g, b);

  sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE,
                  DISPLAY_SCALE, color);
}

void Rm67162Amoled::DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
  sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE,
                  DISPLAY_SCALE, color);
}

void Rm67162Amoled::ClearScreen() {
  sprite.fillSprite(TFT_BLACK);
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

void Rm67162Amoled::SetBrightness(uint8_t level) {
  lcd_brightness(lumval[level]);
}

void Rm67162Amoled::FillScreen(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t color = sprite.color565(r,g,b);
  sprite.fillScreen(color);
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

void Rm67162Amoled::DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r, uint8_t g,
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

void IRAM_ATTR Rm67162Amoled::FillZoneRaw(uint8_t idx, uint8_t *pBuffer) {
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

void IRAM_ATTR Rm67162Amoled::FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) {
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

void IRAM_ATTR Rm67162Amoled::FillPanelRaw(uint8_t *pBuffer) {
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

void Rm67162Amoled::FillPanelUsingPalette(uint8_t *pBuffer, uint8_t *palette) {
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
void Rm67162Amoled::FillPanelUsingChangedPalette(uint8_t *pBuffer, uint8_t *palette, bool *paletteAffected) {
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


Rm67162Amoled::~Rm67162Amoled() {
  // Clean up resources if necessary
}
#endif