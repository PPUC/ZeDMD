#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESPAsyncWebServer.h>

#include "displayDriver.h"

// Global variables to be shared between webserver.cpp and main.cpp
extern String ssid;
extern String pwd;
extern uint16_t port;
extern uint8_t ssid_length;
extern uint8_t pwd_length;

extern uint32_t dimTimeout;
extern bool enableDimAfterTimeout;
extern uint8_t screensaverMode;
extern uint8_t lumstep;
extern uint8_t rgbMode;
extern uint8_t rgbModeLoaded;
extern DisplayDriver *display;

// Declare missing functions
void Restart();
bool SaveWiFiConfig();
void SaveRgbOrder();
void SaveLum();
void SaveScale();
DisplayDriver *GetDisplayObject();
void RefreshSetupScreen();

// Functions in webserver.cpp
void runWebServer();

#endif  // WEBSERVER_H
