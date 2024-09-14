#ifndef PANEL_H
#define PANEL_H

#ifdef ZEDMD_HD
#define PANEL_WIDTH 128  // Width: number of LEDs for 1 panel.
#define PANEL_HEIGHT 64  // Height: number of LEDs.
#define PANELS_NUMBER 2  // Number of horizontally chained panels.
#elif ZEDMD_HD_HALF
#define PANEL_WIDTH 128  // Width: number of LEDs for 1 panel.
#define PANEL_HEIGHT 64  // Height: number of LEDs.
#define PANELS_NUMBER 1  // Number of horizontally chained panels.
#endif
#ifndef PANEL_WIDTH
#define PANEL_WIDTH 64   // Width: number of LEDs for 1 panel.
#define PANEL_HEIGHT 32  // Height: number of LEDs.
#define PANELS_NUMBER 2  // Number of horizontally chained panels.
#endif

#define TOTAL_WIDTH (PANEL_WIDTH * PANELS_NUMBER)
#define TOTAL_WIDTH_PLANE (TOTAL_WIDTH >> 3)
#ifdef ZEDMD_HD_HALF
#define TOTAL_HEIGHT (PANEL_HEIGHT / 2)
#endif
#ifndef TOTAL_HEIGHT
#define TOTAL_HEIGHT PANEL_HEIGHT
#endif
#define TOTAL_BYTES (TOTAL_WIDTH * TOTAL_HEIGHT * 3)
#define ZONE_WIDTH (TOTAL_WIDTH / 16)
#define ZONE_HEIGHT (TOTAL_HEIGHT / 8)
#define ZONES_PER_ROW (TOTAL_WIDTH / ZONE_WIDTH)
#define TOTAL_ZONES ((TOTAL_HEIGHT / ZONE_HEIGHT) * ZONES_PER_ROW)
#define ZONE_SIZE (ZONE_WIDTH * ZONE_HEIGHT * 3)
#define RGB565_ZONE_SIZE (ZONE_WIDTH * ZONE_HEIGHT * 2)

#endif // PANEL_H