#ifndef AMOLED_H
#define AMOLED_H

#include <LittleFS.h>
#include <TFT_eSPI.h>

#include "displayDriver.h"
#include "panel.h"  // Include ZeDMD panel constants
#include "rm67162.h"

// Scale pixels 4x for almost full display coverage
#define DISPLAY_SCALE 4

#define DISPLAY_Y_OFFSET \
  48  // Move the display down x pixels to center the image
      // You need be able to divide by 16 for the partial refreshes to work

// SCALING MODES: You can set these via the webui.
// If you are implementing a custom display driver, define the scaling methods
// in your .h and .cpp files. The web UI will automatically populate the
// dropdown menu with the scaling modes specified in your driver implementation.
// AMOLED Scaling modes:
// 0 = 4x4 pixel blocks
// 1 = 2x2 pixel blocks, other pixels black (DMD style)
// 2 = 3x3 pixel blocks, other pixels black (DMD style #2)
// 3 = Argyle(diamond) pixel blocks, other pixels black (DMD style #3)

class Rm67162Amoled : public DisplayDriver {
 private:
  TFT_eSPI tft;
  TFT_eSprite sprite;
  const uint8_t lumval[16] = {0,   50,  66,  82,  98,  114, 130, 146,
                              162, 178, 194, 210, 226, 242, 250, 254};

  static const char *scalingModes[4];  // Static array of scaling mode names
  static const uint8_t modeCount = 4;  // Number of scaling modes

  uint8_t currentScalingMode;

 public:
  Rm67162Amoled();

  bool HasScalingModes();
  const char **GetScalingModes();
  uint8_t GetScalingModeCount();
  uint8_t GetCurrentScalingMode();
  void SetCurrentScalingMode(uint8_t mode);

  void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);
  void DrawPixel(uint16_t x, uint16_t y, uint16_t color);
  void ClearScreen();
  void SetBrightness(uint8_t level);
  void FillScreen(uint8_t r, uint8_t g, uint8_t b);
  void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r,
                   uint8_t g, uint8_t b, bool transparent = false,
                   bool inverted = false);
  void FillZoneRaw(uint8_t idx, uint8_t *pBuffer);
  void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer);
  void ClearZone(uint8_t idx);
  void FillPanelRaw(uint8_t *pBuffer);
  void FillPanelUsingPalette(uint8_t *pBuffer, uint8_t *palette);
#if !defined(ZEDMD_WIFI)
  void FillPanelUsingChangedPalette(uint8_t *pBuffer, uint8_t *palette,
                                    bool *paletteAffected);
#endif

  ~Rm67162Amoled();
};

#endif  // AMOLED_H