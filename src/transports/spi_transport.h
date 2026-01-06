#ifndef ZEDMD_SPI_TRANSPORT_H
#define ZEDMD_SPI_TRANSPORT_H

#ifdef PICO_BUILD
#include "pico/zedmd_pico.h"
#endif
#ifdef DMDREADER
#include <dmdreader.h>

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
  bool ProcessEnablePinEvents();
  uint8_t* GetDataBuffer();

 private:
  void initPio();
  void enableSpiStateMachine();
  void disableSpiStateMachine();
  void resetStateMachine();
  void startDma();
  bool stopDmaAndFlush(bool abortChannel);
  void switchToSpiMode();
  bool onEnableRise();
  void onEnableFall();
  static void gpio_irq_handler(uint gpio, uint32_t events);
  static void dma_irq_handler();

  static SpiTransport* s_instance;
  static constexpr uint kSpiDmaIrqIndex = 2;
  static constexpr uint kSpiDmaIrq = DMA_IRQ_2;

  PIO m_pio;
  uint m_stateMachine;
  uint m_programOffset;
  uint m_dmaChannel;
  bool m_spiEnabled = false;
  bool m_transferActive = false;
  bool m_dmaRunning = false;
  uint8_t m_rxBuffer;
  Color m_color = Color::ORANGE;
  volatile bool m_enableRisePending = false;
  volatile bool m_enableFallPending = false;
  volatile bool m_dmaCompletePending = false;
#endif
};

#endif  // ZEDMD_SPI_TRANSPORT_H
