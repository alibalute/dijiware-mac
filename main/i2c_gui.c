
/**
 * @file i2c_gui.cpp
 * @author Ali Tehrani (atehrani@gmail.com)
 * @brief
 * @version 0.1
 * @date 2023-04-8
 *
 * (c) Copyright 2023, Elementali ID Inc.
 *
 */

#include "i2c_gui.h"




/**
 * @brief init I2C bus as slave and install driver
 *
 */


static const char *TAG = "i2c-gui";







// esp_err_t i2c_slave_init()
// {
//     i2c_config_t conf;
//     conf.mode = I2C_MODE_SLAVE;
//     conf.sda_io_num = 9;
//     conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
//     conf.scl_io_num = 8;
//     conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
//     conf.slave.addr_10bit_en = 0;
//     conf.slave.slave_addr = I2C_SLAVE_ADDRESS;
//     //conf.master.clk_speed = 400000;
//     conf.clk_flags = 0;   //this line is super important, without it, slave wont be initialized 
//     //conf.slave.maximum_speed = 400000;
    
    
//     i2c_param_config(I2C_NUM_0, &conf);
//     return i2c_driver_install(I2C_NUM_0, conf.mode, 16, 16, 0); //(NOTE: eTar doesnt like buffer size greater than 16)

// }


// void handleGuiMessages (uint8_t * data)
// {

//         printf("handling the gui message \n");
//         for (int i = 0; i < 3; i++) {
//             printf("%02x ", data[i]);
//         }
//         printf("\n");

// }


