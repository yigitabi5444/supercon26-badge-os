#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { int initialized; } esp_elf_t;
typedef struct elf_source {
    size_t (*size)(void *);int (*read)(void *,size_t,void *,size_t);void (*close)(void *);void *ctx;
} elf_source_t;
int esp_elf_init(esp_elf_t*);
int esp_elf_relocate(esp_elf_t*,const uint8_t*,size_t);
int esp_elf_relocate_source(esp_elf_t *,const elf_source_t *);
int esp_elf_request(esp_elf_t*,int,int,char**);
void esp_elf_deinit(esp_elf_t*);
void elf_set_symbol_resolver(uintptr_t (*)(const char*));
void elf_reset_symbol_resolver(void);
