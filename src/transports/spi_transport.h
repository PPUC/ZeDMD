#ifndef ZEDMD_SPI_TRANSPORT_H
#define ZEDMD_SPI_TRANSPORT_H

#ifdef PICO_BUILD
#include "pico/zedmd_pico.h"
#endif
#ifdef DMDREADER
#include <dmdreader.h>

#include "hardware/dma.h"
#include "hardware/pio.h"
#endif
#include "main.h"
#include "transport.h"

#define SPI_TRANSPORT_ENABLE_PIN 13
#define SPI_TRANSPORT_CLK_PIN 14
#define SPI_TRANSPORT_DATA_PIN 15

class SpiTransport final : public Transport {
 public:
  SpiTransport();

  ~SpiTransport() override;

  bool init() override;

  bool deinit() override;

#ifdef DMDREADER
  void initDmdReader();
  void SetColor(Color color);
  void SetFrameReceived() { m_frameReceived = true; }
  bool GetFrameReceived();
  uint8_t* GetDataBuffer();

 private:
  void initPio();
  void SetAndEnableNewDmaTarget();
  static void dmaHandler();

  static SpiTransport* s_instance;
  static constexpr uint kSpiDmaIrqIndex = 2;
  static constexpr uint kSpiDmaIrq = DMA_IRQ_2;

  PIO m_pio;
  uint m_stateMachine;
  uint m_programOffset;
  uint m_dmaChannel;
  dma_channel_config m_dmaChannelConfig;
  uint8_t m_rxBuffer;
  Color m_color = Color::ORANGE;
  bool m_frameReceived = false;
#endif
};

#endif  // ZEDMD_SPI_TRANSPORT_H
