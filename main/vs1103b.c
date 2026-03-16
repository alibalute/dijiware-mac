/**
 * @file vs1103b.c
 * @author Phil Hilger (phil@peergum.com)
 * @brief
 * @version 0.1
 * @date 2022-07-15
 *
 * Copyright (c) 2022 Waterloo Tech Inc.
 *
 */

#include "vs1103b.h"
#include "esp_log.h"
#include "esp_err.h"
#include "pins.h"
#include <math.h>

#define DEBUG_LEVEL ESP_LOG_INFO
#define SEMAPHORE_TIMEOUT 20
/* Max iterations waiting for synth to leave reset; avoids boot freeze if VS1103 never responds */
#define VS1103B_RESET_POLL_MAX  500

//#define SINE_WAVE_TEST        // start tests
#define SINE_WAVE_FREQ 3      // (0-7) wave frequency
#define SINE_WAVE_DIVIDER 30  // (0-31) see vs1103b doc

static esp_err_t readRegister(uint8_t reg, uint16_t *value);
static esp_err_t writeRegister(uint8_t reg, uint16_t value);
static void initiateSineWave(void);
static esp_err_t writeSDI(uint8_t *buffer, int size);
static void waitForDREQ(void);

static const char *TAG = "vs1103b";

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t xAudioSemaphore;

static const spi_device_interface_config_t devConfig = {
    .mode = 0,
    .command_bits = 8,
    .address_bits = 8,
    .clock_speed_hz = VS1103B_SPI_MASTER_FREQ,
    .spics_io_num = N_VS_CS,
    // .flags = SPI_DEVICE_NO_DUMMY,
    .queue_size = 1,
};

static const spi_device_interface_config_t sdiConfig = {
    .mode = 0,
    .clock_speed_hz = VS1103B_SPI_MASTER_FREQ,
    .spics_io_num = N_VS_XDCS,
    .queue_size = 1,
};

static spi_device_handle_t devHandle;
static spi_device_handle_t sdiHandle;

void vs1103b_resetAudio(void) {
  resetAudioChip();

  waitForDREQ();
  uint16_t value;

#ifdef SINE_WAVE_TEST
  value = (SM_TESTS | SM_SDINEW) & ~(SM_SDISHARE | SM_SDIORD | SM_DACT);
  // value |= ~(SM_ICONF) | ((3UL << 6) & SM_ICONF);
  writeRegister(SCI_MODE, value);

  initiateSineWave();

  // test proper init
  readRegister(SCI_STATUS, &value);
  uint8_t version = (value & 0xf0) >> 4;
  ESP_LOGD(TAG, "Version %d [vs1103b = 7]", version);
  assert(version == 7);
  setGain(25, 25, 25, 25, 1);
  ESP_LOGI(TAG, "gain is set in sine wave test");

#endif

#ifdef SINE_WAVE_TEST
  while (1) {
    
    vTaskDelay(1);
  }
#endif

  setClockF(XTALIx4_0, 12288000); // set clockf to 48MHz

  // spi_device_acquire_bus(devHandle, portMAX_DELAY);

  writeRegister(SCI_MODE, SM_RESET);
  vTaskDelay(pdMS_TO_TICKS(5));

  uint16_t smDefault;
  // ensure RESET happened
  int reset_poll = 0;
  while (ESP_OK != readRegister(SCI_MODE, &value) ||
         ((value & SM_RESET) != 0)) {
    if (++reset_poll >= VS1103B_RESET_POLL_MAX) {
      ESP_LOGE(TAG, "Synth reset poll timeout (boot freeze avoided)");
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  smDefault = value; // save default value

  ESP_LOGD(TAG, "Default SCI_MODE value = %04x", smDefault);

  smDefault |= SM_LINE_IN;
  writeRegister(SCI_MODE, smDefault); // ensure line in, not mic
  // setBassAndTrebleBoost(0, 0, 0, 0); // no boost
}

#define VS1103B_INIT_VERIFY_RETRIES 3
#define VS1103B_POST_INIT_MS 120  /* let chip stabilize before MIDI (no-sound boot fix) */

void vs1103b_init(uint8_t hostId) {
  esp_log_level_set(TAG, DEBUG_LEVEL);
  ESP_LOGD(TAG, "Init driver");
  xAudioSemaphore = xSemaphoreCreateMutex();
  assert(xAudioSemaphore != NULL);

  ESP_ERROR_CHECK(spi_bus_add_device(hostId, &devConfig, &devHandle));
  ESP_ERROR_CHECK(spi_bus_add_device(hostId, &sdiConfig, &sdiHandle));

  uint16_t status = 0;
  int attempt;
  for (attempt = 0; attempt < VS1103B_INIT_VERIFY_RETRIES; attempt++) {
    vs1103b_resetAudio();
    vTaskDelay(pdMS_TO_TICKS(20));  /* brief settle after reset */
    if (readRegister(SCI_STATUS, &status) == ESP_OK) {
      ESP_LOGI(TAG, "Synth verify OK (attempt %d), SCI_STATUS=0x%04x", attempt + 1, (unsigned)status);
      break;
    }
    ESP_LOGW(TAG, "Synth not ready (attempt %d), retrying init", attempt + 1);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (attempt >= VS1103B_INIT_VERIFY_RETRIES) {
    ESP_LOGE(TAG, "Synth init verify failed after %d attempts", VS1103B_INIT_VERIFY_RETRIES);
  }

  setVolume(0, true); // no sound at boot
  setGain(0, 0, 0, 0, 0);

  vTaskDelay(pdMS_TO_TICKS(VS1103B_POST_INIT_MS));  /* ensure MIDI path ready before first UART */
  ESP_LOGI(TAG, "Init done");
}

esp_err_t vs1103b_read_status(uint16_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return readRegister(SCI_STATUS, out_status);
}

void initiateSineWave(void) {
  uint8_t buffer[8] = {
      0x53, 0xef, 0x6e, (SINE_WAVE_FREQ << 5) | SINE_WAVE_DIVIDER, 0, 0, 0, 0};
  writeSDI(buffer, sizeof(buffer));
}

/**
 * @brief Set ClockF
 *
 * @param multiplier (0-7)
 * @param freq (in Hz)
 */
void setClockF(uint16_t multiplier, uint32_t freq) {
  uint16_t value = (multiplier & 0x07) << 13 | ((freq - 8000000) / 4000);
  writeRegister(SCI_CLOCKF, value);
}

/**
 * @brief set bass and treble booster
 *
 * @param bassBoostDB db to boost in steps of 1.0dB (1 to 15, 0 = off)
 * @param bassCutFreq in 10Hz steps (2 to 15)
 * @param trebleBoostDB db to boost in steps of 1.5dB (-8 to 7, 0 = off)
 * @param trebleCutFreq in 1000Hz steps (0 to 15)
 */
void setBassAndTrebleBoost(uint16_t bassBoost, uint16_t bassCut,
                           uint16_t trebleBoost, uint16_t trebleCut) {
  writeRegister(SCI_BASS, (bassCut & 0x0f) | ((bassBoost & 0x0f) << 4) |
                              ((trebleCut & 0x0f) << 8) |
                              ((trebleBoost & 0x0f) << 12));
}

/**
 * @brief Set the Volume
 *
 * @param volume (0-100)
 * @return ESP_OK on success (so caller can retry on failure and avoid no-sound boot)
 */
esp_err_t setVolume(uint8_t volume, bool usingHeadPhone) {
  if (volume == 0) {
    ESP_LOGD(TAG, "Setting volume 0 (mute)");
    return writeRegister(SCI_VOL, 0xFEFE);  /* full attenuation = silence, min volume */
  }
  uint8_t v = (uint8_t)roundf(100.0 * logf((float)volume) / logf(100.0));
  uint16_t value = ((uint32_t)(100 - v) * 0x19 / 100) * 4;  // min -25dB
  ESP_LOGD(TAG, "Setting volume (%u) to %d", volume, value);
  return writeRegister(SCI_VOL, value | (value << 8));
}

/**
 * @brief Set the Gain
 *
 * @param gain1 from 0 [-25dB] to 31 [+6dB] in 1dB steps
 * @param gain2 from 0 [-25dB] to 31 [+6dB] in 1dB steps
 * @param gain3 from 0 [-25dB] to 31 [+6dB] in 1dB steps
 * @param enabled on/off
 */
void setGain(uint16_t gain1, uint16_t gain2, uint16_t gain3, uint16_t gain4,
             bool enabled) {
  uint16_t value = ((gain1 & 0x1f << 0)) | ((gain2 & 0x1f << 5)) |
                   ((gain3 & 0x1f << 10)) | ((enabled ? 1 : 0) << 15);
  ESP_LOGI(TAG, "gain = %x", value);                
  writeRegister(SCI_MIXERVOL, value);
  if (gain4 > 0) {
    value = (gain4 & 0x1f) | SARC_MANUALGAIN;
    writeRegister(SCI_ADPCMRECCTL, value);
  }
}

esp_err_t writeSDI(uint8_t *buffer, int size) {
  if (xSemaphoreTake(xAudioSemaphore, pdMS_TO_TICKS(100)) == pdFALSE) {
    ESP_LOGE(TAG, "Failed to obtain Semaphore");
    return ESP_FAIL;
  }
  esp_err_t ret;
  spi_transaction_t transaction;
  uint8_t tx_buffer[2];
  transaction.tx_buffer = buffer;
  transaction.length = size * 8;
  transaction.rxlength = 0;
  transaction.flags = SPI_TRANS_USE_RXDATA;
  ret = spi_device_polling_transmit(sdiHandle, &transaction);
  if (ret == ESP_OK) {
    ESP_LOGD(TAG, "wrote %d SDI bytes", size);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, size, ESP_LOG_DEBUG);
  } else {
    ESP_LOGE(TAG, "failed to write %d SDI bytes", size);
  }

  waitForDREQ();
  xSemaphoreGive(xAudioSemaphore);
#ifdef STOP_ON_ERROR
  ESP_ERROR_CHECK(ret);
#endif
  return ret;
}

esp_err_t writeRegister(uint8_t reg, uint16_t value) {
  if (xSemaphoreTake(xAudioSemaphore, SEMAPHORE_TIMEOUT) == pdFALSE) {
    ESP_LOGE(TAG, "Failed to obtain Semaphore");
    return ESP_FAIL;
  }
  esp_err_t ret;
  spi_transaction_t transaction;
  uint8_t tx_buffer[2];
  transaction.cmd = WRITE_INSTRUCTION;
  transaction.addr = reg;
  transaction.tx_data[0] = value >> 8;
  transaction.tx_data[1] = value & 0xff;
  // transaction.tx_buffer = tx_buffer;
  // tx_buffer[0] = value >> 8;
  // tx_buffer[1] = value & 0xff;
  transaction.length = 2 * 8;
  transaction.rxlength = 0;
  transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret == ESP_OK) {
    ESP_LOGD(TAG, "wrote reg %02x = %04x", reg, value);
  } else {
    ESP_LOGE(TAG, "err %d (%04x) on reg %02x write", ret, ret, reg);
  }

  waitForDREQ();
  xSemaphoreGive(xAudioSemaphore);
#ifdef STOP_ON_ERROR
  ESP_ERROR_CHECK(ret);
#endif
  return ret;
}

esp_err_t readRegister(uint8_t reg, uint16_t *value) {
  if (xSemaphoreTake(xAudioSemaphore, SEMAPHORE_TIMEOUT) == pdFALSE) {
    ESP_LOGE(TAG, "Failed to obtain Semaphore");
    return ESP_FAIL;
  }
  esp_err_t ret;
  *value = 0;
  uint8_t rx_buffer[2];
  spi_transaction_t transaction;
  transaction.cmd = READ_INSTRUCTION;
  transaction.addr = reg;
  // transaction.rx_buffer = rx_buffer;
  transaction.length = 2 * 8;
  transaction.rxlength = 2 * 8;
  transaction.flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA;
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "err %d (%04x) on read reg %02x", ret, ret, reg);
  } else {
    *value = (((uint16_t)(transaction.rx_data[0])) << 8) + transaction.rx_data[1];
    // *value = (((uint16_t)(rx_buffer[0])) << 8) + rx_buffer[1];
    ESP_LOGD(TAG, "Reg %02x = %04x", reg, *value);
  }
  waitForDREQ();
  xSemaphoreGive(xAudioSemaphore);
  return ret;
}

/**
 * @brief wait for transaction to be finished
 * 
 */
void waitForDREQ(void) {
  // wait for DREQ up
  for (int retries = 0; retries<100; retries++) {
    if (gpio_get_level(VS_DREQ) >0) {
      break;
    }
    vTaskDelay(1);
  }
}