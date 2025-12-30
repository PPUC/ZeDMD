#include "spi_transport.h"

#include "main.h"
#ifdef DMDREADER
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/zedmd_spi_input.pio.h"
#include "utility/clock.h"
#endif

#ifdef DMDREADER
SpiTransport* SpiTransport::s_instance = nullptr;
#endif

SpiTransport::SpiTransport() : Transport() {
  m_type = SPI;
  m_loopback = true;
}

SpiTransport::~SpiTransport() { deinit(); }

bool SpiTransport::init() {
#ifdef DMDREADER
  // Start in loopback mode until the host enables SPI via GPIO 13.
  dmdreader_loopback_init(buffers[0], buffers[1], m_color);

  pinMode(SPI_TRANSPORT_ENABLE_PIN, INPUT_PULLDOWN);
  pinMode(SPI_TRANSPORT_CLK_PIN, INPUT);
  pinMode(SPI_TRANSPORT_DATA_PIN, INPUT);

  initPio();
  m_dmaChannel = dma_claim_unused_channel(true);
  s_instance = this;

  m_enableRisePending = false;
  m_enableFallPending = false;

  // Initialize GPIO IRQ from core1 (loop1) so callbacks run on core1.
  gpio_acknowledge_irq(SPI_TRANSPORT_ENABLE_PIN,
                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);  // clear stale
  gpio_set_irq_enabled_with_callback(SPI_TRANSPORT_ENABLE_PIN,
                                     GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                     true, &SpiTransport::gpio_irq_handler);
  dma_irqn_set_channel_enabled(kSpiDmaIrqIndex, m_dmaChannel, true);
  irq_set_exclusive_handler(kSpiDmaIrq, &SpiTransport::dma_irq_handler);
  irq_set_enabled(kSpiDmaIrq, true);

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

void SpiTransport::initDmdReader() {
  dmdreader_init();
  // Start SPI transfer, waiting in the background.
  dmdreader_spi_init();
}

void SpiTransport::initPio() {
  dmdreader_error_blink(pio_claim_free_sm_and_add_program_for_gpio_range(
      &zedmd_spi_input_program, &m_pio, &m_stateMachine, &m_programOffset,
      SPI_TRANSPORT_CLK_PIN, 2, true));

  pio_gpio_init(m_pio, SPI_TRANSPORT_CLK_PIN);
  pio_gpio_init(m_pio, SPI_TRANSPORT_DATA_PIN);
  pio_sm_set_consecutive_pindirs(m_pio, m_stateMachine, SPI_TRANSPORT_CLK_PIN,
                                 1, false);
  pio_sm_set_consecutive_pindirs(m_pio, m_stateMachine, SPI_TRANSPORT_DATA_PIN,
                                 1, false);

  pio_sm_config config =
      zedmd_spi_input_program_get_default_config(m_programOffset);
  sm_config_set_in_pins(&config, SPI_TRANSPORT_DATA_PIN);
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

void SpiTransport::startDma() {
  if (m_dmaRunning) return;
  dma_irqn_acknowledge_channel(kSpiDmaIrqIndex, m_dmaChannel);
  m_dmaCompletePending = false;
  dma_channel_config cfg = dma_channel_get_default_config(m_dmaChannel);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);
  channel_config_set_dreq(&cfg, pio_get_dreq(m_pio, m_stateMachine, false));

  dma_channel_configure(m_dmaChannel, &cfg, buffers[m_rxBuffer],
                        &m_pio->rxf[m_stateMachine], RGB565_TOTAL_BYTES, true);
  m_dmaRunning = true;
}

void SpiTransport::resetStateMachine() {
  pio_sm_set_enabled(m_pio, m_stateMachine, false);
  pio_sm_clear_fifos(m_pio, m_stateMachine);
  pio_sm_restart(m_pio, m_stateMachine);
  pio_sm_set_enabled(m_pio, m_stateMachine, true);
};

bool SpiTransport::stopDmaAndFlush(bool abortChannel) {
  if (!m_dmaRunning) return false;

  uint32_t remaining = dma_channel_hw_addr(m_dmaChannel)->transfer_count;
  uint32_t received = RGB565_TOTAL_BYTES - remaining;

  // Stop DMA if we're aborting before transfer completion.
  if (abortChannel) {
    dma_channel_abort(m_dmaChannel);
  }

  // Drain any unexpected residual bytes to leave the FIFO empty.
  uint32_t drainedBytes = 0;
  while (!pio_sm_is_rx_fifo_empty(m_pio, m_stateMachine)) {
    (void)pio_sm_get(m_pio, m_stateMachine);
    ++drainedBytes;
  }

  // Ignore incomplete frames (shorter than RGB565_TOTAL_BYTES).
  if (received != RGB565_TOTAL_BYTES) {
    // Reset the state machine to clear any bit-shifted state before next frame.
    resetStateMachine();

    m_dmaRunning = false;
    return false;
  }

  // If extra bytes were clocked in after DMA completed, reset to avoid
  // misalignment on the next frame.
  if (drainedBytes > 0) {
    resetStateMachine();
    m_dmaRunning = false;
    return false;
  }

  if (++m_rxBuffer >= NUM_BUFFERS) {
    m_rxBuffer = 0;
  }

  m_dmaRunning = false;
  m_transferActive = false;
  m_dmaCompletePending = false;
  return true;
}

void SpiTransport::switchToSpiMode() {
  if (!m_loopback) return;

  // Turn on transport active flag immediately to avoid a logo to be displayed.
  transportActive = true;
  m_loopback = false;
  m_transferActive = false;

  dmdreader_loopback_stop();

  m_rxBuffer = 0;
  m_dataBuffer = NUM_BUFFERS - 1;

  enableSpiStateMachine();
}

bool SpiTransport::onEnableRise() {
  bool new_data = false;
  if (m_loopback) {
    switchToSpiMode();
  } else if (m_transferActive) {
    new_data = stopDmaAndFlush(true);
  }
  return new_data;
}

void SpiTransport::onEnableFall() {
  if (m_loopback)
    return;
  else if (!m_transferActive) {
    startDma();
    m_transferActive = true;
  }
}

bool SpiTransport::ProcessEnablePinEvents() {
  if (m_dmaCompletePending) {
    m_dmaCompletePending = false;
    return stopDmaAndFlush(false);
  }

  if (m_enableRisePending) {
    m_enableRisePending = false;
    return onEnableRise();
  }

  if (m_enableFallPending) {
    m_enableFallPending = false;
    onEnableFall();
  }

  return false;
}

uint8_t* SpiTransport::GetDataBuffer() {
  if (++m_dataBuffer >= NUM_BUFFERS) {
    m_dataBuffer = 0;
  }

  return buffers[m_dataBuffer];
}

void SpiTransport::gpio_irq_handler(uint gpio, uint32_t events) {
  if (!s_instance || gpio != SPI_TRANSPORT_ENABLE_PIN) return;
  if (events & GPIO_IRQ_EDGE_RISE) {
    s_instance->m_enableRisePending = true;
  }
  if (events & GPIO_IRQ_EDGE_FALL) {
    s_instance->m_enableFallPending = true;
  }
}

void SpiTransport::dma_irq_handler() {
  if (!s_instance) return;
  if (dma_irqn_get_channel_status(kSpiDmaIrqIndex, s_instance->m_dmaChannel)) {
    dma_irqn_acknowledge_channel(kSpiDmaIrqIndex, s_instance->m_dmaChannel);
    s_instance->m_transferActive = false;
    s_instance->m_dmaCompletePending = true;
  }
}

void SpiTransport::SetColor(Color color) { m_color = color; }

#endif
