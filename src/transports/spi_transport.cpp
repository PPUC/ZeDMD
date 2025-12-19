#include "spi_transport.h"

#include "main.h"
#ifdef DMDREADER
#include "pico/zedmd_spi_input.pio.h"
#include "utility/clock.h"
#endif

#ifdef DMDREADER
SpiTransport* SpiTransport::s_instance = nullptr;
#endif

SpiTransport::SpiTransport() : Transport() { m_type = SPI; }

SpiTransport::~SpiTransport() { deinit(); }

bool SpiTransport::init() {
#ifdef DMDREADER
  dmdreader_init();

  // Start in loopback mode until the host enables SPI via GPIO 13.
  dmdreader_loopback_init(buffers[0], buffers[1], m_color);
  m_loopback = true;

  pinMode(kEnablePin, INPUT_PULLDOWN);
  pinMode(kClockPin, INPUT);
  pinMode(kDataPin, INPUT);

  initPio();
  m_dmaChannel = dma_claim_unused_channel(true);
  s_instance = this;
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

#ifdef DMDREADER

void SpiTransport::initPio() {
  pio_claim_free_sm_and_add_program_for_gpio_range(
      &zedmd_spi_input_program, &m_pio, &m_stateMachine, &m_programOffset,
      kClockPin, 2, true);

  pio_gpio_init(m_pio, kClockPin);
  pio_gpio_init(m_pio, kDataPin);
  pio_sm_set_consecutive_pindirs(m_pio, m_stateMachine, kClockPin, 1, false);
  pio_sm_set_consecutive_pindirs(m_pio, m_stateMachine, kDataPin, 1, false);

  pio_sm_config config =
      zedmd_spi_input_program_get_default_config(m_programOffset);
  sm_config_set_in_pins(&config, kDataPin);
  sm_config_set_in_shift(&config, false, true, 8);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);

  pio_sm_init(m_pio, m_stateMachine, m_programOffset, &config);
  pio_sm_set_enabled(m_pio, m_stateMachine, false);
}

void SpiTransport::enableSpiStateMachine() {
  if (m_spiEnabled) return;

  pio_sm_set_enabled(m_pio, m_stateMachine, false);
  pio_sm_clear_fifos(m_pio, m_stateMachine);
  pio_sm_restart(m_pio, m_stateMachine);
  pio_sm_set_enabled(m_pio, m_stateMachine, true);
  m_spiEnabled = true;
}

void SpiTransport::disableSpiStateMachine() {
  if (!m_spiEnabled || m_stateMachine < 0) return;
  pio_sm_set_enabled(m_pio, m_stateMachine, false);
  m_spiEnabled = false;
}

void SpiTransport::flushRxBuffer() {
  if (m_rxBufferPos == 0) return;
  HandleData(m_rxBuffer, m_rxBufferPos);
  m_rxBufferPos = 0;
}

void SpiTransport::startDma() {
  if (m_dmaChannel < 0 || m_stateMachine < 0) return;
  m_rxBufferPos = 0;
  dma_channel_config cfg = dma_channel_get_default_config(m_dmaChannel);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_dreq(&cfg, pio_get_dreq(m_pio, m_stateMachine, false));

  dma_channel_configure(m_dmaChannel, &cfg, m_rxBuffer,
                        &m_pio->rxf[m_stateMachine], kRxBufferSize, true);
  m_dmaRunning = true;
}

void SpiTransport::stopDmaAndFlush() {
  if (m_dmaChannel < 0) return;

  uint32_t remaining = dma_channel_hw_addr(m_dmaChannel)->transfer_count;
  dma_channel_abort(m_dmaChannel);
  m_dmaRunning = false;

  // Bytes already written by DMA
  m_rxBufferPos = kRxBufferSize - remaining;
  if (m_rxBufferPos > kRxBufferSize) m_rxBufferPos = 0;

  // Drain any residual FIFO bytes that arrived after the DMA stop.
  while (!pio_sm_is_rx_fifo_empty(m_pio, m_stateMachine) &&
         m_rxBufferPos < kRxBufferSize) {
    const uint32_t raw = pio_sm_get(m_pio, m_stateMachine);
    m_rxBuffer[m_rxBufferPos++] = raw & 0xff;
  }

  flushRxBuffer();
}

void SpiTransport::switchToSpiMode() {
  if (!m_loopback) return;
  m_loopback = false;
  m_transferActive = false;
  m_rxBufferPos = 0;
  payloadMissing = 0;
  headerBytesReceived = 0;
  numCtrlCharsFound = 0;

  dmdreader_spi_init();

  transportActive = true;
}

void SpiTransport::onEnableRise() {
  if (m_loopback)
    switchToSpiMode();
  else if (m_transferActive) {
    stopDmaAndFlush();
    m_transferActive = false;
  }
}

void SpiTransport::onEnableFall() {
  if (m_loopback)
    return;
  else if (!m_transferActive) {
    enableSpiStateMachine();
    startDma();
    m_transferActive = true;
  }
}

void SpiTransport::ProcessEnablePinEvents() {
  if (m_enableRisePending) {
    m_enableRisePending = false;
    onEnableRise();
  }

  if (m_enableFallPending) {
    m_enableFallPending = false;
    onEnableFall();
  }
}

void SpiTransport::gpio_irq_handler(uint gpio, uint32_t events) {
  if (!s_instance || gpio != kEnablePin) return;
  if (events & GPIO_IRQ_EDGE_RISE) {
    s_instance->m_enableRisePending = true;
  }
  if (events & GPIO_IRQ_EDGE_FALL) {
    s_instance->m_enableFallPending = true;
  }
}

void SpiTransport::SetupEnablePin() {
  m_enableRisePending = false;
  m_enableFallPending = false;

  // Initialize GPIO IRQ from core1 (loop1) so callbacks run on core1.
  gpio_acknowledge_irq(kEnablePin,
                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);  // clear stale
  gpio_set_irq_enabled_with_callback(kEnablePin,
                                     GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                     true, &SpiTransport::gpio_irq_handler);
}

void SpiTransport::SetColor(Color color) { m_color = color; }

#endif
