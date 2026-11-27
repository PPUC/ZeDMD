//
// Created by cpasjuste on 20/10/2025.
//

#ifndef ZEDMD_PICO_H
#define ZEDMD_PICO_H

#ifndef DMDREADER
#include <Adafruit_TinyUSB.h>
#endif
#include <Arduino.h>
#include <RP2040Support.h>

#include <cstring>

// in ram
#define IRAM_ATTR __attribute__((section(".time_critical.")))

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
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    // Copy the ID bytes; casting the array would return its address, which
    // is the same on every board.
    uint64_t id = 0;
    memcpy(&id, board_id.id, sizeof(id));
    return id;
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

#ifndef DMDREADER
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
#endif

#ifdef PICO_CYW43_SUPPORTED
#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/structs/iobank0.h>
#include <hardware/structs/padsbank0.h>
#include <hardware/structs/sio.h>

// The same firmware runs on the Pico 2 and the Pico 2 W, but the core assumes
// there is a WiFi chip on every RP2350. On the Pico 2 W, GPIO 25 (the chip's
// SPI select) also switches the VSYS/3 sense divider onto GPIO 29 (ADC 3).
// On a Pico 2 the divider is always connected, and on boards where GPIO 29 is
// a button nothing follows GPIO 25 either. So GPIO 29 reading VSYS/3 with
// GPIO 25 high and about 0 with it low means there's a WiFi chip. Measured on
// a Pico 2 W powered over USB: 1827 and 16. Nothing else is driven.
struct Cyw43Check {
  uint16_t csHigh;
  uint16_t csLow;
  bool present;
};

inline uint16_t ReadAdcGpio29() {
  adc_select_input(3);  // GPIO 29
  busy_wait_us(200);    // let the divider settle
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += adc_read();
  return sum / 8;
}

// Call before anything else uses GPIO 25 or 29, i.e. before WiFi starts.
inline Cyw43Check DetectCyw43() {
  constexpr uint cs = 25, vsys = 29;
  // Restore both pins exactly as they were, so the WiFi driver later finds
  // them untouched.
  const uint32_t csCtrl = io_bank0_hw->io[cs].ctrl;
  const uint32_t csPad = pads_bank0_hw->io[cs];
  const uint32_t vsysCtrl = io_bank0_hw->io[vsys].ctrl;
  const uint32_t vsysPad = pads_bank0_hw->io[vsys];
  const bool csOe = sio_hw->gpio_oe & (1u << cs);
  const bool csOut = sio_hw->gpio_out & (1u << cs);

  adc_init();
  adc_gpio_init(vsys);  // no pulls, digital input off
  gpio_init(cs);
  gpio_set_dir(cs, GPIO_OUT);

  Cyw43Check check;
  gpio_put(cs, 1);
  check.csHigh = ReadAdcGpio29();
  gpio_put(cs, 0);
  check.csLow = ReadAdcGpio29();
  // VSYS above ~2 V while switched on, about 0 V while switched off.
  check.present = check.csHigh > 800 && check.csLow < 300;

  if (csOut)
    sio_hw->gpio_set = 1u << cs;
  else
    sio_hw->gpio_clr = 1u << cs;
  if (csOe)
    sio_hw->gpio_oe_set = 1u << cs;
  else
    sio_hw->gpio_oe_clr = 1u << cs;
  pads_bank0_hw->io[cs] = csPad;
  io_bank0_hw->io[cs].ctrl = csCtrl;
  pads_bank0_hw->io[vsys] = vsysPad;
  io_bank0_hw->io[vsys].ctrl = vsysCtrl;
  return check;
}
#endif

#endif  // ZEDMD_PICO_H
