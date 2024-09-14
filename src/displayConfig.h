#ifndef DISPLAYCONFIG_H
#define DISPLAYCONFIG_H

#include <stdint.h>

#define R 0
#define G 1
#define B 2

// Global variables
extern uint8_t rgbMode;
extern uint8_t rgbModeLoaded;

// Constant array
extern const uint8_t rgbOrder[3 * 6];

#endif // DISPLAYCONFIG_H