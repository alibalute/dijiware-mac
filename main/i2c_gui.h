/**
 * @file i2c_gui.h
 * @author Ali Tehrani (atehrani@gmail.com)
 * @brief
 * @version 0.1
 * @date 2023-04-8
 *
 * (c) Copyright 2023, Elementali ID Inc.
 *
 */

#ifndef __I2C_GUI_H_
#define __I2C_GUI_H_

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "soc/i2c_reg.h"
#include "esp_log.h"


#define I2C_MASTER_TIMEOUT_MS       1000

//master doesnt need buffer
#define I2C_MASTER_RX_BUF_DISABLE  0
#define I2C_MASTER_TX_BUF_DISABLE  0



#endif // __I2C_GUI_H_
