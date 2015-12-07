#pragma once

enum { 
    SLOTSIZE=0x10000,
};

uint32_t spi_readID();
uint32_t spi_readFlashSize();
bool spi_dumpFlash(char *filename, int addr, int size);
bool spi_writeFile(char *filename, uint32_t addr);
bool spi_writeFlash(const uint8_t *buf, uint32_t addr, uint32_t size, int pos, void *user, void(*callback)(void*,int));
bool spi_erasePage(int addr);
bool spi_readFlash(int addr, uint8_t *buf, int size);
