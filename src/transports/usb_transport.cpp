#include "usb_transport.h"

#include <Arduino.h>

#include <cstdint>

#include "main.h"
#include "utility/clock.h"

UsbTransport::UsbTransport() : Transport() { m_type = USB; }

UsbTransport::~UsbTransport() { deinit(); }

bool UsbTransport::init() {
#ifdef BOARD_HAS_PSRAM
  xTaskCreatePinnedToCore(Task_ReadSerial, "Task_ReadSerial", 8192, this, 1,
                          &m_task, 0);
#else
  xTaskCreatePinnedToCore(Task_ReadSerial, "Task_ReadSerial", 4096, this, 1,
                          &m_task, 0);
#endif

  m_active = true;

  return true;
}

bool UsbTransport::deinit() {
  if (m_active) {
    m_active = false;
    // TODO ? clean exit ?
    // delay(500);
    // vTaskDelete(m_task);
  }

  return true;
}

void UsbTransport::Task_ReadSerial(void* pvParameters) {
  const auto transport = static_cast<UsbTransport*>(pvParameters);
  const uint16_t usbPackageSize = usbPackageSizeMultiplier * 32;
  bool connected = false;

#ifdef PICO_BUILD
  tud_cdc_set_ignore_dtr(1);
  tud_cdc_set_rx_buffer_size(usbPackageSize + 128);
#else
  Serial.setRxBufferSize(usbPackageSize + 128);
  Serial.setTxBufferSize(64);
#endif
#if (defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1)
  // S3 USB CDC. The actual baud rate doesn't matter.
  Serial.begin(115200);
  while (!Serial) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  GetDisplayDriver()->DisplayText("USB CDC", 0, 0, 0, 0, 0, true);
#else
  Serial.setTimeout(SERIAL_TIMEOUT);
  Serial.begin(SERIAL_BAUD);
  while (!Serial) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (1 == debug) {
    DisplayNumber(SERIAL_BAUD, (SERIAL_BAUD >= 1000000 ? 7 : 6), 0, 0, 0, 0, 0,
                  true);
  } else {
    GetDisplayDriver()->DisplayText("USB UART", 0, 0, 0, 0, 0, true);
  }
#endif

#ifdef BOARD_HAS_PSRAM
  const auto pUsbBuffer = static_cast<uint8_t*>(
      heap_caps_malloc(usbPackageSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT));
#else
  const auto pUsbBuffer = static_cast<uint8_t*>(malloc(usbPackageSize));
#endif

  if (nullptr == pUsbBuffer) {
    GetDisplayDriver()->DisplayText("out of memory", 0, 0, 255, 0, 0);
    while (1);
  }

  payloadMissing = 0;
  headerBytesReceived = 0;
  numCtrlCharsFound = 0;

  Clock timeoutClock;
  size_t received = 0;
  size_t expected = 0;
  uint8_t numFrameCharsFound = 0;
  uint8_t result = 0;

  while (transport->isActive()) {
    timeoutClock.restart();
    numFrameCharsFound = 0;
    // Wait for FRAME header
    while (numFrameCharsFound < N_FRAME_CHARS) {
      if (Serial.available()) {
        if (Serial.read() == FrameChars[numFrameCharsFound]) {
          numFrameCharsFound++;
        } else {
          numFrameCharsFound = 0;
        }
      } else {
        if (timeoutClock.getElapsedTime().asMilliseconds() >
            CONNECTION_TIMEOUT) {
          transportActive = false;
          timeoutClock.restart();
        }
        // Avoid busy-waiting
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }

    expected = usbPackageSize - N_FRAME_CHARS;
    transportActive = true;
    timeoutClock.restart();

    while (transport->isActive()) {
      // Wait for data to be ready
      if (Serial.available() >= expected ||
          (!connected && Serial.available() >= (N_CTRL_CHARS + 4))) {
        memset(pUsbBuffer, 0, usbPackageSize);
        received = Serial.readBytes(pUsbBuffer, expected);
        result = HandleData(pUsbBuffer, received);
        expected = usbPackageSize;
        if (2 == result) {
          // Error
          Serial.write(CtrlChars, N_CTRL_CHARS);
          Serial.write('F');
          Serial.flush();
          vTaskDelay(pdMS_TO_TICKS(2));
          Serial.end();
          vTaskDelay(pdMS_TO_TICKS(2));
          Serial.begin(SERIAL_BAUD);
          while (!Serial) {
            vTaskDelay(pdMS_TO_TICKS(1));
          }
          break;  // Wait for the next FRAME header
        }
        connected = true;
        if (3 == result) {
          break;  // fast ack has been sent, wait for the next FRAME header
        }
        Serial.write(CtrlChars, N_ACK_CHARS);
        Serial.flush();
        if (1 == result) break;  // Wait for the next FRAME header
        timeoutClock.restart();
      } else {
        if (timeoutClock.getElapsedTime().asMilliseconds() >
            CONNECTION_TIMEOUT) {
          transportActive = false;
          timeoutClock.restart();
          break;  // Wait for the next FRAME header
        }
        // Avoid busy-waiting
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
  }

  Serial.end();

#ifdef BOARD_HAS_PSRAM
  heap_caps_free(pUsbBuffer);
#else
  free(pUsbBuffer);
#endif
}