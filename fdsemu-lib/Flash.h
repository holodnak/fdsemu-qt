#pragma once
#include "Device.h"

enum {
//	PAGESIZE = 256,
	CMD_READID = 0x9F,
	CMD_READSTATUS = 0x05,
	CMD_WRITEENABLE = 0x06,

	CMD_READDATA = 0x03,
	CMD_WRITESTATUS = 0x01,
	CMD_PAGEWRITE = 0x02,
	CMD_PAGEERASE = 0xdb,
	CMD_PAGEPROGRAM = 0x02,
	CMD_BLOCKERASE = 0xd8,
	CMD_BLOCKERASE64 = CMD_BLOCKERASE,
	CMD_BLOCKERASE32 = 0x52,
	CMD_SECTORERASE = 0x20,

	SECTORSIZE = 4096,
	PAGESIZE = 256,
};

typedef void(*TCallback)(void*, uint32_t);

class CFlash
{
protected:
	CDevice *dev;
public:
	CFlash(CDevice *d);
	virtual ~CFlash();

	//enable writes
	virtual bool WriteEnable();

	//wait for chip to stop being busy
	virtual bool WaitBusy(uint32_t timeout);

	//read and write to flash
	virtual bool Read(uint8_t *buf, uint32_t addr, int size);
	virtual bool Write(uint8_t *buf, uint32_t addr, int size, TCallback cb = 0, void *user = 0);
	virtual bool Erase(uint32_t addr, int size);

	//write one 256 byte page
	virtual bool PageProgram(uint32_t addr, uint8_t *buf);

	//erase 4kb sector
	virtual bool EraseSector(uint32_t addr);

	//erase one disk slot
	virtual bool EraseSlot(int slot);

	//erase entire chip
	virtual bool ChipErase();
};
