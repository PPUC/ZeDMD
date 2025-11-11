#ifndef LEDMATRIX_H
#define LEDMATRIX_H

#include "main.h"

class LedMatrix : public DisplayDriver {
 public:
  LedMatrix() = default;
  ~LedMatrix() override = default;

  bool HasScalingModes() override;
  const char **GetScalingModes() override;
  uint8_t GetScalingModeCount() override;
  uint8_t GetCurrentScalingMode() override;
  void SetCurrentScalingMode(uint8_t mode) override;

  void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                 uint8_t b) override {}
  void DrawPixel(uint16_t x, uint16_t y, uint16_t color) override {}

  void ClearScreen() override;
  void SetBrightness(uint8_t level) override;
  void FillScreen(uint8_t r, uint8_t g, uint8_t b) override;
  void DisplayText(const char *text, uint16_t x, uint16_t y, uint8_t r,
                   uint8_t g, uint8_t b, bool transparent = false,
                   bool inverted = false) override;
  void FillZoneRaw(uint8_t idx, uint8_t *pBuffer) override;
  void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) override;
  void ClearZone(uint8_t idx) override;
  void FillPanelRaw(uint8_t *pBuffer) override;
};

#endif  // LEDMATRIX_H
