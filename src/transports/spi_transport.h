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
  uint8_t* GetDataBuffer() { return m_dataBuffer; }

 private:
  void initPio();
  void enableSpiStateMachine();
  void disableSpiStateMachine();
  void resetStateMachine();
  void startDma();
  bool stopDmaAndFlush();
  void switchToSpiMode();
  bool onEnableRise();
  void onEnableFall();
  static void gpio_irq_handler(uint gpio, uint32_t events);

  static SpiTransport* s_instance;

  PIO m_pio;
  uint m_stateMachine;
  uint m_programOffset;
  uint m_dmaChannel;
  bool m_spiEnabled = false;
  bool m_transferActive = false;
  bool m_dmaRunning = false;
  uint8_t* m_rxBuffer;
  uint8_t* m_dataBuffer;
  Color m_color = Color::ORANGE;
  volatile bool m_enableRisePending = false;
  volatile bool m_enableFallPending = false;
#endif
};

#endif  // ZEDMD_SPI_TRANSPORT_H
