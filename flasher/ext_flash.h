/* Copyright (C) 2025 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "fx_api.h"

bool ext_flash_init(void);
bool ext_flash_program_from_file(FX_MEDIA *sd_card, char* filename);
