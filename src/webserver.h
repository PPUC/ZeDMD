#ifdef ZEDMD_WIFI
#ifndef WEBSERVER_H
#define WEBSERVER_H

//#include <ESPAsyncWebServer.h>

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

// Declare missing functions
bool SaveWiFiConfig();
void SaveRgbOrder();
void SaveLum();
bool verifyImage(const char *filename);
bool DisplayImage(const char *filename);
void setBrightness(uint8_t lumstep);
void refreshScreen();
bool SaveScreensaverConfig();

// Functions in webserver.cpp
void runWebServer();
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

#endif  // WEBSERVER_H
#endif // ZEDMD_WIFI