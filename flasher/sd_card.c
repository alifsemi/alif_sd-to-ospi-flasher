/* Copyright (C) 2025 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */

#include "sd_card.h"
#include "board_defs.h"
#include "sd.h"

#include "Driver_IO.h"

// TODO: VSEL PORT and PIN are not yet set in BSP
//       so we do a shortcut here and define the VSEL pin for E4/E8 DevKit when RESET pin is defined 
#ifdef BOARD_SD_RESET_GPIO_PORT
#define BOARD_SD_VSEL_GPIO_PORT 6
#define BOARD_SD_VSEL_GPIO_PIN 3
#endif

#ifdef BOARD_SD_RESET_GPIO_PORT
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_SD_RESET_GPIO_PORT);
#endif

#ifdef BOARD_SD_VSEL_GPIO_PORT
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_SD_VSEL_GPIO_PORT);
#endif

void sd_card_reset(void)
{
#ifdef BOARD_SD_RESET_GPIO_PORT
    ARM_DRIVER_GPIO *gpioSD_RST = &ARM_Driver_GPIO_(BOARD_SD_RESET_GPIO_PORT);

    static bool gpio_initialized = false;
    if (!gpio_initialized) {
        gpio_initialized = true;
        gpioSD_RST->Initialize(BOARD_SD_RESET_GPIO_PIN, 0);
        gpioSD_RST->PowerControl(BOARD_SD_RESET_GPIO_PIN, ARM_POWER_FULL);
        gpioSD_RST->SetDirection(BOARD_SD_RESET_GPIO_PIN, GPIO_PIN_DIRECTION_OUTPUT);
    }

    gpioSD_RST->SetValue(BOARD_SD_RESET_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    sys_busy_loop_us(SDMMC_RESET_DELAY_US);
    gpioSD_RST->SetValue(BOARD_SD_RESET_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
#endif
}

void sd_card_select_io_voltage(bool io_1v8)
{
#ifdef BOARD_SD_VSEL_GPIO_PORT
    ARM_DRIVER_GPIO *gpioSD_VSEL = &ARM_Driver_GPIO_(BOARD_SD_VSEL_GPIO_PORT);

    static bool gpio_initialized = false;
    if (!gpio_initialized) {
        gpio_initialized = true;
        gpioSD_VSEL->Initialize(BOARD_SD_VSEL_GPIO_PIN, 0);
        gpioSD_VSEL->PowerControl(BOARD_SD_VSEL_GPIO_PIN, ARM_POWER_FULL);
        gpioSD_VSEL->SetDirection(BOARD_SD_VSEL_GPIO_PIN, GPIO_PIN_DIRECTION_OUTPUT);
    }
    gpioSD_VSEL->SetValue(BOARD_SD_VSEL_GPIO_PIN, io_1v8 ? GPIO_PIN_OUTPUT_STATE_HIGH : GPIO_PIN_OUTPUT_STATE_LOW);
#endif
}