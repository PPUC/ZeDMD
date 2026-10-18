#ifndef PICO_LED_MATRIX_H
#define PICO_LED_MATRIX_H

#include "LedMatrix.h"
#include "hub75.hpp"

class PicoLedMatrix final : public LedMatrix {
 public:
  PicoLedMatrix();            // Constructor
  ~PicoLedMatrix() override;  // Destructor

  void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                 uint8_t b) override;
  void DrawPixel(uint16_t x, uint16_t y, uint16_t color) override;

  void ClearScreen() override;
  void SetBrightness(uint8_t level) override;
  void FillScreen(uint8_t r, uint8_t g, uint8_t b) override;
  void FillZoneRaw(uint8_t idx, uint8_t *pBuffer) override;
  void FillZoneRaw565(uint8_t idx, uint8_t *pBuffer) override;
  void ClearZone(uint8_t idx) override;
  void FillPanelRaw(uint8_t *pBuffer) override;
  void Render() override;

  // Full RGB565 frame (TOTAL_WIDTH x TOTAL_HEIGHT, little-endian): converted
  // straight into the HUB75 draw buffer and shown via Flip().
  void FillPanelRaw565(const uint8_t *pBuffer);
  // Building blocks to split FillPanelRaw565() across both cores: BeginFrame()
  // once, FillRows565() for disjoint row ranges, then Flip().
  void BeginFrame();
  void FillRows565(uint16_t yStart, uint16_t yEnd, const uint8_t *pBuffer);
  // Shows the drawn frame at the next refresh boundary without copying.
  void Flip();
  // Moves the refresh IRQ to the calling core. Must be called on the target
  // core while the IRQ is disabled on the other one.
  void EnableRefreshIrqOnThisCore();
  void DisableRefreshIrqOnThisCore();

 private:
  uint8_t color_order[6] = {
      static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::RGB),
      static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::BRG),
      static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::GBR),
      static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::RBG),
      static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::GRB),
      static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::BGR)};
};

#endif  // PICO_LED_MATRIX_H
