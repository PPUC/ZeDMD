//
// Created by cpasjuste on 22/10/2025.
//

#ifndef ZEDMD_ESP_TASK_WDT_H
#define ZEDMD_ESP_TASK_WDT_H

inline uint8_t esp_task_wdt_reset() {
    watchdog_update();
    return 0;
}

#endif //ZEDMD_ESP_TASK_WDT_H