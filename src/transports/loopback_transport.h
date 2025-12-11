#ifndef ZEDMD_LOOPBACK_TRANSPORT_H
#define ZEDMD_LOOPBACK_TRANSPORT_H

#ifdef PICO_BUILD
#include "pico/zedmd_pico.h"
#endif
#include "transport.h"

class LoopbackTransport final : public Transport {
 public:
  LoopbackTransport();

  ~LoopbackTransport() override;

  bool init() override;

  bool deinit() override;

 private:
  static void Task_DmdReader(void *pvParameters);

  TaskHandle_t m_task{};
};

#endif  // ZEDMD_LOOPBACK_TRANSPORT_H
