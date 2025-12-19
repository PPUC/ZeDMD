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

class SpiTransport final : public Transport {
 public:
  SpiTransport();

  ~SpiTransport() override;

  bool init() override;

  bool deinit() override;

#ifdef DMDREADER
  void SetupEnablePin();
  void SetColor(Color color);
  void ProcessEnablePinEvents();

 private:
  void initPio();
  void enableSpiStateMachine();
  void disableSpiStateMachine();
  void flushRxBuffer();
  void startDma();
  void stopDmaAndFlush();
  void switchToSpiMode();
  void onEnableRise();
  void onEnableFall();
  static void gpio_irq_handler(uint gpio, uint32_t events);

  static constexpr uint8_t kEnablePin = 13;
  static constexpr uint8_t kClockPin = 14;
  static constexpr uint8_t kDataPin = 15;
  static constexpr size_t kRxBufferSize = BUFFER_SIZE;

  static SpiTransport* s_instance;

  PIO m_pio;
  uint m_stateMachine;
  uint m_programOffset;
  uint m_dmaChannel;
  bool m_spiEnabled = false;
  bool m_transferActive = false;
  bool m_dmaRunning = false;
  uint8_t m_rxBuffer[kRxBufferSize];
  size_t m_rxBufferPos = 0;
  Color m_color = Color::ORANGE;
  volatile bool m_enableRisePending = false;
  volatile bool m_enableFallPending = false;
#endif
};

#endif  // ZEDMD_SPI_TRANSPORT_H
