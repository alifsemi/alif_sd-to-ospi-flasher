/* Copyright (C) 2025 Alif Semiconductor - All Rights Reserved.
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

#include "ext_flash.h"

#include "Driver_IO.h"
#include "Driver_Flash.h"

#include "board_defs.h"

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(BOARD_ISSI_FLASH_RESET_GPIO_PORT);
static ARM_DRIVER_GPIO *OSPI_GPIODrv = &ARM_Driver_GPIO_(BOARD_ISSI_FLASH_RESET_GPIO_PORT);
extern ARM_DRIVER_FLASH ARM_Driver_Flash_(1);
static ARM_DRIVER_FLASH* ptrDrvFlash = &ARM_Driver_Flash_(1);

UCHAR file_buffer[512] __attribute__((section("sd_dma_buf"))) __attribute__((aligned(32)));

static void toggle_ospi_flash_reset(void) {
    OSPI_GPIODrv->SetValue(BOARD_ISSI_FLASH_RESET_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    OSPI_GPIODrv->SetValue(BOARD_ISSI_FLASH_RESET_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
}

bool ext_flash_init(void)
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

bool ext_flash_program_from_file(FX_MEDIA *sd_card, char* filename)
{
    FX_FILE image_file;
    uint32_t status = fx_file_open(sd_card, &image_file, filename, FX_OPEN_FOR_READ);
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
    const uint8_t *file_buffer_ptr = file_buffer;
    uint32_t bytes_left_in_buffer = 0;

    uint32_t progress = 0;
    uint32_t progress_printed = 100;

    // Program pages
    printf("Programming flash...\n");
    for (unsigned int ii = 0; ii < pages; ii++) {
        progress = (ii+1) * 100 / pages;
        // Fetch data to write
        ULONG actual_size = 0;
        if (bytes_left_in_buffer == 0) {
            status = fx_file_read(&image_file, file_buffer, sizeof(file_buffer), &actual_size);
            if (status != FX_SUCCESS)
            {
                printf("Failed reading '%s' status=%u\n", filename, status);
                return false;
            }

            file_buffer_ptr = file_buffer;
            bytes_left_in_buffer += actual_size;
        }

        // Check that we write full pages
        if (bytes_left_in_buffer < page_size) {
            // Only last page can be non-full
            if (ii + 1 != pages) {
                printf("Error: not enough data for page write, ii = %u, bytes_left_in_buffer=%u\n", ii, bytes_left_in_buffer);
                return false;
            }
        }

        int32_t ret = ptrDrvFlash->ProgramData(addr, file_buffer_ptr, cnt);
        if (ret != (int32_t)cnt) {
            printf("Flash write failed in program-page at addr[%" PRIu32 "], error = %" PRIi32 "\n", addr, ret);
            return false;
        }

        addr += page_size;
        file_buffer_ptr += page_size;
        bytes_left_in_buffer -= page_size;

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

