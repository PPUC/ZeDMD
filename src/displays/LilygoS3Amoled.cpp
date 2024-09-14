#ifdef DISPLAY_LILYGO_S3_AMOLED
#include "LilygoS3Amoled.h"



LilygoS3Amoled::LilygoS3Amoled() : tft(), sprite(&tft), sprite2(&tft) {
  // AMOLED-specific initialization

  sprite.createSprite(536, 240);
  sprite.setSwapBytes(1);
  sprite2.createSprite(8 * 4, 4 * 4);
  sprite2.setSwapBytes(1);

  rm67162_init();
  lcd_setRotation(1);
}

void LilygoS3Amoled::DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                               uint8_t b) {
  // AMOLED works with 16 bit only; 24 bit gets converted
  uint16_t color =
      sprite.color565(r, g, b);

  sprite.fillRect(x * SCALING_FACTOR, y * SCALING_FACTOR, SCALING_FACTOR,
                  SCALING_FACTOR, color);
}

void LilygoS3Amoled::DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
  sprite.fillRect(x * SCALING_FACTOR, y * SCALING_FACTOR, SCALING_FACTOR,
                  SCALING_FACTOR, color);
}

void LilygoS3Amoled::ClearScreen() {
  sprite.fillSprite(TFT_BLACK);
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

void LilygoS3Amoled::SetBrightness(uint8_t level) {
  lcd_brightness(lumval[level]);
}

void LilygoS3Amoled::FillScreen(uint8_t r, uint8_t g, uint8_t b) {
  // AMOLED-specific fill screen
  
}

void LilygoS3Amoled::UpdateDisplay() {
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

void LilygoS3Amoled::UpdateDisplayZone(uint16_t x, uint16_t y, uint16_t w,
                                       uint16_t h) {
  lcd_PushColors(x, y, w, h, (uint16_t *)sprite.getPointer());
}

LilygoS3Amoled::~LilygoS3Amoled() {
  // Clean up resources if necessary
}
#endif