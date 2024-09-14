#include "displayConfig.h"

uint8_t rgbMode = 0;
uint8_t rgbModeLoaded = 0;

const uint8_t rgbOrder[3 * 6] = {
    R, G, B,  // rgbMode 0
    B, R, G,  // rgbMode 1
    G, B, R,  // rgbMode 2
    R, B, G,  // rgbMode 3
    G, R, B,  // rgbMode 4
    B, G, R   // rgbMode 5
};
