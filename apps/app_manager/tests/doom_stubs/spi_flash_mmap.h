#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_flash.h"
typedef uintptr_t spi_flash_mmap_handle_t;
enum { SPI_FLASH_MMAP_DATA=1 };
esp_err_t spi_flash_mmap(uint32_t,size_t,int,const void **,spi_flash_mmap_handle_t *);
void spi_flash_munmap(spi_flash_mmap_handle_t);
