/**
 * @file tla2518.c
 * @author Phil Hilger (phil@peergum.com)
 * @brief
 * @version 0.1
 * @date 2022-07-15
 *
 * Copyright (c) 2022 Waterloo Tech Inc.
 *
 */

#include "tla2518.h"
#include "esp_log.h"
#include "esp_err.h"
#include "pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#undef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(x)   do { esp_err_t rc = (x); if (rc != ESP_OK) { ESP_LOGE("err", "esp_err_t = %d", rc); assert(0 && #x);} } while(0);

#define DEBUG_LEVEL ESP_LOG_INFO
#define DEBUG_READ 0
#define DEBUG_WRITE 0
#define DEBUG_SAMPLE 0
#define STOP_ON_ERROR
#define SEMAPHORE_TIMEOUT 10
#define ADC_READ_RETRY_MAX 100
/* Log before/after each SPI call to pinpoint which call hangs (set tla2518 to DEBUG). */
#define TLA2518_SPI_HANG_DIAG 1
/* Max wait for any single SPI transaction; prevents etarTask from blocking forever if SPI hangs. */
#define SPI_TRANS_TIMEOUT_MS 50
/* Max iterations for init wait loops; avoids boot freeze if ADC never responds */
#define TLA2518_INIT_STATUS_RETRY_MAX  500
#define TLA2518_INIT_CAL_RETRY_MAX     1000

static esp_err_t writeRegister(uint8_t register, uint8_t value);
static esp_err_t readRegister(uint8_t register, uint8_t *value);
static esp_err_t readSample(uint16_t *value, uint8_t *channel);
static esp_err_t readTest(uint8_t register, uint8_t bitmask, uint8_t value);

static const char *TAG = "tla2518";

static const spi_device_interface_config_t devConfig = {
    .command_bits = 0,
    .address_bits = 0,
    .mode = 0,
    .clock_speed_hz = TLA2518_SPI_MASTER_FREQ,
    .spics_io_num = N_ADC_CS,
    .flags = SPI_DEVICE_NO_DUMMY,
    .queue_size = 1,
};

static spi_device_handle_t devHandle;

/* ADC semaphore: only tla2518.c take/give; only callers of getSample (adc.c readAndAverageStrum/String) are etar.c and util.c (calibration from etar/checkSleep). No other tasks use xADCSemaphore — deadlock from another task unlikely. */
extern SemaphoreHandle_t xADCSemaphore;

uint16_t adcValues[NUM_ADC_CHANNELS];

void tla2518_init(uint8_t hostId) {
  esp_log_level_set(TAG, DEBUG_LEVEL);
  xADCSemaphore = xSemaphoreCreateMutex();
  assert(xADCSemaphore != NULL);
  xSemaphoreGive(xADCSemaphore);

  ESP_LOGD(TAG, "Init driver");
  ESP_ERROR_CHECK(spi_bus_add_device(hostId, &devConfig, &devHandle));
  vTaskDelay(pdMS_TO_TICKS(50));

  spi_device_acquire_bus(devHandle, portMAX_DELAY);

  // reset BOR bit
  writeRegister(SYSTEM_STATUS_ADDRESS, SYSTEM_STATUS_DEFAULT | BOR_ERROR);

  // reset device and check status
  writeRegister(GENERAL_CFG_ADDRESS, GENERAL_CFG_DEFAULT | RST_START);
  int status_retries = 0;
  while (ESP_OK != readTest(SYSTEM_STATUS_ADDRESS, CRC_ERR_FUSE_ERROR, CRC_ERR_FUSE_OKAY)) {
    if (++status_retries >= TLA2518_INIT_STATUS_RETRY_MAX) {
      ESP_LOGE(TAG, "ADC init timeout waiting for status (boot freeze avoided)");
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  // set all INPUT as ANALOG + start calibration
  writeRegister(GENERAL_CFG_ADDRESS,
                GENERAL_CFG_DEFAULT | CH_RST_FORCE_AIN | CAL_START);
  int cal_retries = 0;
  while (ESP_OK != readTest(GENERAL_CFG_ADDRESS, CAL_MASK, CAL_COMPLETE)) {
    if (++cal_retries >= TLA2518_INIT_CAL_RETRY_MAX) {
      ESP_LOGE(TAG, "ADC init timeout waiting for calibration (boot freeze avoided)");
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (cal_retries < TLA2518_INIT_CAL_RETRY_MAX) {
    ESP_LOGI(TAG, "Calibrated");
  }
  // append channel ID to ADC data
  writeRegister(DATA_CFG_ADDRESS, DATA_CFG_DEFAULT | APPEND_STATUS_ID);
  // internal sampling rate to 250 kSPS (low bits: 0100) see Table 3 p.16
  // internal sampling rate to 1000 kSPS (low bits: 0000)
  // internal sampling rate to 125 kSPS (low bits: 0110)
  // Datasheet
  writeRegister(OPMODE_CFG_ADDRESS, OPMODE_CFG_DEFAULT | OSC_SEL_HIGH_SPEED | 0b0100);

  spi_device_release_bus(devHandle);
  ESP_LOGI(TAG, "Init done");
}

esp_err_t readTest(uint8_t reg, uint8_t bitmask, uint8_t testValue) {
  uint8_t value = 0;
  readRegister(reg, &value);
  if ((value & bitmask) != testValue) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

uint16_t getSample(int channel) {
  static uint32_t s_adc_call_count = 0;
  s_adc_call_count++;
  if (s_adc_call_count % 500 == 0) {
    ESP_LOGI(TAG, "freeze ckpt ADC getSample n=%lu ch=%d", (unsigned long)s_adc_call_count, channel);
  }
  // sampleChannels((1 << channel));
  sampleChannel(channel);
  /* Yield after every ADC read so we never run many reads back-to-back;
   * avoids intermittent freezes when simple strums hit paths with several reads. */
  vTaskDelay(0);
  return adcValues[channel];
}

void sampleChannel(uint8_t channel) {
  static uint32_t s_sample_ch_count = 0;
  uint16_t adcValue = 0;
  uint8_t adcChannel = 0;

  s_sample_ch_count++;
  bool trace = (s_sample_ch_count % 500 == 0);
  if (trace) {
    ESP_LOGI(TAG, "freeze ckpt ADC sampleChannel n=%lu ch=%u", (unsigned long)s_sample_ch_count, (unsigned)channel);
  }
  writeRegister(OSR_CFG_ADDRESS, OSR_CFG_DEFAULT | OSR_32);
  writeRegister(DATA_CFG_ADDRESS,
                DATA_CFG_DEFAULT /* | FIX_PAT_ENABLE */ | APPEND_STATUS_ID);
  writeRegister(SEQUENCE_CFG_ADDRESS,
                SEQUENCE_CFG_DEFAULT | SEQ_MODE_MANUAL);
  writeRegister(MANUAL_CH_SEL_ADDRESS,
                MANUAL_CH_SEL_DEFAULT | channel); // starting channel conversion
  if (trace) {
    ESP_LOGI(TAG, "freeze ckpt ADC sc after writes ch=%u", (unsigned)channel);
  }
  // writeRegister(SYSTEM_STATUS_ADDRESS, SYSTEM_STATUS_DEFAULT | OSR_DONE_MASK);
  // wait for ADC data averaging done
  // readSample(&adcValue, &adcChannel);
  // while (ESP_OK !=
  //        readTest(SYSTEM_STATUS_ADDRESS, OSR_DONE_MASK, OSR_DONE_COMPLETE)) {
  //   vTaskDelay(1);
  //        }
  // writeRegister(SYSTEM_STATUS_ADDRESS, SYSTEM_STATUS_DEFAULT | OSR_DONE_MASK);
  // readSample(&adcValue, &adcChannel); // discard at least one
  // sample
  int retries = 0;
  while (retries < ADC_READ_RETRY_MAX) {
    esp_err_t ret = readSample(&adcValue, &adcChannel);
    if (trace && retries > 0 && retries % 25 == 0) {
      ESP_LOGI(TAG, "freeze ckpt ADC sc retry %d ch=%u", retries, (unsigned)channel);
    }
    if (ESP_OK == ret && adcChannel == channel) {
      adcValues[adcChannel] = adcValue;
      break;
    }
    retries++;
    /* Always yield on retry (wrong channel or read failed) to avoid task freeze and watchdog */
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (trace) {
    ESP_LOGI(TAG, "freeze ckpt ADC sc after loop ch=%u retries=%d", (unsigned)channel, retries);
  }
  if (retries >= ADC_READ_RETRY_MAX) {
    ESP_LOGW(TAG, "ADC read timeout ch %u", channel);
  }
#if DEBUG_SAMPLE == 1
      ESP_LOGD(TAG, "[%d] %u", adcChannel, adcValue);
  #endif
}

void sampleChannels(uint8_t channelMask) {
  // averaging 64 samples
  writeRegister(OSR_CFG_ADDRESS, OSR_CFG_DEFAULT | OSR_128);
  writeRegister(AUTO_SEQ_CH_SEL_ADDRESS, channelMask);
  writeRegister(DATA_CFG_ADDRESS, DATA_CFG_DEFAULT /* | FIX_PAT_ENABLE */ | APPEND_STATUS_ID);
  writeRegister(SEQUENCE_CFG_ADDRESS,
                SEQUENCE_CFG_DEFAULT | SEQ_MODE_AUTO | SEQ_START_ASSEND);
  // writeRegister(SEQUENCE_CFG_ADDRESS,
  //               SEQUENCE_CFG_DEFAULT | SEQ_START_ASSEND);
  uint8_t status;
  // wait for sequencer to start
  while (ESP_OK != readTest(SYSTEM_STATUS_ADDRESS, SEQ_STATUS_MASK, SEQ_STATUS_RUNNING)) {
    vTaskDelay(10);
  }

  // wait for ADC data averaging done
  while (ESP_OK == readTest(SYSTEM_STATUS_ADDRESS, OSR_DONE_MASK, OSR_DONE_COMPLETE))
    ;
  uint16_t adcValue = 0;
  uint8_t adcChannel = 0;
  // readSample(&adcValue, &adcChannel); // pass one read (12 clocks min) before starting collecting samples
  for (int channel = 0; channel < 8; channel++) {
    if ((1 << channel) & channelMask) {
      int retries = 0;
      while (retries < ADC_READ_RETRY_MAX) {
        if (ESP_OK == readSample(&adcValue, &adcChannel) && adcChannel == channel) {
          adcValues[adcChannel] = adcValue;
#if DEBUG_SAMPLE == 1
          ESP_LOGD(TAG, "[%d] %u", adcChannel, adcValue);
#endif
          break;
        }
        retries++;
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      if (retries >= ADC_READ_RETRY_MAX) {
        ESP_LOGW(TAG, "ADC read timeout ch %d", channel);
      }
    }
  }
  writeRegister(SEQUENCE_CFG_ADDRESS, SEQUENCE_CFG_DEFAULT | SEQ_START_END);
  // clear OSR_DONE bit
  // writeRegister(SYSTEM_STATUS_ADDRESS, SYSTEM_STATUS_DEFAULT | OSR_DONE_MASK);
}

esp_err_t writeRegister(uint8_t reg, uint8_t value) {
  if (xSemaphoreTake(xADCSemaphore, SEMAPHORE_TIMEOUT) == pdFALSE) {
    ESP_LOGW(TAG, "ADC semaphore timeout (freeze diag): writeRegister reg=%02x", reg);
    return ESP_FAIL;
  }

  esp_err_t ret;
  spi_transaction_t transaction;
  uint8_t rx_buffer[4], tx_buffer[4];

  tx_buffer[0] = SINGLE_REGISTER_WRITE;
  tx_buffer[1] = reg;
  tx_buffer[2] = value;
  transaction.length = 3 * 8;
  transaction.rxlength = 0;
  transaction.rx_buffer = rx_buffer;
  transaction.tx_buffer = tx_buffer;
  transaction.flags = 0;   // SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
#if TLA2518_SPI_HANG_DIAG
  ESP_LOGD(TAG, "freeze diag: before spi write reg=%02x", reg);
#endif
  ret = spi_device_queue_trans(devHandle, &transaction, portMAX_DELAY);
  if (ret == ESP_OK) {
    spi_transaction_t *rtrans = NULL;
    ret = spi_device_get_trans_result(devHandle, &rtrans, pdMS_TO_TICKS(SPI_TRANS_TIMEOUT_MS));
    if (ret == ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "SPI transaction timeout write reg=%02x (freeze avoided)", reg);
      ret = ESP_ERR_TIMEOUT;
    }
  }
#if TLA2518_SPI_HANG_DIAG
  ESP_LOGD(TAG, "freeze diag: after spi write reg=%02x", reg);
#endif
  if (ret == ESP_OK) {
#if DEBUG_WRITE == 1
    ESP_LOGD(TAG, "wrote reg %02x = %02x", reg, value);
#endif
  } else {
    ESP_LOGE(TAG, "err %d (%04x) on reg %02x write", ret, ret, reg);
  }
  xSemaphoreGive(xADCSemaphore);
#ifdef STOP_ON_ERROR
  ESP_ERROR_CHECK(ret);
#endif
  return ret;
}

esp_err_t readRegister(uint8_t reg, uint8_t *value) {
  if (xSemaphoreTake(xADCSemaphore, SEMAPHORE_TIMEOUT) == pdFALSE) {
    ESP_LOGW(TAG, "ADC semaphore timeout (freeze diag): readRegister reg=%02x", reg);
    return ESP_FAIL;
  }

  esp_err_t ret;
  *value = 0;
  uint8_t rx_buffer[4], tx_buffer[4];
  spi_transaction_t transaction;
  tx_buffer[0] = SINGLE_REGISTER_READ;
  tx_buffer[1] = reg;
  tx_buffer[2] = 0;
  transaction.length = 3 * 8;
  transaction.rxlength = 0 * 8;
  transaction.rx_buffer = rx_buffer;
  transaction.tx_buffer = tx_buffer;
  transaction.flags = 0; //SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
#if TLA2518_SPI_HANG_DIAG
  ESP_LOGD(TAG, "freeze diag: before spi read cmd reg=%02x", reg);
#endif
  ret = spi_device_queue_trans(devHandle, &transaction, portMAX_DELAY);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
    xSemaphoreGive(xADCSemaphore);
    return ret;
  }
  spi_transaction_t *rtrans = NULL;
  ret = spi_device_get_trans_result(devHandle, &rtrans, pdMS_TO_TICKS(SPI_TRANS_TIMEOUT_MS));
  if (ret == ESP_ERR_TIMEOUT) {
    ESP_LOGW(TAG, "SPI transaction timeout read cmd reg=%02x (freeze avoided)", reg);
    xSemaphoreGive(xADCSemaphore);
    return ESP_ERR_TIMEOUT;
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
    xSemaphoreGive(xADCSemaphore);
    return ret;
  }
  transaction.length = 3 * 8;
  transaction.rxlength = 3 * 8;
  transaction.flags = 0;  // SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
#if TLA2518_SPI_HANG_DIAG
  ESP_LOGD(TAG, "freeze diag: before spi read data reg=%02x", reg);
#endif
  ret = spi_device_queue_trans(devHandle, &transaction, portMAX_DELAY);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
    xSemaphoreGive(xADCSemaphore);
    return ret;
  }
  rtrans = NULL;
  ret = spi_device_get_trans_result(devHandle, &rtrans, pdMS_TO_TICKS(SPI_TRANS_TIMEOUT_MS));
  if (ret == ESP_ERR_TIMEOUT) {
    ESP_LOGW(TAG, "SPI transaction timeout read data reg=%02x (freeze avoided)", reg);
    xSemaphoreGive(xADCSemaphore);
    return ESP_ERR_TIMEOUT;
  }
#if TLA2518_SPI_HANG_DIAG
  ESP_LOGD(TAG, "freeze diag: after spi read reg=%02x", reg);
#endif
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
  } else {
    *value = rx_buffer[0];
  #if DEBUG_READ == 1
    ESP_LOGD(TAG, "rx_buffer:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, 3, ESP_LOG_DEBUG);
    ESP_LOGD(TAG, "Reg %02x = %02x", reg, *value);
  #endif
  }
  xSemaphoreGive(xADCSemaphore);
  return ret;
}

esp_err_t readSample(uint16_t *value, uint8_t *channel) {
#if TLA2518_SPI_HANG_DIAG
  ESP_LOGD(TAG, "freeze diag: readSample entry");
#endif
  if (xSemaphoreTake(xADCSemaphore, SEMAPHORE_TIMEOUT) == pdFALSE) {
    ESP_LOGW(TAG, "ADC semaphore timeout (freeze diag): readSample");
    return ESP_FAIL;
  }

  esp_err_t ret;
  *value = 0;
  uint8_t rx_buffer[4], tx_buffer[4];
  spi_transaction_t transaction;
  tx_buffer[0] = 0;
  tx_buffer[1] = 0;
  tx_buffer[2] = 0;
  // transaction.rx_data[0] = 0xff;
  // transaction.rx_data[1] = 0xff;
  // transaction.rx_data[2] = 0xff;
  // transaction.tx_data[3] = 0;
  transaction.length = 2 * 8 + 4;
  transaction.rxlength = 2 * 8 + 4;
  transaction.rx_buffer = rx_buffer;
  transaction.tx_buffer = tx_buffer;
  transaction.flags = 0; // SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
#if TLA2518_SPI_HANG_DIAG
  ESP_LOGD(TAG, "freeze diag: before spi readSample");
#endif
  ret = spi_device_queue_trans(devHandle, &transaction, portMAX_DELAY);
  if (ret == ESP_OK) {
    spi_transaction_t *rtrans = NULL;
    ret = spi_device_get_trans_result(devHandle, &rtrans, pdMS_TO_TICKS(SPI_TRANS_TIMEOUT_MS));
    if (ret == ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "SPI transaction timeout readSample (freeze avoided)");
      xSemaphoreGive(xADCSemaphore);
      return ESP_ERR_TIMEOUT;
    }
  }
#if TLA2518_SPI_HANG_DIAG
  ESP_LOGD(TAG, "freeze diag: after spi readSample");
#endif
  if (ret == ESP_OK) {
#if DEBUG_SAMPLE == 2
    ESP_LOGD(TAG, "sample_buffer:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, 3, ESP_LOG_DEBUG);
#endif
    *value = ((uint16_t)rx_buffer[0] << 8) + rx_buffer[1];
    *channel = rx_buffer[2] >> 4;
  } else {
    ESP_LOGE(TAG, "err %d (%04x) on sample read", ret, ret);
  }
  xSemaphoreGive(xADCSemaphore);
  return ret;
}