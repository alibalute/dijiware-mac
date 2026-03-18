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
#include "esp_system.h"
#include "pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#undef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(x)   do { esp_err_t rc = (x); if (rc != ESP_OK) { ESP_LOGE("err", "esp_err_t = %d", rc); assert(0 && #x);} } while(0);

#define DEBUG_LEVEL ESP_LOG_INFO
#define DEBUG_READ 0
#define DEBUG_WRITE 0
#define DEBUG_SAMPLE 0
#undef STOP_ON_ERROR
#define SEMAPHORE_TIMEOUT 10
#define ADC_READ_RETRY_MAX 100
/* Pre-remove drain (legacy ISR-queued trans only). */
#define SPI_DRAIN_TIMEOUT_MS 400
#define SPI_DRAIN_RETRIES 12
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
static spi_host_device_t s_adc_spi_host = SPI_HOST_MAX;
static bool s_adc_spi_host_valid = false;

/** Last-ditch drain before remove_device (only needed if legacy ISR-queued trans stuck). */
static void drain_before_remove(void) {
  spi_transaction_t *rtrans = NULL;
  for (int i = 0; i < SPI_DRAIN_RETRIES; i++) {
    if (spi_device_get_trans_result(devHandle, &rtrans, pdMS_TO_TICKS(SPI_DRAIN_TIMEOUT_MS)) ==
        ESP_OK) {
      return;
    }
  }
}

static esp_err_t adc_polling_write(uint8_t reg, uint8_t val) {
  uint8_t tx[3] = { SINGLE_REGISTER_WRITE, reg, val };
  uint8_t rx[4];
  spi_transaction_t t = { 0 };
  t.length = 3 * 8;
  t.rxlength = 0;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  return spi_device_polling_transmit(devHandle, &t);
}

static esp_err_t adc_polling_read(uint8_t reg, uint8_t *out) {
  uint8_t tx[3] = { SINGLE_REGISTER_READ, reg, 0 };
  uint8_t rx[4];
  spi_transaction_t t = { 0 };
  t.length = 3 * 8;
  t.rxlength = 0;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  esp_err_t e = spi_device_polling_transmit(devHandle, &t);
  if (e != ESP_OK) {
    return e;
  }
  t.length = 3 * 8;
  t.rxlength = 3 * 8;
  e = spi_device_polling_transmit(devHandle, &t);
  if (e == ESP_OK && out) {
    *out = rx[0];
  }
  return e;
}

static esp_err_t adc_registers_reinit_polling(void) {
  esp_err_t e = adc_polling_write(SYSTEM_STATUS_ADDRESS, SYSTEM_STATUS_DEFAULT | BOR_ERROR);
  if (e != ESP_OK) {
    return e;
  }
  e = adc_polling_write(GENERAL_CFG_ADDRESS, GENERAL_CFG_DEFAULT | RST_START);
  if (e != ESP_OK) {
    return e;
  }
  for (int n = 0; n < TLA2518_INIT_STATUS_RETRY_MAX; n++) {
    uint8_t v = 0;
    e = adc_polling_read(SYSTEM_STATUS_ADDRESS, &v);
    if (e != ESP_OK) {
      return e;
    }
    if ((v & CRC_ERR_FUSE_ERROR) == CRC_ERR_FUSE_OKAY) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  e = adc_polling_write(GENERAL_CFG_ADDRESS,
                        GENERAL_CFG_DEFAULT | CH_RST_FORCE_AIN | CAL_START);
  if (e != ESP_OK) {
    return e;
  }
  for (int n = 0; n < TLA2518_INIT_CAL_RETRY_MAX; n++) {
    uint8_t v = 0;
    e = adc_polling_read(GENERAL_CFG_ADDRESS, &v);
    if (e != ESP_OK) {
      return e;
    }
    if ((v & CAL_MASK) == CAL_COMPLETE) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  e = adc_polling_write(DATA_CFG_ADDRESS, DATA_CFG_DEFAULT | APPEND_STATUS_ID);
  if (e != ESP_OK) {
    return e;
  }
  return adc_polling_write(OPMODE_CFG_ADDRESS,
                           OPMODE_CFG_DEFAULT | OSC_SEL_HIGH_SPEED | 0b0100);
}

static esp_err_t adc_polling_read_sample(uint16_t *value, uint8_t *channel) {
  uint8_t rx_buffer[4], tx_buffer[4] = { 0, 0, 0 };
  spi_transaction_t transaction = { 0 };
  transaction.length = 2 * 8 + 4;
  transaction.rxlength = 2 * 8 + 4;
  transaction.rx_buffer = rx_buffer;
  transaction.tx_buffer = tx_buffer;
  esp_err_t e = spi_device_polling_transmit(devHandle, &transaction);
  if (e == ESP_OK) {
    *value = ((uint16_t)rx_buffer[0] << 8) + rx_buffer[1];
    *channel = rx_buffer[2] >> 4;
  }
  return e;
}

/** Remove/re-add SPI device and re-init ADC when queue is stuck (must hold xADCSemaphore). */
static esp_err_t tla2518_spi_bus_recovery(void) {
  if (!s_adc_spi_host_valid || s_adc_spi_host >= SPI_HOST_MAX) {
    ESP_LOGE(TAG, "SPI recovery: no host stored");
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGW(TAG, "ADC SPI bus recovery: remove/re-add device");
  drain_before_remove();
  esp_err_t r = spi_bus_remove_device(devHandle);
  if (r == ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG,
             "ADC SPI: unfinished ISR transaction (cannot remove device); restarting");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_ERR_INVALID_STATE;
  }
  if (r != ESP_OK) {
    ESP_LOGW(TAG, "spi_bus_remove_device returned %d", (int)r);
    return r;
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  r = spi_bus_add_device(s_adc_spi_host, &devConfig, &devHandle);
  if (r != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device failed %d", (int)r);
    return r;
  }
  vTaskDelay(pdMS_TO_TICKS(20));
  r = adc_registers_reinit_polling();
  if (r != ESP_OK) {
    ESP_LOGE(TAG, "adc_registers_reinit_polling failed %d", (int)r);
    return r;
  }
  ESP_LOGI(TAG, "ADC SPI bus recovery OK");
  return ESP_OK;
}

/* ADC semaphore: only tla2518.c take/give; only callers of getSample (adc.c readAndAverageStrum/String) are etar.c and util.c (calibration from etar/checkSleep). No other tasks use xADCSemaphore — deadlock from another task unlikely. */
extern SemaphoreHandle_t xADCSemaphore;

uint16_t adcValues[NUM_ADC_CHANNELS];

void tla2518_init(uint8_t hostId) {
  esp_log_level_set(TAG, DEBUG_LEVEL);
  xADCSemaphore = xSemaphoreCreateMutex();
  assert(xADCSemaphore != NULL);
  xSemaphoreGive(xADCSemaphore);

  s_adc_spi_host = (spi_host_device_t)hostId;
  s_adc_spi_host_valid = true;

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
  sampleChannel(channel);
  /* Yield after every ADC read so we never run many reads back-to-back;
   * avoids intermittent freezes when simple strums hit paths with several reads. */
  vTaskDelay(0);
  return adcValues[channel];
}

void sampleChannel(uint8_t channel) {
  uint16_t adcValue = 0;
  uint8_t adcChannel = 0;

  if (writeRegister(OSR_CFG_ADDRESS, OSR_CFG_DEFAULT | OSR_32) != ESP_OK) {
    return;
  }
  if (writeRegister(DATA_CFG_ADDRESS,
                DATA_CFG_DEFAULT /* | FIX_PAT_ENABLE */ | APPEND_STATUS_ID) != ESP_OK) {
    return;
  }
  if (writeRegister(SEQUENCE_CFG_ADDRESS,
                SEQUENCE_CFG_DEFAULT | SEQ_MODE_MANUAL) != ESP_OK) {
    return;
  }
  if (writeRegister(MANUAL_CH_SEL_ADDRESS,
                MANUAL_CH_SEL_DEFAULT | channel) != ESP_OK) {
    return;
  }
  int retries = 0;
  while (retries < ADC_READ_RETRY_MAX) {
    esp_err_t ret = readSample(&adcValue, &adcChannel);
    if (ESP_OK == ret && adcChannel == channel) {
      adcValues[adcChannel] = adcValue;
      break;
    }
    retries++;
    /* Always yield on retry (wrong channel or read failed) to avoid task freeze and watchdog */
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (retries >= ADC_READ_RETRY_MAX) {
    ESP_LOGW(TAG, "ADC read timeout ch %u", channel);
  }
#if DEBUG_SAMPLE == 1
      ESP_LOGD(TAG, "[%d] %u", adcChannel, adcValue);
  #endif
}

void sampleChannels(uint8_t channelMask) {
  if (writeRegister(OSR_CFG_ADDRESS, OSR_CFG_DEFAULT | OSR_128) != ESP_OK) {
    return;
  }
  if (writeRegister(AUTO_SEQ_CH_SEL_ADDRESS, channelMask) != ESP_OK) {
    return;
  }
  if (writeRegister(DATA_CFG_ADDRESS, DATA_CFG_DEFAULT /* | FIX_PAT_ENABLE */ | APPEND_STATUS_ID) != ESP_OK) {
    return;
  }
  if (writeRegister(SEQUENCE_CFG_ADDRESS,
                SEQUENCE_CFG_DEFAULT | SEQ_MODE_AUTO | SEQ_START_ASSEND) != ESP_OK) {
    return;
  }
  int wait_retries = 0;
  while (ESP_OK != readTest(SYSTEM_STATUS_ADDRESS, SEQ_STATUS_MASK, SEQ_STATUS_RUNNING)) {
    if (++wait_retries >= 50) {
      ESP_LOGW(TAG, "sampleChannels SEQ_STATUS wait timeout");
      return;
    }
    vTaskDelay(10);
  }
  wait_retries = 0;
  while (ESP_OK == readTest(SYSTEM_STATUS_ADDRESS, OSR_DONE_MASK, OSR_DONE_COMPLETE)) {
    if (++wait_retries >= 500) {
      ESP_LOGW(TAG, "sampleChannels OSR_DONE wait timeout");
      (void)writeRegister(SEQUENCE_CFG_ADDRESS, SEQUENCE_CFG_DEFAULT | SEQ_START_END);
      return;
    }
  }
  uint16_t adcValue = 0;
  uint8_t adcChannel = 0;
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
  (void)writeRegister(SEQUENCE_CFG_ADDRESS, SEQUENCE_CFG_DEFAULT | SEQ_START_END);
}

esp_err_t writeRegister(uint8_t reg, uint8_t value) {
  if (xSemaphoreTake(xADCSemaphore, SEMAPHORE_TIMEOUT) == pdFALSE) {
    ESP_LOGW(TAG, "ADC semaphore timeout writeRegister reg=%02x", reg);
    return ESP_FAIL;
  }

  /* Polling-only: never uses ISR queue, so remove_device recovery always works. */
  esp_err_t ret = adc_polling_write(reg, value);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "ADC polling write fail reg=%02x err=%d, recovery", reg, (int)ret);
    if (tla2518_spi_bus_recovery() == ESP_OK) {
      ret = adc_polling_write(reg, value);
    }
  }
  if (ret == ESP_OK) {
#if DEBUG_WRITE == 1
    ESP_LOGD(TAG, "wrote reg %02x = %02x", reg, value);
#endif
  } else {
    ESP_LOGE(TAG, "err %d (%04x) on reg %02x write", ret, ret, reg);
  }
  xSemaphoreGive(xADCSemaphore);
  return ret;
}

esp_err_t readRegister(uint8_t reg, uint8_t *value) {
  if (xSemaphoreTake(xADCSemaphore, SEMAPHORE_TIMEOUT) == pdFALSE) {
    ESP_LOGW(TAG, "ADC semaphore timeout readRegister reg=%02x", reg);
    return ESP_FAIL;
  }

  *value = 0;
  esp_err_t ret = adc_polling_read(reg, value);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "ADC polling read fail reg=%02x err=%d, recovery", reg, (int)ret);
    if (tla2518_spi_bus_recovery() == ESP_OK) {
      ret = adc_polling_read(reg, value);
    }
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
  }
#if DEBUG_READ == 1
  else {
    ESP_LOGD(TAG, "Reg %02x = %02x", reg, *value);
  }
#endif
  xSemaphoreGive(xADCSemaphore);
  return ret;
}

esp_err_t readSample(uint16_t *value, uint8_t *channel) {
  if (xSemaphoreTake(xADCSemaphore, SEMAPHORE_TIMEOUT) == pdFALSE) {
    ESP_LOGW(TAG, "ADC semaphore timeout readSample");
    return ESP_FAIL;
  }

  *value = 0;
  esp_err_t ret = adc_polling_read_sample(value, channel);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "ADC polling sample err=%d, recovery", (int)ret);
    if (tla2518_spi_bus_recovery() == ESP_OK) {
      ret = adc_polling_read_sample(value, channel);
    }
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on sample read", ret, ret);
  }
  xSemaphoreGive(xADCSemaphore);
  return ret;
}