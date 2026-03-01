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
  uint8_t drawBuffer[TOTAL_WIDTH * PANEL_HEIGHT * 3];
};

#endif  // PICO_LED_MATRIX_H
