#include "loopback_transport.h"

#include "main.h"
#ifdef DMDREADER
#include <dmdreader.h>
#endif

LoopbackTransport::LoopbackTransport() : Transport() { m_type = LOOPBACK; }

LoopbackTransport::~LoopbackTransport() { deinit(); }

bool LoopbackTransport::init() {
  m_active = true;

  xTaskCreatePinnedToCore(Task_DmdReader, "Task_DmdReader", 4096, this, 1,
                          &m_task, 1);

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

#ifdef DMDREADER
  dmdreader_init(pio0);
#endif

  while (transport->isActive()) {
    // Avoid busy-waiting
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
