/* Copyright (C) 2024 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */
#include <stdio.h>
#include <inttypes.h>

/* ThreadX and FileX Includes */
#include "tx_api.h"
#include "fx_api.h"
#include "fx_sd_driver.h"
#include "RTE_Components.h"
#include CMSIS_device_header

#include "Driver_IO.h"
#include "se_services_port.h"

#if defined(RTE_CMSIS_Compiler_STDOUT)
#include "retarget_init.h"
#include "Driver_Common.h"
#endif

#include "leds.h"
#include "ext_flash.h"
#include "sd_card.h"
#include "board_defs.h"
#include "board_config.h"

// ThreadX RTOS and SD card
#define K (1024)
#define STACK_POOL_SIZE (64*K)
#define SD_STACK_SIZE (16*K)
#define SD_BUF_SIZE (8*K)
TX_THREAD program_thread;
UCHAR pool_memory_area[STACK_POOL_SIZE];
TX_BYTE_POOL StackPool;

unsigned char *p_sdStack = NULL;
/* Buffer for FileX FX_MEDIA sector cache. This must be large enough for at least one sector , which are typically 512 bytes in size. */
UCHAR media_memory[SD_BUF_SIZE] __attribute__((section("sd_dma_buf"))) __attribute__((aligned(32)));
FX_MEDIA sd_card;

void clock_init(bool enable)
{
    uint32_t service_error_code = 0;
    /* Enable Clocks */
    uint32_t error_code = SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_CLK_100M, enable, &service_error_code);
    if(error_code || service_error_code){
        printf("SE: 100MHz clock enable error_code=%u se_error_code=%u\n", error_code, service_error_code);
        return;
    }

    error_code = SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_HFOSC, enable, &service_error_code);
    if(error_code || service_error_code){
        printf("SE: HFOSC enable error_code=%u se_error_code=%u\n", error_code, service_error_code);
        return;
    }
}


void program_thread_entry(ULONG args)
{
    bool status = ext_flash_init();

    sd_card_select_io_voltage(false); // Use 3.3V initially
    sd_card_reset();

    /* Open the SD disk. and initialize SD controller */
    memset(&sd_card,0, sizeof(sd_card));

    uint32_t fx_media_status = fx_media_open(&sd_card, "SD_DISK", _fx_sd_driver, 0, (VOID *)media_memory, sizeof(media_memory));
    if (fx_media_status != FX_SUCCESS)
    {
        printf("media open fail status = %d...\n", fx_media_status);
        status = false;
    } else {
        printf("media open SUCCESS!\n");
    }

    if (status) {
        leds_set_green(true);
        status = ext_flash_program_from_file(&sd_card, "ext_flash.bin");
        leds_set_green(false);
    }

    if (fx_media_status == FX_SUCCESS) {
        printf("close SD\n");
        fx_media_close(&sd_card);
    }

    clock_init(false);

    leds_set_red(!status);

    while(1) {
	    tx_thread_sleep(100);
    }
}

void tx_application_define(void *first_unused_memory){

    /* Tasks memory allocation and creation */
    tx_byte_pool_create(&StackPool, "Stack_pool", pool_memory_area, STACK_POOL_SIZE);

    tx_byte_allocate(&StackPool, (void **)&p_sdStack, SD_STACK_SIZE, TX_NO_WAIT);
    tx_thread_create(&program_thread, "program_thread", program_thread_entry, 0, p_sdStack, SD_STACK_SIZE,
                     1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);

    /* FileX Initialization */
    fx_system_initialize();

    return;
}

int main (void)
{
    /* Initialize the SE services */
    se_services_port_init();
    clock_init(true);

    int32_t ret = board_pins_config();
    if (ret != 0) {
        printf("Error in pin-mux configuration: %d\n", ret);
        return 0;
    }

    ret = board_gpios_config();
    if (ret != 0) {
        printf("Error in gpio configuration: %d\n", ret);
        return 0;
    }

    leds_init();

#if defined(RTE_CMSIS_Compiler_STDOUT)
    if(stdout_init() != ARM_DRIVER_OK)
    {
        return 0;
    }
#endif

    tx_kernel_enter();
    return 0;
}
