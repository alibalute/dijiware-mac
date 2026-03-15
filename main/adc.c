/**
 * @file adc.c
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#include "adc.h"
#include "pins.h"
#include "tla2518.h"
#include "mc3419.h"

#define MIN(a,b) ((a<b) ? a : b)

// #define VOLUME_DEBUG
// #define BATTERY_DEBUG

// #include "i2c_adc.h"

// static esp_adc_cal_characteristics_t adc1_chars;
// static esp_adc_cal_characteristics_t adc2_chars;
static bool adc_calibrated = false;

static adc_cali_handle_t adc_cali_handle;
static adc_oneshot_unit_handle_t adc1_handle;

static const char *TAG = "ADC";

static SemaphoreHandle_t adcConversionSemaphore;

// static const adc_channel_t strumADCChannel[NUM_STRINGS] = {
//     STRUM_ONE_ADC_CHANNEL,   STRUM_TWO_ADC_CHANNEL,
//     STRUM_THREE_ADC_CHANNEL, STRUM_FOUR_ADC_CHANNEL,/*
//     STRUM_FIVE_ADC_CHANNEL,  STRUM_SIX_ADC_CHANNEL,
//     STRUM_SEVEN_ADC_CHANNEL, STRUM_EIGHT_ADC_CHANNEL, */
// };

void initADC(void) {
  adc_calibration_init();
  // init_i2c_adc();
}

void adc_calibration_init(void) {
  adc_calibrated = false;
  adcConversionSemaphore = xSemaphoreCreateBinary();
  assert(adcConversionSemaphore != NULL);
  xSemaphoreGive(adcConversionSemaphore);

  // --- old method (pre IDF 5.0)
  // ret = esp_adc_cal_check_efuse(ADC_EXAMPLE_CALIBRATION_SCHEME);
  // if (ret == ESP_ERR_NOT_SUPPORTED) {
  //   ESP_LOGW(TAG,
  //            "Calibration scheme not supported, skip software calibration");
  // } else if (ret == ESP_ERR_INVALID_VERSION) {
  //   ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
  // } else if (ret == ESP_OK) {
  //   adc_calibrated = true;
  //   esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN, ADC_WIDTH_BIT, 0,
  //                            &adc1_chars);
  //   esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN, ADC_WIDTH_BIT, 0,
  //                            &adc2_chars);
  // } else {
  //   ESP_LOGE(TAG, "Invalid arg");
  // }
  // ESP_ERROR_CHECK(adc1_config_channel_atten(BATT_ADC_CHANNEL, ADC_ATTEN));
  // ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT));

  ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = ADC_UNIT_1,
      .atten = ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));

  adc_calibrated = true;

  adc_oneshot_unit_init_cfg_t init_config1 = {
      .unit_id = ADC_UNIT_1,
      .ulp_mode = false,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_11,
  };
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc1_handle, BATT_ADC_CHANNEL, &config));
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc1_handle, VOL_ADC_CHANNEL, &config));

  // TO DO when done using ADC:
  // ESP_LOGI(TAG, "delete %s calibration scheme", "Curve Fitting");
  // ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
  // ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
}

uint16_t readBattery(void) {
  // adc_power_acquire();
  int voltage = 0;
  int samples = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    int batt_raw;
    if (ESP_OK == adc_oneshot_read(adc1_handle, BATT_ADC_CHANNEL, &batt_raw)) {
  #ifdef BATTERY_DEBUG
      ESP_LOGD(TAG, "raw batt: %d", batt_raw);
  #endif
      voltage += batt_raw;
      samples++;
    }
    vTaskDelay(1);
  }
  if (samples>0) {
    voltage /= samples;
  } else {
    voltage = 0;
  }
  int batt_voltage;
  if (adc_calibrated &&
      xSemaphoreTake(adcConversionSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
    ESP_ERROR_CHECK(
        adc_cali_raw_to_voltage(adc_cali_handle, voltage, &batt_voltage));
    xSemaphoreGive(adcConversionSemaphore);
#ifdef BATTERY_DEBUG
    ESP_LOGD(TAG, "calibrated batt: %d mV", batt_voltage);
#endif
  } else {
    batt_voltage = (voltage * ADC_MAX_VOLTAGE) / 4095U;
  }
  // adc_power_release();
  return (uint16_t)((uint32_t)(batt_voltage) * 4000/703);
}

uint16_t readAccelerometer(void) {
  bool filtered = false;
  uint16_t acc = readAndAverageAccelerometer(filtered);
  // ESP_LOGD(TAG, "acc = %u", acc);
  return acc;
}

uint16_t readAndAverageAccelerometer(bool filtered) {
  return get_acceleration();
  // return 0;
  // adc_power_acquire();
  // uint32_t accel = 0;
  // int samples = 0;
  // for (int i = 0; i < ADC_SAMPLES; i++) {
  //   uint32_t accel_raw = adc1_get_raw(ACCEL_ADC_CHANNEL);
  //   ESP_LOGI(TAG, "raw accel: %u", accel_raw);
  //   if (!filtered || (accel >= ACCEL_MIN && accel <= ACCEL_MAX)) {
  //     accel += accel_raw;
  //     samples++;
  //   }
  //   // vTaskDelay(pdMS_TO_TICKS(1));
  // }
  // adc_power_release();
  // return accel / samples;
}

/**
 * @brief read volume as voltage in mV
 * 
 * @return uint32_t 
 */
uint32_t readVolumeVoltage(void) {
  // adc_power_acquire();
  int volume = 0;
  int samples = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    int volume_raw;
    if (ESP_OK == adc_oneshot_read(adc1_handle, VOL_ADC_CHANNEL, &volume_raw)) {
  #ifdef VOLUME_DEBUG
      ESP_LOGD(TAG, "raw volume: %d", volume_raw);
  #endif
      volume += volume_raw;
      samples++;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (samples>0) {
    volume /= samples;
  } else {
    volume = 0;
  }
  int volume_voltage;
  if (adc_calibrated &&
      xSemaphoreTake(adcConversionSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
    ESP_ERROR_CHECK(
        adc_cali_raw_to_voltage(adc_cali_handle, volume, &volume_voltage));
    xSemaphoreGive(adcConversionSemaphore);
#ifdef VOLUME_DEBUG
    ESP_LOGD(TAG, "calibrated volume: %d mV", volume_voltage);
#endif
  } else {
    volume_voltage = (volume * ADC_MAX_VOLTAGE) / 4095U;
#ifdef VOLUME_DEBUG
    ESP_LOGD(TAG, "Non calibrated volume: %d mV", volume_voltage);
#endif
  }

  // adc_power_release();
  return (uint32_t)(volume_voltage);
}

/**
 * @brief read volume as 0-100 (float)
 * 
 * @return float 
 */
uint8_t readVolume(void) {
  static uint8_t lastRead = 0;
  uint8_t value =
      (uint32_t)(MIN(readVolumeVoltage(), MAX_VOLUME)) * 100 / MAX_VOLUME;
  /* Minimum ADC value at knob physical minimum is 8; treat 0–8 as zero volume (mute) */
  if (value <= 8) {
    value = 0;
  }
  if (ABS(value,lastRead)>=5) {
    ESP_LOGI(TAG, "Volume = %u", value);
    lastRead = value;
  }
  return value;
}

uint16_t readAndAverageStrum(int position) {
  return getSample(position + ADC_CHANNEL_STRUM1)>>4;
}

uint16_t readAndAverageString(int position) {
  uint16_t value = getSample(position + ADC_CHANNEL_STRING1);
  return value;
}

uint16_t readADC(int v_channel) {
  bool filtered = false;
  return readAndAverageADC(v_channel,filtered);
}

uint16_t readAndAverageADC(int v_channel, bool filtered) {
  if (v_channel == ACCELEROMETER) {
    return readAndAverageAccelerometer(filtered);
  } else if (v_channel >= STRUM_ONE && v_channel < STRUM_LAST) {
    return readAndAverageStrum(v_channel - STRUM_ONE);
  } else if (v_channel>= STRING_ONE && v_channel < STRING_LAST) {
    return readAndAverageString(v_channel);
  }
  // not handled
  return 0;
  // // read and average external ADC samples
  // uint32_t value = 0;
  // int samples = 0;
  // for (int i = 0; i < ADC_SAMPLES; i++) {
  //   uint32_t value_raw = read_sample(v_channel);
  //   ESP_LOGI(TAG, "raw value: %u", value_raw);
  //   if (!filtered || (value >= STRING_MIN && value <= STRING_MAX)) {
  //     value += value_raw;
  //     samples++;
  //   }
  //   // vTaskDelay(pdMS_TO_TICKS(1));
  // }
  // return value / samples;
}