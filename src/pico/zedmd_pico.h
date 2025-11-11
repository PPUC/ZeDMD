//
// Created by cpasjuste on 20/10/2025.
//

#ifndef ZEDMD_PICO_H
#define ZEDMD_PICO_H

#include <Adafruit_TinyUSB.h>
#include <RP2040Support.h>

// in ram
#ifdef PICO_RP2350
#define IRAM_ATTR __attribute__((section(".time_critical.")))
#else
#define IRAM_ATTR
#endif

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_32BIT 1
#define heap_caps_malloc(x, y) malloc(x)
#define heap_caps_free(x) free(x)

// TODO: verify this...
#define ESP_RST_PANIC 1
#define ESP_RST_INT_WDT 2
#define ESP_RST_TASK_WDT 3
#define ESP_RST_WDT 4
#define ESP_RST_CPU_LOCKUP 5
#define ESP_RST_PWR_GLITCH 6

#define ESP_LOG_NONE 0

class EspClass {
 public:
  static uint64_t getEfuseMac() {
    static pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    return reinterpret_cast<uint64_t>(board_id.id);
  }
};

inline EspClass ESP;

inline uint8_t esp_reset_reason() {
  const auto reason = rp2040.getResetReason();
  switch (reason) {
    case RP2040::WDT_RESET:
      return ESP_RST_WDT;
    case RP2040::GLITCH_RESET:  // rp2350
      return ESP_RST_PWR_GLITCH;
    default:
      return 0;
  }
}

inline BaseType_t xTaskCreatePinnedToCore(const TaskFunction_t pxTaskCode,
                                          const char *const pcName,
                                          const uint32_t uxStackDepth,
                                          void *pvParameters,
                                          const UBaseType_t uxPriority,
                                          TaskHandle_t *pxCreatedTask,
                                          const BaseType_t xCoreID) {
  return xTaskCreateAffinitySet(pxTaskCode, pcName, uxStackDepth, pvParameters,
                                uxPriority, 1u << (xCoreID ^ 1), pxCreatedTask);
}

#endif  // ZEDMD_PICO_H
