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
#define SPI_TRANSPORT_CLK_PIN 18
#define SPI_TRANSPORT_DATA_PIN 19
// A partially received frame that doesn't progress for this time in
// microseconds gets discarded. Must be longer than any pause within a frame
// and shorter than the pause between two frames.
#define SPI_TRANSPORT_FRAME_TIMEOUT_US 1000

class SpiTransport final : public Transport {
 public:
  SpiTransport();

  ~SpiTransport() override;

  bool init() override;

  bool deinit() override;

#ifdef DMDREADER
  bool initDmdReader();
  bool isDmdReaderInitialized() const { return m_dmdReaderInitialized; }
  void SetColor(Color color);
  void SetFrameReceived() { m_frameReceived = true; }
  bool GetFrameReceived();
  uint8_t* GetDataBuffer();

 private:
  void initPio();
  void SetAndEnableNewDmaTarget();
  void CheckFrameTimeout();
  void Resync();
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
  uint8_t m_stalledBuffer = 0;
  uint32_t m_missingBytes = 0;
  uint32_t m_lastProgressUs = 0;
  Color m_color = Color::DMD_ORANGE;
  volatile bool m_frameReceived = false;
  bool m_dmdReaderInitialized = false;
#endif
};

#endif  // ZEDMD_SPI_TRANSPORT_H
