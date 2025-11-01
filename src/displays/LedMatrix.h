#ifndef LEDMATRIX_H
#define LEDMATRIX_H

#include "displayDriver.h"
#include "panel.h"  // Include ZeDMD panel constants

extern uint8_t rgbMode;
extern int8_t yOffset;
extern uint8_t panelClkphase;
extern uint8_t panelDriver;
extern uint8_t panelI2sspeed;
extern uint8_t panelLatchBlanking;
extern uint8_t panelMinRefreshRate;
extern const uint8_t rgbOrder[3 * 6];

class LedMatrix : public DisplayDriver {
 public:
  LedMatrix() {}
  ~LedMatrix() {}

  bool HasScalingModes();
  const char **GetScalingModes();
  uint8_t GetScalingModeCount();
  uint8_t GetCurrentScalingMode();
  void SetCurrentScalingMode(uint8_t mode);

  virtual void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                         uint8_t b);
  virtual void DrawPixel(uint16_t x, uint16_t y, uint16_t color);

  void ClearScreen();
  void SetBrightness(uint8_t level);
  void FillScreen(uint8_t r, uint8_t g, uint8_t b);
  void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r,
                   uint8_t g, uint8_t b, bool transparent = false,
                   bool inverted = false);
  void FillZoneRaw(uint8_t idx, uint8_t *pBuffer);
  void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer);
  void ClearZone(uint8_t idx);
  void FillPanelRaw(uint8_t *pBuffer) override;
};

#endif  // LEDMATRIX_H
