#include <stdio.h>
#include <stdlib.h>
#include "Device.h"

#define VID 0x0416
#define PID 0xBEEF
#define DEV_NAME L"FDSdick"

CDevice::CDevice()
{
}


CDevice::~CDevice()
{
	Close();
}

bool CDevice::Open()
{
	struct hid_device_info *devs, *dev;

	//ensure device isnt open
	Close();

	//get list of available usb devices
	devs = hid_enumerate(VID, PID);

	//search for device in all the usb devices found
	for (dev = devs; dev != NULL; dev = dev->next) {
		//		 if (cur_dev->vendor_id == VID && cur_dev->product_id == PID && cur_dev->product_string && wcscmp(DEV_NAME, cur_dev->product_string) == 0)
		if (dev->vendor_id == VID && dev->product_id == PID)
			break;
	}

	//device found, try to open it
	if (dev) {
		handle = hid_open_path(dev->path);
	}

	//device opened successfully, try to communicate with the flash chip
	if (handle) {

		//save device informations
		wcstombs(DeviceName, dev->product_string, 256);
		VendorID = dev->vendor_id;
		ProductID = dev->product_id;
		Version = dev->release_number;

		//read in flash id to determine type of flash
		FlashID = ReadFlashID();
		if (FlashID == 0) {
			printf("Error reading flash ID.\n");
			Close();
		}
		
		//get the size of the flash chip
		FlashSize = GetFlashSize();
		if (FlashSize == 0) {
			printf("Error determining flash size.\n");
			Close();
		}

		Slots = FlashSize / 65536;
		Sram = new CSram(this);
		Flash = new CFlash(this);
		FlashUtil = new CFlashUtil(this);
	}
	else {
		printf("Device not found.\n");
	}
	hid_free_enumeration(devs);
	return !!this->handle;
}

void CDevice::Close()
{
	if (this->Flash) {
		delete this->Flash;
	}
	if (this->FlashUtil) {
		delete this->FlashUtil;
	}
	if (this->handle) {
		hid_close(this->handle);
	}
	this->Flash = 0;
	this->FlashUtil = 0;
	this->handle = NULL;
}

uint32_t CDevice::ReadFlashID()
{
	static uint8_t readID[] = { CMD_READID };
	uint32_t id = 0;

	if (!this->FlashWrite(readID, 1, 1, 1)) {
		printf("CDevice::ReadFlashID: FlashWrite failed\n");
		return 0;
	}
	if (!this->FlashRead((uint8_t*)&id, 3, 0)) {
		printf("CDevice::ReadFlashID: FlashRead failed\n");
		return 0;
	}
	return(id);
}

uint32_t CDevice::GetFlashSize()
{
	switch (this->FlashID) {

		//16mbit flash
		case 0x1440EF: // W25Q80DV
			return(0x100000);

		//32mbit flash
		case 0x1640EF: // W25Q32FV
			return(0x400000);

        //64mbit flash
        case 0x1740EF: // W25Q64FV
        case 0x174001: // S25FL164K
            return(0x800000);

        //128mbit flash
        case 0x1840EF: // W25Q128FV
            return(0x1000000);
    }

	//unknown flash chip
	printf("Unknown flash chip detected.  Flash ID: $%06X\n", this->FlashID);
	return(0);
}

//will reset the device
void CDevice::Reset()
{
	hidbuf[0] = ID_RESET;
	hid_send_feature_report(handle, hidbuf, 2);    //reset will cause an error, ignore it
}

//causes device to perform its self-test
void CDevice::Test()
{
	hidbuf[0] = ID_SELFTEST;
	hid_send_feature_report(handle, hidbuf, 2);
}

//command to update firmware loaded into special region of flash
void CDevice::UpdateFirmware()
{
	hidbuf[0] = ID_FIRMWARE_UPDATE;
	hid_send_feature_report(handle, hidbuf, 2);    //reset after update will cause an error, ignore it
}

bool CDevice::GenericRead(int reportid, uint8_t *buf, int size, bool holdCS)
{
	int ret;

	if(size > SPI_READMAX) {
		printf("Read too big.\n");
		return(false);
	}
	hidbuf[0] = holdCS ? reportid : (reportid + 1);
	ret = hid_get_feature_report(handle, hidbuf, 64);
	if (ret < 0)
		return(false);
	memcpy(buf, hidbuf + 1, size);
	return(true);
}

bool CDevice::GenericWrite(int reportid, uint8_t *buf, int size, bool initCS, bool holdCS)
{
	int ret;

	if (size > SPI_WRITEMAX) {
		printf("Write too big.\n");
		return(false);
	}
	hidbuf[0] = reportid;
	hidbuf[1] = size;
	hidbuf[2] = initCS,
	hidbuf[3] = holdCS;
	if (size)
		memcpy(hidbuf + 4, buf, size);
	ret = hid_send_feature_report(handle, hidbuf, 4 + size);
	if (ret == -1) {
		wprintf(L"error: %s\n", hid_error(handle));
	}
	return(ret >= 0);
}

bool CDevice::FlashRead(uint8_t *buf, int size, bool holdCS)
{
	return(GenericRead(ID_SPI_READ, buf, size, holdCS));
}

bool CDevice::FlashWrite(uint8_t *buf, int size, bool initCS, bool holdCS)
{
	return(GenericWrite(ID_SPI_WRITE, buf, size, initCS, holdCS));
}

bool CDevice::SramRead(uint8_t *buf, int size, bool holdCS)
{
	return(GenericRead(ID_SPI_SRAM_READ, buf, size, holdCS));
}

bool CDevice::SramWrite(uint8_t *buf, int size, bool initCS, bool holdCS)
{
	return(GenericWrite(ID_SPI_SRAM_WRITE, buf, size, initCS, holdCS));
}

bool CDevice::DiskWriteStart()
{
	hidbuf[0] = ID_DISK_WRITE_START;
	return hid_send_feature_report(handle, hidbuf, 2) >= 0;
}

bool CDevice::DiskWrite(uint8_t *buf, int size)
{
	if (size != DISK_WRITEMAX)        //always max!
		return false;
	hidbuf[0] = ID_DISK_WRITE;
	memcpy(hidbuf + 1, buf, size);
	return hid_write(handle, hidbuf, DISK_WRITEMAX + 1) >= 0;     // WRITEMAX+reportID
}

bool CDevice::DiskReadStart()
{
	hidbuf[0] = ID_DISK_READ_START;
	sequence = 1;
	return hid_send_feature_report(handle, hidbuf, 2) >= 0;
}

int CDevice::DiskRead(uint8_t *buf)
{
	int result;

	hidbuf[0] = ID_DISK_READ;
	result = hid_get_feature_report(handle, hidbuf, DISK_READMAX + 2);  // + reportID + sequence

	//hidapi increments the result by 1 to account for the report id, if it was a success
	if (result > 0) {
		result--;
	}

	//read time out
	if (result < 2) {
		printf("\nDisk read timed out\n");
		return(-1);
	}

	//adapter will send incomplete/empty packets when it's out of data (end of disk)
	else if (result > 2) {
		memcpy(buf, hidbuf + 2, result - 2);

		//sequence out of order (data lost)
		if (hidbuf[1] != sequence++) {
			printf("\nDisk read sequence out of order (got %d, wanted %d)\n",hidbuf[1],sequence-1);
			return(-1);
		}
		else {
//			printf("read %d bytes\n", result - 2);
			return(result - 2);
		}
	}

	else {
		printf("\nDisk read returned no data\n");
		return(0);
	}
}

