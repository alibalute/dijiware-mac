/**
 * @file mcp23s08.cpp
 * @author Phil Hilger (phil.hilger@waterlooti.com)
 * @brief
 * @version 0.1
 * @date 2022-05-17
 *
 * We use 2 expanders
 * For simple set/get API, we pass a channel between 0-15:
 *  0-7 = expander1 (EX1_CS)
 *  8-15 = expander2 (EX2_CS)
 *
 * (c) Copyright 2022, Waterloo Tech Inc.
 *
 */

#include "mcp23s08.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define DEBUG_LEVEL ESP_LOG_INFO
#define DEBUG_WRITE 0
#define DEBUG_READ 0
// #define STOP_ON_ERROR
#define SEMAPHORE_TIMEOUT 50

static const char *TAG = "mcp23s08";

extern SemaphoreHandle_t xExpanderSemaphore;

static const spi_device_interface_config_t dev1Config = {
    .command_bits = 8,
    .address_bits = 8,
    .mode = 0,
    .clock_speed_hz = 8000000UL, // MCP23S08_SPI_MASTER_FREQ,
    .spics_io_num = N_EX1_CS,
    .flags = SPI_DEVICE_NO_DUMMY,
    .queue_size = 1,
};

static const spi_device_interface_config_t dev2Config = {
    .command_bits = 8,
    .address_bits = 8,
    .mode = 0,
    .clock_speed_hz = 8000000UL, // MCP23S08_SPI_MASTER_FREQ,
    .spics_io_num = N_EX2_CS,
    .flags = SPI_DEVICE_NO_DUMMY,
    .queue_size = 1,
};

static spi_device_handle_t dev1Handle;
static spi_device_handle_t dev2Handle;

static esp_err_t writeRegister(spi_device_handle_t devHandle, uint8_t reg,
                               uint8_t value);
static esp_err_t writeRegisterBurst(spi_device_handle_t devHandle, uint8_t reg,
                                    uint8_t *value, uint8_t size);
static esp_err_t readRegister(spi_device_handle_t devHandle, uint8_t reg,
                              uint8_t *value);
static void softReset(spi_device_handle_t h, uint8_t dir, uint8_t value);

void mcp23s08_init(uint8_t hostId)
{
  esp_log_level_set(TAG, DEBUG_LEVEL);
  xExpanderSemaphore = xSemaphoreCreateMutex();
  assert(xExpanderSemaphore != NULL);
  xSemaphoreGive(xExpanderSemaphore);

  ESP_LOGD(TAG, "Init driver");
  ESP_ERROR_CHECK(spi_bus_add_device(hostId, &dev1Config, &dev1Handle));
  ESP_ERROR_CHECK(spi_bus_add_device(hostId, &dev2Config, &dev2Handle));
  vTaskDelay(pdMS_TO_TICKS(50));

  softReset(dev1Handle, 0x02, 0x08); // all pins on EX1 are outputs, set EX_N_VS_RST to 1   (0x00 for enable 5V)(0x02 for disable 5V)
  vTaskDelay(pdMS_TO_TICKS(50));
  softReset(dev2Handle, 0xFF, 0x00); // all pins on EX2 are inputs
  /* for (int i = 0; i < 16; i++)
  {
    bool a = mcp23s08_get_level(i);
    ESP_LOGD(TAG, "ch#%d -> %d", i, a ? 1 : 0);
  } */
  // // reset BOR bit
  // writeRegister(SYSTEM_STATUS_ADDRESS, SYSTEM_STATUS_DEFAULT | BOR_ERROR);

  // // reset device and check status
  // writeRegister(GENERAL_CFG_ADDRESS, GENERAL_CFG_DEFAULT | RST_START);
  // ESP_ERROR_CHECK(readTest(SYSTEM_STATUS_ADDRESS, CRC_ERR_FUSE_ERROR,
  // CRC_ERR_FUSE_OKAY));
  // // vTaskDelay(100);

  // // set all INPUT as ANALOG + start calibration
  // writeRegister(GENERAL_CFG_ADDRESS,
  //               GENERAL_CFG_DEFAULT | CH_RST_FORCE_AIN | CAL_START);
  // while (ESP_OK != readTest(GENERAL_CFG_ADDRESS, CAL_MASK, CAL_COMPLETE))
  //   ;
  // ESP_LOGD(TAG, "Calibrated");
  // // append channel ID to ADC data
  // writeRegister(DATA_CFG_ADDRESS, DATA_CFG_DEFAULT | APPEND_STATUS_ID);
  // // internal sampling rate to 250 kSPS (low bits: 0100) see Table 3 p.16
  // // internal sampling rate to 1000 kSPS (low bits: 0000)
  // // internal sampling rate to 125 kSPS (low bits: 0110)
  // // Datasheet
  // writeRegister(OPMODE_CFG_ADDRESS, OPMODE_CFG_DEFAULT | OSC_SEL_HIGH_SPEED
  // | 0b0100);

  ESP_LOGI(TAG, "Init done");
}

void softReset(spi_device_handle_t devHandle, uint8_t dir, uint8_t value)
{
  spi_device_acquire_bus(devHandle, portMAX_DELAY);

  uint8_t registers[OLAT - IODIR + 1];
  // default registers to 0 (default)
  memset((void *)registers, 0,
         sizeof(registers));
  registers[IODIR] = dir;
  registers[GPIO] = value;
  writeRegisterBurst(devHandle, IODIR, registers, sizeof(registers));

  spi_device_release_bus(devHandle);
}

void mcp23s08_set_level(int channel, bool level)
{
  uint8_t mask = 1UL << (channel % 8);
  uint8_t port = channel / 8;
  spi_device_handle_t devHandle = (port == 0) ? dev1Handle : dev2Handle;
  ESP_LOGD(TAG, "set output %d on exp %d to %d", channel % 8, port + 1, level ? 1 : 0);
  // check current latch value
  uint8_t value;
  readRegister(devHandle, GPIO, &value);
  ESP_LOGD(TAG, "read (GPIO) EX#%d ch#%d=%d [%02x]", port + 1, channel, (value & mask) ? 1 : 0, value);
  // readRegister(devHandle, OLAT, &value);
  // ESP_LOGD(TAG, "read (OLAT) EX#%d ch#%d=%02x", port+1, channel, value);
  ESP_LOGD(TAG, "write (GPIO) EX#%d ch#%d=%d [%02x]", port + 1, channel, level ? 1 : 0,
           level ? (value | mask) : (value & ~mask));
  // set/reset proper bit
  writeRegister(devHandle, GPIO,
                level ? (value | mask) : (value & ~mask));
}

bool mcp23s08_get_level(int channel)
{
  uint8_t mask = 1UL << (channel % 8);
  uint8_t port = channel / 8;
  spi_device_handle_t devHandle = (port == 0) ? dev1Handle : dev2Handle;      
  // check current latch value
  uint8_t value;
  readRegister(devHandle, GPIO, &value);
  return (value & mask) != 0;
}

esp_err_t writeRegister(spi_device_handle_t devHandle, uint8_t reg,
                        uint8_t value)
{
  if (xSemaphoreTake(xExpanderSemaphore, ( TickType_t )SEMAPHORE_TIMEOUT) == pdFALSE) {
    return ESP_FAIL;
  }
  esp_err_t ret;
  spi_transaction_t transaction;
  transaction.cmd = CMD_WRITE;
  transaction.addr = reg;
  transaction.tx_data[0] = value;
  transaction.length = 1 * 8;
  transaction.rxlength = 0;
  transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret == ESP_OK)
  {
#if DEBUG_WRITE == 1
    ESP_LOGD(TAG, "wrote EX#%d reg %02x = %02x", (devHandle == dev1Handle) ? 1 : 2, reg, value);
#endif
  }
  else
  {
    ESP_LOGE(TAG, "err %d (%04x) on EX#%d reg %02x write",
             (devHandle == dev1Handle) ? 1 : 2, ret, ret, reg);
  }
  xSemaphoreGive(xExpanderSemaphore);
#ifdef STOP_ON_ERROR
  ESP_ERROR_CHECK(ret);
#endif
  return ret;
}

esp_err_t writeRegisterBurst(spi_device_handle_t devHandle, uint8_t reg,
                             uint8_t *value, uint8_t size)
{
  if (xSemaphoreTake(xExpanderSemaphore, ( TickType_t )SEMAPHORE_TIMEOUT) == pdFALSE) {
    return ESP_FAIL;
  }

  esp_err_t ret;
  uint8_t rx_buffer[32], tx_buffer[32];
  spi_transaction_t transaction;
  transaction.cmd = CMD_WRITE;
  transaction.addr = reg;
  transaction.tx_buffer = value;
  transaction.rx_buffer = rx_buffer;
  transaction.length = size * 8;
  transaction.rxlength = 0;
  transaction.flags = 0;
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "err %d (%04x) on read EX#%d reg %02x",
             (devHandle == dev1Handle) ? 1 : 2, ret, ret, reg);
  } else {
#if DEBUG_WRITE == 1
  ESP_LOGD(TAG, "Wrote to EX#%d register %02x (%d bytes):",
           (devHandle == dev1Handle) ? 1 : 2, reg, size);
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, value, size, ESP_LOG_DEBUG);
#endif
  }
  xSemaphoreGive(xExpanderSemaphore);
  return ret;
}

esp_err_t readRegister(spi_device_handle_t devHandle, uint8_t reg,
                       uint8_t *value)
{
  if (xSemaphoreTake(xExpanderSemaphore,( TickType_t )SEMAPHORE_TIMEOUT) == pdFALSE) {
    return ESP_FAIL;
  }
  esp_err_t ret;
  *value = 0;
  spi_transaction_t transaction;
  transaction.cmd = CMD_READ;
  transaction.addr = reg;
  transaction.tx_data[0] = 0;
  transaction.rx_data[0] = 0xff;
  transaction.length = 1 * 8;
  transaction.rxlength = 1 * 8;
  transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  ret = spi_device_polling_transmit(devHandle, &transaction);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "err %d (%04x) on EX#%d read reg %02x",
             (devHandle == dev1Handle) ? 1 : 2, ret, ret, reg);
  } else {
    *value = transaction.rx_data[0];
#if DEBUG_READ == 1
    ESP_LOGD(TAG, "EX#%d Reg %02x = %02x", (devHandle == dev1Handle) ? 1 : 2, reg,
            *value);
#endif
  }
  xSemaphoreGive(xExpanderSemaphore);
  return ret;
}
