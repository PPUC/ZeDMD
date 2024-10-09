#ifndef S3SPECIFIC_H
#define S3SPECIFIC_H

#ifdef ZEDMD_WIFI
// These defines maximize WiFi performance per ESP32 technical documentation
// The S3 has memory enough to run this without issue.
#define WIFI_STATIC_RX_BUFFER_NUM 24
#define WIFI_DYNAMIC_RX_BUFFER_NUM 64
#define WIFI_DYNAMIC_TX_BUFFER_NUM 64
#define WIFI_RX_BA_WIN 32
#define WIFI_IRAM_OPT 15
#define WIFI_RX_IRAM_OPT 16
#define LWIP_IRAM_OPTIMIZATION 13
#define INSTRUCTION_CACHE 32
#define INSTRUCTION_CACHE_LINE 32
#define INSTRUCTION_CACHE_WAYS 8
#define DATA_CACHE 64
#define DATA_CACHE_LINE 32
#define DATA_CACHE_WAYS 8

#endif

#endif // VERSION_H