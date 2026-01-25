#include "spi_transport.h"

#include "main.h"
#ifdef DMDREADER
#include "pico/zedmd_spi_input.pio.h"

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

  m_dmaChannel = dma_claim_unused_channel(true);
  m_dmaChannelConfig = dma_channel_get_default_config(m_dmaChannel);
  channel_config_set_transfer_data_size(&m_dmaChannelConfig, DMA_SIZE_8);
  channel_config_set_read_increment(&m_dmaChannelConfig, false);
  channel_config_set_write_increment(&m_dmaChannelConfig, true);
  channel_config_set_dreq(&m_dmaChannelConfig,
                          pio_get_dreq(m_pio, m_stateMachine, false));
  dma_channel_configure(m_dmaChannel, &m_dmaChannelConfig, NULL,
                        &m_pio->rxf[m_stateMachine], RGB565_TOTAL_BYTES, false);
  dma_irqn_set_channel_enabled(kSpiDmaIrqIndex, m_dmaChannel, true);
  irq_set_exclusive_handler(kSpiDmaIrq, &SpiTransport::dmaHandler);
  irq_set_enabled(kSpiDmaIrq, true);

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

void SpiTransport::initDmdReader() {
  dmdreader_init();
  // Start SPI transfer, waiting in the background.
  dmdreader_spi_init();
}

void SpiTransport::SetAndEnableNewDmaTarget() {
  if (++m_rxBuffer >= NUM_BUFFERS) {
    m_rxBuffer = 0;
  }

  // Clear the interrupt request, enable a new transfer
  dma_irqn_acknowledge_channel(kSpiDmaIrqIndex, m_dmaChannel);
  dma_channel_set_write_addr(m_dmaChannel, buffers[m_rxBuffer], false);
  dma_channel_set_trans_count(m_dmaChannel, RGB565_TOTAL_BYTES, true);
}

void SpiTransport::initPio() {
  m_rxBuffer = NUM_BUFFERS;
  m_frameReceived = false;
  SetAndEnableNewDmaTarget();
  pio_sm_set_enabled(m_pio, m_stateMachine, false);
  pio_sm_clear_fifos(m_pio, m_stateMachine);
  pio_sm_restart(m_pio, m_stateMachine);
  pio_sm_set_enabled(m_pio, m_stateMachine, true);
}

void SpiTransport::dmaHandler() {
  if (!s_instance) return;
  s_instance->SetAndEnableNewDmaTarget();
  s_instance->SetFrameReceived();
}

bool SpiTransport::GetFrameReceived() {
  if (m_frameReceived) {
    m_frameReceived = false;
    return true;
  }

  if (m_loopback && digitalRead(SPI_TRANSPORT_ENABLE_PIN)) {
    // Turn on transport active flag immediately to avoid a logo to be
    // displayed.
    transportActive = true;
    m_loopback = false;

    dmdreader_loopback_stop();
    initPio();
  }

  return false;
}

uint8_t* SpiTransport::GetDataBuffer() {
  return buffers[(0 == m_rxBuffer) ? NUM_BUFFERS - 1 : m_rxBuffer - 1];
}

void SpiTransport::SetColor(Color color) { m_color = color; }

#endif
