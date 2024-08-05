/* Copyright (C) 2024 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */
/* ThreadX and FileX Includes */
#include "tx_api.h"
#include "fx_api.h"
#include "fx_sd_driver.h"
#include "RTE_Components.h"
#include CMSIS_device_header

#include "Driver_GPIO.h"
#include "Driver_Flash.h"
#include "Driver_OSPI.h"
#include "board.h"
#include "sd_pinconf.h"
#include "se_services_port.h"

#include <inttypes.h>

// Check if UART trace is disabled
#if !defined(DISABLE_UART_TRACE)
#include <stdio.h>
#include "uart_tracelib.h"

static void uart_callback(uint32_t event)
{
}
#else
#define printf(fmt, ...) (0)
#endif

// OSPI flash
#define OSPI_RESET_PORT     LP
#define OSPI_RESET_PIN      7
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(OSPI_RESET_PORT);
static ARM_DRIVER_GPIO *OSPI_GPIODrv = &ARM_Driver_GPIO_(OSPI_RESET_PORT);
extern ARM_DRIVER_FLASH ARM_Driver_Flash_(1);
static ARM_DRIVER_FLASH* ptrDrvFlash = &ARM_Driver_Flash_(1);

// Azure RTOS and SD card
#define K (1024)
#define STACK_POOL_SIZE (64*K)
#define SD_STACK_SIZE (16*K)
#define SD_BUF_SIZE (8*K)
TX_THREAD program_thread;
TX_BYTE_POOL StackPool;
unsigned char *p_sdStack = NULL;
/* Buffer for FileX FX_MEDIA sector cache. This must be large enough for at least one sector , which are typically 512 bytes in size. */
UCHAR media_memory[SD_BUF_SIZE] __attribute__((section("sd_dma_buf"))) __attribute__((aligned(32)));
UCHAR file_buffer[512] __attribute__((section("sd_dma_buf"))) __attribute__((aligned(32)));
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

    error_code = SERVICES_clocks_enable_clock(se_services_s_handle, CLKEN_USB, enable, &service_error_code);
    if(error_code || service_error_code){
        printf("SE: SDMMC 20MHz clock enable error_code=%u se_error_code=%u\n", error_code);
        return;
    }
}

static void toggle_ospi_flash_reset(void)
{
    OSPI_GPIODrv->SetValue(OSPI_RESET_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    OSPI_GPIODrv->SetValue(OSPI_RESET_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
}

bool init_ext_flash(void)
{
    toggle_ospi_flash_reset();

    int32_t ret = ptrDrvFlash->Initialize(NULL);    // Initialize ext-FLASH
    if (ret != ARM_DRIVER_OK) {
        printf("OSPI Flash: Init failed, error = %" PRIi32 "\n", ret);
        return false;
    }

    ret = ptrDrvFlash->PowerControl(ARM_POWER_FULL);
    if (ret != ARM_DRIVER_OK) {
        printf("OSPI Flash: Power up failed, error = %" PRIi32 "\n", ret);
        return false;
    }

    if(ptrDrvFlash->GetInfo()->page_size > sizeof(file_buffer))
    {
        printf("OSPI Flash: ERROR: buffer size configured smaller than flash page size.\n");
        return false;
    }

    return true;
}

bool program_from_file(char* filename)
{
    FX_FILE image_file;
    uint32_t status = fx_file_open(&sd_card, &image_file, filename, FX_OPEN_FOR_READ);
    if (status != FX_SUCCESS)
    {
        printf("Failed opening '%s' status=%u\n", filename, status);
        return false;
    }

    status = fx_file_seek(&image_file, 0);
    if (status != FX_SUCCESS)
    {
        printf("Failed seeking '%s' status=%u\n", filename, status);
        return false;
    }

    const uint32_t file_size = image_file.fx_file_current_file_size;
    const uint32_t page_size = ptrDrvFlash->GetInfo()->page_size;
    const uint32_t sector_size = ptrDrvFlash->GetInfo()->sector_size;
    const uint32_t data_width = ptrDrvFlash->GetCapabilities().data_width;

    const uint32_t pages = (file_size + page_size - 1) / page_size;
    const uint32_t sectors = (file_size + sector_size - 1) / sector_size;
;
    const uint32_t cnt = page_size / (1 << data_width); // data width 0 = 8 bytes, 1 = 16 bytes, 2 = 32 bytes
    printf("file_size=%u page_size=%u pages=%u data_width=%u\n", file_size, page_size, pages, data_width);
    printf("sector_size=%u sectors=%u\n", sector_size, sectors);

    // Erase sectors
    // TODO: Note that in some use-case the last sector (or page) could contain existing data that should be first read to a temporary buffer
    //       and then written back while writing. This is a simple demo solution and erased/programmed sectors are just rounded up.
    printf("Erasing flash...\n");    
    for (unsigned int ii = 0; ii < sectors; ii++) {
        uint32_t sector_addr = ii * sector_size;
        int32_t ret = ptrDrvFlash->EraseSector(sector_addr);
        if (ret != ARM_DRIVER_OK) {
            printf("Failed erasing sector at address=%d: status=%d\n", sector_addr, ret);
            return false;
        }
    }

    uint32_t addr = 0;
    const uint8_t *file_buffer_ptr = &file_buffer;
    uint32_t pages_left_in_buffer = 0;

    uint32_t progress = 0;
    uint32_t progress_printed = 100;

    // Program pages
    printf("Programming flash...\n");
    for (unsigned int ii = 0; ii < pages; ii++) {
        progress = (ii+1) * 100 / pages;
        // Fetch data to write
        ULONG actual_size = 0;
        if (pages_left_in_buffer == 0) {
            status = fx_file_read(&image_file, file_buffer, sizeof(file_buffer), &actual_size);
            if (status != FX_SUCCESS)
            {
                printf("Failed reading '%s' status=%u\n", filename, status);
                return false;
            }

            file_buffer_ptr = &file_buffer;
            pages_left_in_buffer = actual_size / page_size;

            // Check that we write full pages
            if (pages_left_in_buffer * page_size != actual_size) {
                // Only last page can be non-full
                if (ii + 1 != pages) {
                    printf("Error: not enough data for page write");
                    return false;
                }
            }
        }

        int32_t ret = ptrDrvFlash->ProgramData(addr, file_buffer_ptr, cnt);
        if (ret != (int32_t)cnt) {
            printf("Flash write failed in program-page at addr[%" PRIu32 "], error = %" PRIi32 "\n", addr, ret);
            return false;
        }

        addr += page_size;
        file_buffer_ptr += page_size;
        pages_left_in_buffer--;

        if (progress != progress_printed) {
            printf("%u%%\n", progress);
            progress_printed = progress;
        }
    }

    status = fx_file_close(&image_file);
    if (status != FX_SUCCESS)
    {
        printf("Failed closing '%s' status=%u\n", filename, status);
        return false;
    }

    printf("Successfully wrote '%s'\n", filename);
    return true;
}

void program_thread_entry(ULONG args)
{
    bool status = init_ext_flash();

    /* Open the SD disk. and initialize SD controller */
    uint32_t fx_media_status = fx_media_open(&sd_card, "SD_DISK", _fx_sd_driver, 0, (VOID *)media_memory, sizeof(media_memory));
    if (fx_media_status != FX_SUCCESS)
    {
        printf("media open fail status = %d...\n", fx_media_status);
        status = false;
    } else {
        printf("media open SUCCESS!\n");
    }

    if (status) {
        BOARD_LED2_Control(BOARD_LED_STATE_HIGH);
        status = program_from_file("ext_flash.bin");
        BOARD_LED2_Control(BOARD_LED_STATE_LOW);
    }

    if (fx_media_status == FX_SUCCESS) {
        printf("close SD\n");
        fx_media_close(&sd_card);
    }

    clock_init(false);

    BOARD_LED1_Control(status ? BOARD_LED_STATE_LOW : BOARD_LED_STATE_HIGH);
    while(1) {
	    __WFE();
    }
}

void tx_application_define(void *first_unused_memory){

    /* Tasks memory allocation and creation */
    tx_byte_pool_create(&StackPool, "Stack_pool", first_unused_memory, STACK_POOL_SIZE);

    tx_byte_allocate(&StackPool, (void **)&p_sdStack, SD_STACK_SIZE, TX_NO_WAIT);
    tx_thread_create(&program_thread, "program_thread", program_thread_entry, NULL, p_sdStack, SD_STACK_SIZE,
                     1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);

    /* FileX Initialization */
    fx_system_initialize();

    return;
}

int main (void)
{
    BOARD_Pinmux_Init();

    set_SD_card_pinconf();

    /* Initialize the SE services */
    se_services_port_init();

    clock_init(true);

#if !defined(DISABLE_UART_TRACE)
    tracelib_init(NULL, uart_callback);
#endif

    tx_kernel_enter();
    return 0;
}
