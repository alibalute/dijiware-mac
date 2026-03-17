/**
 * @file mc3419.c
 * @author Phil Hilger (phil@peergum.com)
 * @brief
 * @version 0.1
 * @date 2022-07-15
 *
 * Copyright (c) 2022 Waterloo Tech Inc.
 *
 */

#include "mc3419.h"
#include "esp_log.h"
#include "esp_err.h"
#include "pins.h"
#include <string.h>

#define DEBUG_LEVEL ESP_LOG_INFO
#define DEBUG_READ 0
#define DEBUG_WRITE 0
#define STOP_ON_ERROR

static esp_err_t writeRegister(uint8_t register, uint8_t value);
static esp_err_t readRegister(uint8_t register, uint8_t *value);
static esp_err_t readRegisterBurst(uint8_t register, uint8_t *value,
                                   uint8_t size);
// esp_err_t readSample(uint16_t *value, uint8_t *channel);
static esp_err_t readTest(uint8_t register, uint8_t bitmask);
static esp_err_t readCompare(uint8_t reg, uint8_t expectedValue);

static const char *TAG = "mc3419";

static const spi_device_interface_config_t devConfig = {
    .mode = 0,
    .command_bits = 0,
    .address_bits = 8,
    .clock_speed_hz = MC3419_SPI_MASTER_FREQ,
    .spics_io_num = N_AXL_CS,
    .flags = 0,
    .queue_size = 1,
};

static spi_device_handle_t devHandle;

typedef enum {
  STANDBY,
  WAKE,
} Mc3419State;
static Mc3419State state = STANDBY;
// uint16_t axlValues[NUM_ADC_CHANNELS];

void mc3419_init(uint8_t hostId) {
  esp_log_level_set(TAG, DEBUG_LEVEL);
  ESP_LOGD(TAG, "Init driver");
  ESP_ERROR_CHECK(spi_bus_add_device(hostId, &devConfig, &devHandle));
  vTaskDelay(pdMS_TO_TICKS(50));
  spi_device_acquire_bus(devHandle, portMAX_DELAY);
  // standby mode for configuration
  ESP_ERROR_CHECK(readCompare(RD_CNT, 0x06));
  ESP_ERROR_CHECK(writeRegister(MODE, 0x00)); // mode stanbdy (for prog)
  // wait for idle
   while (readTest(DEV_STAT,0x80)) {
    vTaskDelay(pdMS_TO_TICKS(5));
  } 
  ESP_ERROR_CHECK(writeRegister(COMM_CTRL, 0x00)); // 4-wire SPI and default interrupts
  ESP_ERROR_CHECK(writeRegister(SR, (MC3419_SPI_MASTER_FREQ < 4000000U ? 0x16 : 0x0E))); // 500Hz sample rate
  ESP_ERROR_CHECK(
      writeRegister(MOTION_CTRL, 0b10111000));  // motion settings (all enabled)

  ESP_ERROR_CHECK(writeRegister(MODE, 0x01)); // mode wake (for monitoring)
  // wait for idle

   while (readTest(DEV_STAT, 0x80)) {
    vTaskDelay(pdMS_TO_TICKS(5));
  } 
  // uint8_t value;
  // readRegister(DEV_STAT, &value);
  // readRegister(FIFO_STAT, &value);
  // readRegister(FIFO_TH, &value);
  spi_device_release_bus(devHandle);
  ESP_LOGI(TAG, "Init done");
}

/**
 * @brief return Z accelerometer value
 *
 * @return uint16_t
 */
uint16_t get_acceleration(void) {
  uint8_t buffer[2] = {0,0};
  readRegisterBurst(ZOUT_EX_L, buffer, sizeof(buffer));
  return (uint16_t)((buffer[1] << 8) + buffer[0]);
}

/**
 * @brief Read X and Y accelerometer values (for x-y plane rotation / twist gesture).
 */
void get_acceleration_xy(int16_t *out_x, int16_t *out_y) {
  uint8_t buf[4];
  readRegisterBurst(XOUT_EX_L, buf, sizeof(buf));
  *out_x = (int16_t)((uint16_t)buf[1] << 8 | buf[0]);
  *out_y = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
}

esp_err_t readTest(uint8_t reg, uint8_t bitmask) {
  uint8_t value;
  readRegister(reg, &value);
  if (value & bitmask) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t readCompare(uint8_t reg, uint8_t expectedValue) {
  uint8_t value;
  readRegister(reg, &value);
  if (value != expectedValue) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

// void sampleChannels(uint8_t channels) {
//   writeRegister(AUTO_SEQ_CH_SEL_ADDRESS, channels);
//   writeRegister(DATA_CFG_ADDRESS, DATA_CFG_DEFAULT | FIX_PAT_ENABLE);
//   writeRegister(SEQUENCE_CFG_ADDRESS,
//                 SEQUENCE_CFG_DEFAULT | SEQ_MODE_AUTO);
//   writeRegister(SEQUENCE_CFG_ADDRESS,
//                 SEQUENCE_CFG_DEFAULT | SEQ_START_ASSEND);
//   uint8_t status;
//   while (ESP_OK != readTest(SYSTEM_STATUS_ADDRESS, SEQ_STATUS_MASK))
//     ;

//   for (int channel = 0; channel < 8; channel++) {
//     if ((1 << channel) & channels) {
//       uint16_t adcValue;
//       uint8_t adcChannel;
//       // while (true) {
//       while (ESP_OK != readTest(SYSTEM_STATUS_ADDRESS, OSR_DONE_MASK))
//         ;
//       if (ESP_OK ==
//           readSample(&adcValue, &adcChannel) /*  && adcChannel == channel */) {
//         adcValues[adcChannel] = adcValue;
//         ESP_LOGD(TAG, "Channel %d, value = %u", adcChannel, adcValue);
//         // break;
//       }
//       // }
//     }
//   }
//   writeRegister(SEQUENCE_CFG_ADDRESS, SEQUENCE_CFG_DEFAULT | SEQ_START_END);
// }

esp_err_t writeRegister(uint8_t reg, uint8_t value) {
  esp_err_t ret;
  spi_transaction_t transaction;
  transaction.addr = reg & 0x7f;
  transaction.tx_data[0] = value;
  transaction.length = 1 * 8;
  transaction.rxlength = 0;
  transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret == ESP_OK) {
#if DEBUG_WRITE == 1
    ESP_LOGD(TAG, "wrote reg %02x = %02x", reg, value);
#endif
  } else {
    ESP_LOGE(TAG, "err %d (%04x) on reg %02x write", ret, ret, reg);
  }
#ifdef STOP_ON_ERROR
  ESP_ERROR_CHECK(ret);
#endif
  return ret;
}

esp_err_t writeRegisterBurst(uint8_t reg, uint8_t *value, uint8_t size) {
  esp_err_t ret;
  uint8_t rx_buffer[16], tx_buffer[16];
  spi_transaction_t transaction;
  transaction.addr = reg & 0x7f;
  transaction.tx_buffer = value;
  transaction.rx_buffer = rx_buffer;
  transaction.length = size * 8;
  transaction.rxlength = 0;
  transaction.flags = 0;
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
    return ret;
  }
#if DEBUG_WRITE == 1
  ESP_LOGD(TAG, "Wrote to register %02x (%d bytes):", reg, size);
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, value, size, ESP_LOG_DEBUG);
#endif
  return ret;
}

esp_err_t readRegister(uint8_t reg, uint8_t *value) {
  esp_err_t ret;
  *value = 0;
  spi_transaction_t transaction;
  transaction.addr = reg | 0x80;
  transaction.tx_data[0] = 0;
  transaction.tx_data[1] = 0;
  transaction.rx_data[0] = 0xff;
  transaction.rx_data[1] = 0xff;
  transaction.length = 2 * 8;
  transaction.rxlength = 1 * 8;
  transaction.flags =
      SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
    return ret;
  }
  *value = transaction.rx_data[1];
#if DEBUG_READ == 1
  ESP_LOGD(TAG, "Reg %02x = %02x", reg, *value);
#endif
  return ret;
}

esp_err_t readRegisterBurst(uint8_t reg, uint8_t *value, uint8_t size) {
  esp_err_t ret;
  uint8_t rx_buffer[32], tx_buffer[32];
  spi_transaction_t transaction;
  transaction.addr = reg | 0x80;
  transaction.rx_buffer = rx_buffer;
  transaction.tx_buffer = tx_buffer;
  transaction.length = (1 + size) * 8;
  transaction.rxlength = size * 8;
  transaction.flags = 0;
  memset((void *)rx_buffer, 0, sizeof(rx_buffer));
  memset((void *)tx_buffer, 0, sizeof(tx_buffer));
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
    return ret;
  }
  memcpy((void *)value, (void *)(rx_buffer+1), size);
#if DEBUG_READ == 1
  //ESP_LOGD(TAG, "Register %02x (%d bytes):", reg, size);
  //ESP_LOG_BUFFER_HEX_LEVEL(TAG, value, size, ESP_LOG_DEBUG);
#endif
  return ret;
}

// esp_err_t readSample(uint16_t *value, uint8_t *channel) {
//   esp_err_t ret;
//   *value = 0;
//   spi_transaction_t transaction;
//   transaction.tx_data[0] = 0;
//   transaction.tx_data[1] = 0;
//   transaction.tx_data[2] = 0;
//   transaction.tx_data[3] = 0;  
//   transaction.length = 4 * 8;
//   transaction.rxlength = 4 * 8;
//   transaction.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
//   ret = spi_device_polling_transmit(devHandle, &transaction);
//   if (ret == ESP_OK) {
//     *value = *(uint16_t *)transaction.rx_data;
//     *channel = (transaction.rx_data[2] >> 4);
//   } else {
//     ESP_LOGE(TAG, "err %d (%04x) on sample read", ret, ret);
//   }
//   return ret;
// }