#include "loopback_transport.h"

#include "main.h"
#ifdef DMDREADER
#include <dmdreader.h>
#endif


extern uint8_t* renderBuffer;

LoopbackTransport::LoopbackTransport() : Transport() { m_type = USB; }

LoopbackTransport::~LoopbackTransport() { deinit(); }

bool LoopbackTransport::init() {
  xTaskCreatePinnedToCore(Task_DmdReader, "Task_DmdReader", 4096, this, 1,
                          &m_task, 0);

  m_active = true;

  return true;
}

bool LoopbackTransport::deinit() {
  if (m_active) {
    m_active = false;
    // TODO ? clean exit ?
    // delay(500);
    // vTaskDelete(m_task);
  }

  return true;
}

void LoopbackTransport::Task_DmdReader(void* pvParameters) {
  const auto transport = static_cast<LoopbackTransport*>(pvParameters);

  dmdreader_init(pio1);

  while (transport->isActive()) {
    // Avoid busy-waiting
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
