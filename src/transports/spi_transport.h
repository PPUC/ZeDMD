#ifndef ZEDMD_SPI_TRANSPORT_H
#define ZEDMD_SPI_TRANSPORT_H

#ifdef PICO_BUILD
#include "pico/zedmd_pico.h"
#endif
#include "transport.h"

class SpiTransport final : public Transport {
 public:
  SpiTransport();

  ~SpiTransport() override;

  bool init() override;

  bool deinit() override;
};

#endif  // ZEDMD_SPI_TRANSPORT_H
