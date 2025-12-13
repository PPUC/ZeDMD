#include "spi_transport.h"

#include "main.h"
#ifdef DMDREADER
#include <dmdreader.h>
#endif

SpiTransport::SpiTransport() : Transport() { m_type = SPI; }

SpiTransport::~SpiTransport() { deinit(); }

bool SpiTransport::init() {
  #ifdef DMDREADER
  dmdreader_init(pio1);

  // @todo Check if SPI is established, otherwise use loopback
  dmdreader_loopback_init(buffers[0], buffers[1], Color::GREEN);
  m_loopback = true;

  #endif

  m_active = true;

  return true;
}

bool SpiTransport::deinit() {
  if (m_active) {
    m_active = false;
    // TODO ? clean exit ?
    // delay(500);
    // vTaskDelete(m_task);
  }

  return true;
}
