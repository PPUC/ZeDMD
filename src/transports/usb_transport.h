#ifndef ZEDMD_USB_TRANSPORT_H
#define ZEDMD_USB_TRANSPORT_H

#ifdef PICO_BUILD
#include "pico/zedmd_pico.h"
#endif
#include "transport.h"

class UsbTransport final : public Transport {
 public:
  UsbTransport();

  ~UsbTransport() override;

  bool init() override;

  bool deinit() override;

 private:
  static void Task_ReadSerial(void *pvParameters);

  TaskHandle_t m_task{};
};

#endif  // ZEDMD_USB_TRANSPORT_H
