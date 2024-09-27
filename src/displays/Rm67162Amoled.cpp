#ifdef DISPLAY_RM67162_AMOLED
#include "Rm67162Amoled.h"

#include "displayConfig.h"
#include "fonts/tiny4x6.h"

///// LILYGO S3 AMOLED DRIVER
///// No 24 BIT rendering supported, internally everything will be decoded to 16
///bit.

Rm67162Amoled::Rm67162Amoled() : tft(), sprite(&tft), currentScalingMode(0) {
  // On a LilyGo S3 Amoled V2 we need to enable these pins,
  // but on V1 we want to disable this; because of the nasty green led
  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);

  // Sprite for fullscreen stuff
  sprite.createSprite(536, 240);
  sprite.setSwapBytes(1);

  rm67162_init();
  lcd_setRotation(1);
}

const char* Rm67162Amoled::scalingModes[4] = { "4x4 square pixels (4x Upscale)", 
                                                "2x2 square pixels (DMD Effect #1)", 
                                                "3x3 square pixels (DMD Effect #2)", 
                                                "Argyle (Diamond) pixels (DMD Effect #3)" };



bool Rm67162Amoled::HasScalingModes() {
  return true; //This display supports subpixel scaling  
}

const char** Rm67162Amoled::GetScalingModes()  {
  return scalingModes;  // Return the static array of mode names
}

uint8_t Rm67162Amoled::GetScalingModeCount() {
  return sizeof(scalingModes) / sizeof(scalingModes[0]);  // Return the number of available scaling modes
}

uint8_t Rm67162Amoled::GetCurrentScalingMode() {
  return currentScalingMode;
}

void Rm67162Amoled::SetCurrentScalingMode(uint8_t mode) {
  if (mode >= 0 && mode <= 3) {  // Ensure the mode is within valid range
    currentScalingMode = mode;
  }
}

void Rm67162Amoled::DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                              uint8_t b) {
  // AMOLED works with 16 bit only; 24 bit gets converted
  uint16_t color = sprite.color565(r, g, b);

  sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE + DISPLAY_Y_OFFSET, DISPLAY_SCALE,
                  DISPLAY_SCALE, color);
}

void Rm67162Amoled::DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
  sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE + DISPLAY_Y_OFFSET, DISPLAY_SCALE,
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
  uint16_t color = sprite.color565(r, g, b);
  sprite.fillScreen(color);
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

void Rm67162Amoled::DisplayText(const char *text, uint16_t x, uint16_t y,
                                uint8_t r, uint8_t g, uint8_t b,
                                bool transparent, bool inverted) {
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
        uint16_t color = sprite.color565(r * p, g * p, b * p);

        sprite.fillRect((x + pixel + (ti * 4)) * DISPLAY_SCALE,
                        (y + tj) * DISPLAY_SCALE + DISPLAY_Y_OFFSET, DISPLAY_SCALE, DISPLAY_SCALE,
                        color);
      }
    }
  }
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

// Speed optimized version
void IRAM_ATTR Rm67162Amoled::FillZoneRaw(uint8_t idx, uint8_t *pBuffer) {
  uint16_t yOffset = ((idx / ZONES_PER_ROW) * ZONE_HEIGHT * DISPLAY_SCALE) + DISPLAY_Y_OFFSET;
  uint16_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH * DISPLAY_SCALE;

  // Buffer to store the pixel data in byte format for SPI (2 bytes per pixel)
  uint8_t
      pixelBuffer[ZONE_WIDTH * ZONE_HEIGHT * DISPLAY_SCALE * DISPLAY_SCALE * 2];
  uint16_t bufferIndex = 0;

  for (uint16_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint16_t x = 0; x < ZONE_WIDTH; x++) {
      // Extract the RGB888 color and convert to RGB565
      uint16_t pos = (y * ZONE_WIDTH + x) * 3;
      uint16_t color = ((pBuffer[pos] & 0xF8) << 8) |
                       ((pBuffer[pos + 1] & 0xFC) << 3) |
                       (pBuffer[pos + 2] >> 3);
      uint16_t black = 0x0000;  // Black in RGB565 format (0x0000)

      // Precompute pixel block colors based on currentScalingMode
      uint16_t drawColors[DISPLAY_SCALE][DISPLAY_SCALE];

      switch (currentScalingMode) {
        case 0:
          // 4x4 all pixels are color
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              drawColors[i][j] = color;
            }
          }
          break;
        case 1:
          // 2x2 center pixels in color, rest black
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              if (i > 0 && i < DISPLAY_SCALE - 1 && j > 0 &&
                  j < DISPLAY_SCALE - 1) {
                drawColors[i][j] = color;
              } else {
                drawColors[i][j] = black;
              }
            }
          }
          break;
        case 2:
          // 3x3 center pixels in color, rest black
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              if (i > 0 && i < DISPLAY_SCALE && j > 0 && j < DISPLAY_SCALE) {
                drawColors[i][j] = color;
              } else {
                drawColors[i][j] = black;
              }
            }
          }
          break;
        case 3:
          // Argyle diamond pattern
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              if ((i == j) || (i + j == DISPLAY_SCALE - 1)) {
                // If it's a diagonal in the grid, color it (argyle/diamond
                // pattern)
                drawColors[i][j] = color;
              } else {
                drawColors[i][j] = black;
              }
            }
          }
          break;
        default:
          // Fallback to normal 4x4 block
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              drawColors[i][j] = color;
            }
          }
          break;
      }

      // Scale the block according to DISPLAY_SCALE and position correctly in
      // pixelBuffer
      for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
        for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
          // Correctly place the block in the buffer by considering scaling and
          // offsets
          uint16_t scaledX = x * DISPLAY_SCALE + i;  // Scaled x-coordinate
          uint16_t scaledY = y * DISPLAY_SCALE + j;  // Scaled y-coordinate

          // Calculate the index in the buffer based on scaled coordinates
          uint16_t pixelPos =
              (scaledY * ZONE_WIDTH * DISPLAY_SCALE + scaledX) * 2;

          // Store the color in pixel buffer (in bytes)
          pixelBuffer[pixelPos] = (drawColors[i][j] >> 8) & 0xFF;  // High byte
          pixelBuffer[pixelPos + 1] = drawColors[i][j] & 0xFF;     // Low byte
        }
      }
    }
  }

  // Push the formatted pixel buffer to the display
  lcd_PushColors(xOffset, yOffset, ZONE_WIDTH * DISPLAY_SCALE,
                 ZONE_HEIGHT * DISPLAY_SCALE, (uint16_t *)pixelBuffer);
}

// Speed optimized version
void IRAM_ATTR Rm67162Amoled::FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) {
  uint16_t yOffset = ((idx / ZONES_PER_ROW) * ZONE_HEIGHT * DISPLAY_SCALE) + DISPLAY_Y_OFFSET;
  uint16_t xOffset = (idx % ZONES_PER_ROW) * ZONE_WIDTH * DISPLAY_SCALE;

  // Buffer to store the pixel data in byte format for SPI (2 bytes per pixel)
  uint8_t
      pixelBuffer[ZONE_WIDTH * ZONE_HEIGHT * DISPLAY_SCALE * DISPLAY_SCALE * 2];
  uint16_t bufferIndex = 0;

  for (uint16_t y = 0; y < ZONE_HEIGHT; y++) {
    for (uint16_t x = 0; x < ZONE_WIDTH; x++) {
      // Extract the color in RGB565 format from the pBuffer
      uint16_t pos = (y * ZONE_WIDTH + x) * 2;
      uint16_t color = ((((uint16_t)pBuffer[pos + 1]) << 8) + pBuffer[pos]);
      uint16_t black = 0x0000;  // Black in RGB565 format (0x0000)

      // Precompute pixel block colors based on currentScalingMode
      uint16_t drawColors[DISPLAY_SCALE][DISPLAY_SCALE];

      switch (currentScalingMode) {
        case 0:
          // 4x4 all pixels are color
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              drawColors[i][j] = color;
            }
          }
          break;
        case 1:
          // 2x2 center pixels in color, rest black
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              if (i > 0 && i < DISPLAY_SCALE - 1 && j > 0 &&
                  j < DISPLAY_SCALE - 1) {
                drawColors[i][j] = color;
              } else {
                drawColors[i][j] = black;
              }
            }
          }
          break;
        case 2: 
          // 3x3 center pixels in color, rest black
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              if (i > 0 && i < DISPLAY_SCALE && j > 0 && j < DISPLAY_SCALE) {
                drawColors[i][j] = color;
              } else {
                drawColors[i][j] = black;
              }
            }
          }
          break;
        case 3:
          // Argyle diamond pattern
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              if ((i == j) || (i + j == DISPLAY_SCALE - 1)) {
                // If it's a diagonal in the grid, color it (argyle/diamond
                // pattern)
                drawColors[i][j] = color;
              } else {
                drawColors[i][j] = black;
              }
            }
          }
          break;
        default:
          // Fallback to normal 4x4 block (same as mode 1)
          for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
            for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
              drawColors[i][j] = color;
            }
          }
          break;
      }

      // Scale the block according to DISPLAY_SCALE and position correctly in
      // pixelBuffer
      for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
        for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
          // Correctly place the block in the buffer by considering scaling and
          // offsets
          uint16_t scaledX = x * DISPLAY_SCALE + i;  // Scaled x-coordinate
          uint16_t scaledY = y * DISPLAY_SCALE + j;  // Scaled y-coordinate

          // Calculate the index in the buffer based on scaled coordinates
          uint16_t pixelPos =
              (scaledY * ZONE_WIDTH * DISPLAY_SCALE + scaledX) * 2;

          // Store the color in pixel buffer (in bytes)
          pixelBuffer[pixelPos] = (drawColors[i][j] >> 8) & 0xFF;  // High byte
          pixelBuffer[pixelPos + 1] = drawColors[i][j] & 0xFF;     // Low byte
        }
      }
    }
  }

  // Push the formatted pixel buffer to the display
  lcd_PushColors(xOffset, yOffset, ZONE_WIDTH * DISPLAY_SCALE,
                 ZONE_HEIGHT * DISPLAY_SCALE, (uint16_t *)pixelBuffer);
}

void IRAM_ATTR Rm67162Amoled::FillPanelRaw(uint8_t *pBuffer) {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = (y * TOTAL_WIDTH + x) * 3;
      uint16_t color =
          sprite.color565(pBuffer[pos], pBuffer[pos + 1], pBuffer[pos + 2]);
      uint16_t black = sprite.color565(0, 0, 0);  // Black color

      // Loop through each pixel in the scale block
      for (uint16_t i = 0; i < DISPLAY_SCALE; i++) {
        for (uint16_t j = 0; j < DISPLAY_SCALE; j++) {
          uint16_t drawColor = black;  // Default is black for borders

          switch (currentScalingMode) {
            case 0:
              drawColor = color;  // Normal 4x4 block
              break;
            case 1:
              // 2x2 center pixels in color, rest black
              if (i > 0 && i < DISPLAY_SCALE - 1 && j > 0 &&
                  j < DISPLAY_SCALE - 1) {
                drawColor = color;
              }
              break;
            case 2:
              // 3x3 center pixels in color, rest black
              if (i > 0 && i < DISPLAY_SCALE && j > 0 && j < DISPLAY_SCALE) {
                drawColor = color;
              }
              break;
            case 3:
              // Argyle (diamond) pattern
              if ((i == j) || (i + j == DISPLAY_SCALE - 1)) { 
                drawColor = color;  // Diagonal pixels
              }
              break;
            default:
              drawColor = color;  // Fallback
          }

          // Draw pixel
          sprite.drawPixel(x * DISPLAY_SCALE + i, y * DISPLAY_SCALE + j + DISPLAY_Y_OFFSET, 
                           drawColor);
        }
      }
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

      sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE,
                      DISPLAY_SCALE, color);
    }
  }
  lcd_PushColors(0, 0, 536, 240, (uint16_t *)sprite.getPointer());
}

#if !defined(ZEDMD_WIFI)
void Rm67162Amoled::FillPanelUsingChangedPalette(uint8_t *pBuffer,
                                                 uint8_t *palette,
                                                 bool *paletteAffected) {
  uint16_t pos;

  for (uint16_t y = 0; y < TOTAL_HEIGHT; y++) {
    for (uint16_t x = 0; x < TOTAL_WIDTH; x++) {
      pos = pBuffer[y * TOTAL_WIDTH + x];
      if (paletteAffected[pos]) {
        pos *= 3;

        uint16_t color =
            sprite.color565(palette[pos], palette[pos + 1], palette[pos + 2]);

        sprite.fillRect(x * DISPLAY_SCALE, y * DISPLAY_SCALE, DISPLAY_SCALE,
                        DISPLAY_SCALE, color);
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