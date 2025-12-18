#ifndef PICO_LED_MATRIX_H
#define PICO_LED_MATRIX_H

#include "LedMatrix.h"
#include <hub75.hpp>

class PicoLedMatrix final : public LedMatrix {
 public:
  PicoLedMatrix();            // Constructor
  ~PicoLedMatrix() override;  // Destructor

  void DrawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g,
                 uint8_t b) override;
  void DrawPixel(uint16_t x, uint16_t y, uint16_t color) override;

  void ClearScreen() override;
  void SetBrightness(uint8_t level) override;
  void Render() override;

 private:
  /*
   uint8_t color_order[6] = {
       static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::RGB),
       static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::BRG),
       static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::GBR),
       static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::RBG),
       static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::GRB),
       static_cast<uint8_t>(pimoroni::Hub75::COLOR_ORDER::BGR)};
 */
  uint8_t drawBuffer[TOTAL_WIDTH * PANEL_HEIGHT * 3];
};

#endif  // PICO_LED_MATRIX_H
