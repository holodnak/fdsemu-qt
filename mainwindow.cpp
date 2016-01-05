#include <QDragEnterEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QFileDialog>
#include <stdint.h>
#include <stdio.h>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "writestatus.h"
#include "writefilesdialog.h"
#include "diskreaddialog.h"
#include "fdsemu-lib/Device.h"
#include "fdsemu-lib/System.h"

#define VERSION_HI 0
#define VERSION_LO 42
#define VERSION (VERSION_LO + (VERSION_HI * 100))

CDevice dev;

int force = 0;

__inline uint8_t raw_to_raw03_byte(uint8_t raw)
{
    if (raw < 0x48)
        return(3);
    else if (raw < 0x70)
        return(0);
    else if (raw < 0xA0)
        return(1);
    else if (raw < 0xD0)
        return(2);
    return(3);
}

//Turn raw data from adapter to pulse widths (0..3)
//Input capture clock is 6MHz.  At 96.4kHz (FDS bitrate), 1 bit ~= 62 clocks
//We could be clever about this and account for drive speed fluctuations, etc. but this will do for now
static void raw_to_raw03(uint8_t *raw, int rawSize) {
    for (int i = 0; i<rawSize; ++i) {
        raw[i] = raw_to_raw03_byte(raw[i]);
    }
}

//don't include gap end
uint16_t calc_crc(uint8_t *buf, int size) {
    uint32_t crc = 0x8000;
    int i;
    while (size--) {
        crc |= (*buf++) << 16;
        for (i = 0; i<8; i++) {
            if (crc & 1) crc ^= 0x10810;
            crc >>= 1;
        }
    }
    return crc;
}

void copy_block(uint8_t *dst, uint8_t *src, int size) {
    dst[0] = 0x80;
    memcpy(dst + 1, src, size);
    uint32_t crc = calc_crc(dst + 1, size + 2);
    dst[size + 1] = crc;
    dst[size + 2] = crc >> 8;
}

//Adds GAP + GAP end (0x80) + CRCs to .FDS image
//Returns size (0=error)
int fds_to_bin(uint8_t *dst, uint8_t *src, int dstSize) {
    int i = 0, o = 0;

    //check *NINTENDO-HVC* header
    if (src[0] != 0x01 || src[1] != 0x2a || src[2] != 0x4e) {
        printf("Not an FDS file.\n");
        return 0;
    }
    memset(dst, 0, dstSize);

    //block type 1
    copy_block(dst + o, src + i, 0x38);
    i += 0x38;
    o += 0x38 + 3 + GAP;

    //block type 2
    copy_block(dst + o, src + i, 2);
    i += 2;
    o += 2 + 3 + GAP;

    //block type 3+4...
    while (src[i] == 3) {
        int size = (src[i + 13] | (src[i + 14] << 8)) + 1;
        if (o + 16 + 3 + GAP + size + 3 > dstSize) {    //end + block3 + crc + gap + end + block4 + crc
            printf("Out of space (%d bytes short), adjust GAP size?\n", (o + 16 + 3 + GAP + size + 3) - dstSize);
            return 0;
        }
        copy_block(dst + o, src + i, 16);
        i += 16;
        o += 16 + 3 + GAP;

        copy_block(dst + o, src + i, size);
        i += size;
        o += size + 3 + GAP;
    }
    return o;
}

//allocate buffer and read whole file
bool loadfile(char *filename, uint8_t **buf, int *filesize)
{
    FILE *fp;
    int size;

    //check if the pointers are ok
    if (buf == 0 || filesize == 0) {
        return(false);
    }

    //open file
    if ((fp = fopen(filename, "rb")) == 0) {
        char buf[512];

        sprintf(buf,"Cannot open file '%s'", filename);
        QMessageBox::information(NULL,"Error",buf);
        return(false);
    }

    //get file size
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    //allocate buffer
    *buf = new uint8_t[size];

    //read in file
    *filesize = fread(*buf, 1, size, fp);

    //close file and return
    fclose(fp);
    return(true);
}
/*
bool firmware_update(char *filename)
{
    uint8_t *firmware;
    uint8_t *buf;
    uint32_t *buf32;
    uint32_t chksum;
    int filesize, i;

    //try to load the firmware image
    if (loadfile(filename, &firmware, &filesize) == false) {
        printf("Error loading firmware file %s'\n", filename);
        return(false);
    }

    //create new buffer to hold 32kb of data and clear it
    buf = new uint8_t[0x8000];
    memset(buf, 0, 0x8000);

    //copy firmware loaded to the new buffer
    memcpy(buf, firmware, filesize);
    buf32 = (uint32_t*)buf;

    //free firmware data
    delete[] firmware;

    //insert firmware identifier
    buf32[(0x8000 - 8) / 4] = 0xDEADBEEF;

    //calculate the simple xor checksum
    chksum = 0;
    for (i = 0; i < (0x8000 - 4); i += 4) {
        chksum ^= buf32[i / 4];
    }

    printf("firmware is %d bytes, checksum is $%08X\n", filesize, chksum);

    //insert checksum into the image
    buf32[(0x8000 - 4) / 4] = chksum;

    printf("uploading new firmware");
    if (!dev.Flash->Write(buf, 0x8000, 0x8000)) {
        printf("Write failed.\n");
        return false;
    }
    delete[] buf;

    printf("waiting for device to reboot\n");

    dev.UpdateFirmware();
    sleep_ms(5000);

    if (!dev.Open()) {
        printf("Open failed.\n");
        return false;
    }
    printf("Updated to build %d\n", dev.Version);

    return(true);
}*/

uint32_t chksum_calc(uint8_t *buf, int size)
{
    uint32_t ret = 0;
    int i;

    for (i = 0; i < size / 4; i++) {
        ret ^= buf[i];
    }
    return(ret);
}

//look for pattern of bits matching block 1
static int findFirstBlock(uint8_t *raw) {
    static const uint8_t dat[] = { 1,0,1,0,0,0,0,0, 0,1,2,2,1,0,1,0, 0,1,1,2,1,1,1,1, 1,1,0,0,1,1,1,0 };
    int i, len;
    for (i = 0, len = 0; i<0x2000 * 8; i++) {
        if (raw[i] == dat[len]) {
            if (len == sizeof(dat) - 1)
                return i - len;
            len++;
        }
        else {
            i -= len;
            len = 0;
        }
    }
    return -1;
}

bool block_decode(uint8_t *dst, uint8_t *src, int *inP, int *outP, int srcSize, int dstSize, int blockSize, char blockType) {
    if (*outP + blockSize + 2 > dstSize) {
        printf("Out of space\n");
        return false;
    }

    int in = *inP;
    int outEnd = (*outP + blockSize + 2) * 8;
    int out = (*outP) * 8;
    int start;

    //scan for gap end
    for (int zeros = 0; src[in] != 1 || zeros<MIN_GAP_SIZE; in++) {
        if (src[in] == 0) {
            zeros++;
        }
        else {
            zeros = 0;
        }
        if (in >= srcSize - 2)
            return false;
    }
    start = in;

    char bitval = 1;
    in++;
    do {
        if (in >= srcSize) {   //not necessarily an error, probably garbage at end of disk
                                      //printf("Disk end\n");
            return false;
        }
        switch (src[in] | (bitval << 4)) {
        case 0x11:
            out++;
        case 0x00:
            out++;
            bitval = 0;
            break;
        case 0x12:
            out++;
        case 0x01:
        case 0x10:
            dst[out / 8] |= 1 << (out & 7);
            out++;
            bitval = 1;
            break;
        default: //Unexpected value.  Keep going, we'll probably get a CRC warning
                    //printf("glitch(%d) @ %X(%X.%d)\n", src[in], in, out/8, out%8);
            out++;
            bitval = 0;
            break;
        }
        in++;
    } while (out<outEnd);
    if (dst[*outP] != blockType) {
        printf("Wrong block type %X(%X)-%X(%X) (found %d, expected %d)\n", start, *outP, in, out - 1, dst[*outP], blockType);
        return false;
    }
    out = out / 8 - 2;

    //printf("Out%d %X(%X)-%X(%X)\n", blockType, start, *outP, in, out-1);

    if (calc_crc(dst + *outP, blockSize + 2)) {
        uint16_t crc1 = (dst[out + 1] << 8) | dst[out];
        dst[out] = 0;
        dst[out + 1] = 0;
        uint16_t crc2 = calc_crc(dst + *outP, blockSize + 2);
        printf("Bad CRC (%04X!=%04X)\n", crc1, crc2);
    }

    dst[out] = 0;     //clear CRC
    dst[out + 1] = 0;
    dst[out + 2] = 0;   //+spare bit
    *inP = in;
    *outP = out;
    return true;
}

//Simplified disk decoding.  This assumes disk will follow standard FDS file structure
static bool raw03_to_fds(uint8_t *raw, uint8_t *fds, int rawsize) {
    int in, out;

    memset(fds, 0, FDSSIZE);

    //lead-in can vary a lot depending on drive, scan for first block to get our bearings
    in = findFirstBlock(raw) - MIN_GAP_SIZE;
    if (in<0)
        return false;

    out = 0;
    if (!block_decode(fds, raw, &in, &out, rawsize, FDSSIZE + 2, 0x38, 1))
        return false;
    if (!block_decode(fds, raw, &in, &out, rawsize, FDSSIZE + 2, 2, 2))
        return false;
    do {
        if (!block_decode(fds, raw, &in, &out, rawsize, FDSSIZE + 2, 16, 3))
            return true;
        if (!block_decode(fds, raw, &in, &out, rawsize, FDSSIZE + 2, 1 + (fds[out - 16 + 13] | (fds[out - 16 + 14] << 8)), 4))
            return true;
    } while (in<rawsize);
    return true;
}

//make raw0-3 from flash image (sans header)
static void bin_to_raw03(uint8_t *bin, uint8_t *raw, int binSize, int rawSize) {
    int in, out;
    uint8_t bit, data = 0;

    memset(raw, 0xff, rawSize);
    for (bit = 1, out = 0, in = 0; in<binSize * 8; in++) {
        if ((in & 7) == 0) {
            data = *bin;
            bin++;
        }
        bit = (bit << 7) | (1 & (data >> (in & 7)));   //LSB first
                                                                      //     bit = (bit<<7) | (1 & (bin[in/8]>>(in%8)));   //LSB first
        switch (bit) {
        case 0x00:  //10 10
            out++;
            raw[out]++;
            break;
        case 0x01:  //10 01
        case 0x81:  //01 01
            raw[out]++;
            out++;
            break;
        case 0x80:  //01 10
            raw[out] += 2;
            break;
        }
    }
    memset(raw + out, 3, rawSize - out);  //fill remainder with (undefined)
}

//check for gap at EOF
bool looks_like_file_end(uint8_t *raw, int start, int rawSize) {
    enum {
        MIN_GAP = 976 - 100,
        MAX_GAP = 976 + 100,
    };
    int zeros = 0;
    int in = start;
    for (; in<start + MAX_GAP && in<rawSize; in++) {
        if (raw[in] == 1 && zeros>MIN_GAP) {
            return true;
        }
        else if (raw[in] == 0) {
            zeros++;
        }
        if (raw[in] != 0)
            zeros = 0;
    }
    return in >= rawSize;  //end of disk = end of file!
}

//detect EOF by looking for good CRC.  in=start of file
//returns 0 if nothing found
int crc_detect(uint8_t *raw, int in, int rawSize) {
    static uint32_t crc;
    static uint8_t bitval;
    static int out;
    static bool match;

    //local function ;)
    struct {
        void shift(uint8_t bit) {
            crc |= bit << 16;
            if (crc & 1) crc ^= 0x10810;
            crc >>= 1;
            bitval = bit;
            out++;
            if (crc == 0 && !(out & 7))  //on a byte bounary and CRC is valid
                match = true;
        }
    } f;

    crc = 0x8000;
    bitval = 1;
    out = 0;
    do {
        match = false;
        switch (raw[in] | (bitval << 4)) {
        case 0x11:
            f.shift(0);
        case 0x00:
            f.shift(0);
            break;
        case 0x12:
            f.shift(0);
        case 0x01:
        case 0x10:
            f.shift(1);
            break;
        default:    //garbage / bad encoding
            return 0;
        }
        in++;
    } while (in<rawSize && !(match && looks_like_file_end(raw, in, rawSize)));
    return match ? in : 0;
}

//gap end is known, backtrack and mark the start.  !! this assumes junk data exists between EOF and gap start
static void mark_gap_start(QStringList *messages, uint8_t *raw, int gapEnd) {
    int i;
    QString str;
    for (i = gapEnd - 1; i >= 0 && raw[i] == 0; --i)
    {
    }
    raw[i + 1] = 3;

    str.sprintf("mark gap %X-%X\n", i + 1, gapEnd);
    messages->append(str);
}

//For information only for now.  This checks for standard file format
static void verify_block(QStringList *messages, uint8_t *bin, int start, int *reverse) {
    enum { MAX_GAP = (976 + 100) / 8, MIN_GAP = (976 - 100) / 8 };
    static const uint8_t next[] = { 0,2,3,4,3 };
    static int last = 0;
    static int lastLen = 0;
    static int blockCount = 0;
    QString str, str2;

    int len = 0;
    uint8_t type = bin[start];

    str.sprintf("%d:%X", ++blockCount, type);

    switch (type) {
    case 1:
        len = 0x38;
        break;
    case 2:
        len = 2;
        break;
    case 3:
        len = 16;
        break;
    case 4:
        len = 1 + (bin[last + 13] | (bin[last + 14] << 8));
        break;
    default:
        str2.sprintf(" bad block (%X)\n", start);
        messages->append(str + str2);
        return;
    }
    str2.sprintf(" %X-%X / %X-%X(%X)", reverse[start], reverse[start + len], start, start + len, len);

    str += str2;
    if ((!last && type != 1) || (last && type != next[bin[last]]))
        str += QString(", wrong filetype");
    if (calc_crc(bin + start, len + 2) != 0)
        str += QString(", bad CRC");
    if (last && (last + lastLen + MAX_GAP)<start)
        str += QString(", lost block?");
    if (last + lastLen + MIN_GAP>start)
        str += QString(", block overlap?");
    //if(type==3 && ...)    //check other fields in file header?

    str += "\n";
    messages->append(str);
    last = start;
    lastLen = len;
}

//find gap + gap end.  returns bit following gap end, >=rawSize if not found.
int nextGapEnd(uint8_t *raw, int in, int rawSize) {
    enum { MIN_GAP = 976 - 100, };
    int zeros = 0;
    for (; (raw[in] != 1 || zeros<MIN_GAP) && in<rawSize; in++) {
        if (raw[in] == 0) {
            zeros++;
        }
        else {
            zeros = 0;
        }
    }
    return in + 1;
}


/*
Try to create byte-for-byte, unadulterated representation of disk.  Use hints from the disk structure, given
that it's probably a standard FDS game image but this should still make a best attempt regardless of the disk content.

_bin and _binSize are updated on exit.  alloc'd buffer is returned in _bin, caller is responsible for freeing it.
*/
static void raw03_to_bin(QStringList *messages, uint8_t *raw, int rawSize, uint8_t **_bin, int *_binSize) {
    enum {
        BINSIZE = 0xa0000,
        POST_GLITCH_GARBAGE = 16,
        LONG_POST_GLITCH_GARBAGE = 64,
        LONG_GAP = 900,   //976 typ.
        SHORT_GAP = 16,
    };
    int in, out;
    uint8_t *bin;
    int *reverse;
    int glitch;
    int zeros;
    QString str;

    bin = (uint8_t*)malloc(BINSIZE);
    reverse = (int*)malloc(BINSIZE*sizeof(int));
    memset(bin, 0, BINSIZE);

    //--- assume any glitch is OOB, mark a run of zeros near a glitch as a gap start.

    int junk = 0;
    glitch = 0;
    zeros = 0;
    junk = 0;
    for (in = 0; in<rawSize; in++) {
        if (raw[in] == 3) {
            glitch = in;
            junk = 0;
        }
        else if (raw[in] == 1 || raw[in] == 2) {
            junk = in;
        }
        else if (raw[in] == 0) {
            zeros++;
            if (glitch && junk && zeros>SHORT_GAP && (junk - glitch)<POST_GLITCH_GARBAGE) {
                mark_gap_start(messages, raw, in);
                glitch = 0;
            }
        }
        if (raw[in] != 0)
            zeros = 0;
    }

    //--- Walk filesystem, mark blocks where something looks like a valid file

    in = findFirstBlock(raw);
    if (in>0) {
        str.sprintf("header at %X\n", in);
        messages->append(str);
        mark_gap_start(messages, raw, in - 1);
    }
    /*
    do {
    if(block_decode(..)) {
    raw[head]=0xff;
    raw[tail]=3;
    }
    next_gap(..);
    } while(..);
    */
    //--- Identify files by CRC. If data looks like it's surrounded by gaps and it has a valid CRC where we
    //    expect one to be, assume it's a file and mark its start/end.

    in = findFirstBlock(raw) + 1;
    if (in>0) do {
        out = crc_detect(raw, in, rawSize);
        if (out) {
            str.sprintf("crc found %X-%X\n", in, out);
            messages->append(str);
            raw[out] = 3;     //mark glitch (gap start)
                                    //raw[in-1]=0xff;   //mark gap end
        }
        in = nextGapEnd(raw, out ? out : in, rawSize);
    } while (in<rawSize);

    //--- mark gap start/end using glitches to find gap start

    for (glitch = 0, zeros = 0, in = 0; in<rawSize; in++) {
        if (raw[in] == 3) {
            glitch = in;
        }
        else if (raw[in] == 1) {
            if (zeros>LONG_GAP && (in - zeros - LONG_POST_GLITCH_GARBAGE)<glitch) {
                mark_gap_start(messages, raw, in);
                raw[in] = 0xff;
            }
        }
        else if (raw[in] == 0) {
            zeros++;
        }
        if (raw[in] != 0)
            zeros = 0;
    }

    //--- output

    /*
    FILE *f=fopen("raw03.bin","wb");
    fwrite(raw,1,rawSize,f);
    fclose(f);
    */

    char bitval = 0;
    int lastBlockStart = 0;
    for (in = 0, out = 0; in<rawSize; in++) {
        switch (raw[in] | (bitval << 4)) {
        case 0x11:
            out++;
        case 0x00:
            out++;
            bitval = 0;
            break;
        case 0x12:
            out++;
        case 0x01:
        case 0x10:
            bin[out / 8] |= 1 << (out & 7);
            out++;
            bitval = 1;
            break;
        case 0xff:  //block end
            if (lastBlockStart)
                verify_block(messages, bin, lastBlockStart, reverse);
            bin[out / 8] = 0x80;
            out = (out | 7) + 1;      //byte-align for readability
            lastBlockStart = out / 8;
            bitval = 1;
            break;
        case 0x02:
            //printf("Encoding error @ %X(%X)\n",in,out/8);
        default: //anything else (glitch)
            out++;
            bitval = 0;
            break;
        }
        reverse[out / 8] = in;
    }
    //last block
    verify_block(messages, bin, lastBlockStart, reverse);

    *_bin = bin;
    *_binSize = out / 8 + 1;
    free(reverse);
}

// TODO - only handles one side, files will need to be joined manually
bool FDS_readDisk(char *filename_raw, char *filename_bin, char *filename_fds, void(*callback)(void*,int), void *data) {
    enum { READBUFSIZE = 0x90000 };

    FILE *f;
    uint8_t *readBuf = NULL;
    int result;
    int bytesIn = 0;

    //if(!(dev_readIO()&MEDIA_SET)) {
    //    printf("Warning - Disk not inserted?\n");
    //}
    if (!dev.DiskReadStart()) {
        printf("diskreadstart failed\n");
        return false;
    }

    readBuf = (uint8_t*)malloc(READBUFSIZE);
    do {
        result = dev.DiskRead(readBuf + bytesIn);
        bytesIn += result;
        if (!(bytesIn % ((DISK_READMAX)* 32))) {
            if(callback) {
                callback(data,bytesIn);
            }
//            printf(".");
        }
    } while (result == DISK_READMAX && bytesIn<READBUFSIZE - DISK_READMAX);
    printf("\n");
    if (result<0) {
        printf("Read error.\n");
        return false;
    }

    if (filename_raw) {
        if ((f = fopen(filename_raw, "wb"))) {
            fwrite(readBuf, 1, bytesIn, f);
            fclose(f);
            printf("Wrote %s\n", filename_raw);
        }
    }

    raw_to_raw03(readBuf, bytesIn);

    //decode to .fds
    if (filename_fds) {
        uint8_t *fds = (uint8_t*)malloc(FDSSIZE + 16);   //extra room for CRC junk
        raw03_to_fds(readBuf, fds, bytesIn);
        if ((f = fopen(filename_fds, "wb"))) {
            fwrite(fds, 1, FDSSIZE, f);
            fclose(f);
            printf("Wrote %s\n", filename_fds);
        }
        free(fds);

        //decode to .bin
    }
    else if (filename_bin) {
        uint8_t *binBuf;
        int binSize;
        QStringList messages;

        raw03_to_bin(&messages, readBuf, bytesIn, &binBuf, &binSize);
        if ((f = fopen(filename_bin, "wb"))) {
            fwrite(binBuf, 1, binSize, f);
            fclose(f);
            printf("Wrote %s\n", filename_bin);
        }
        free(binBuf);
    }

    free(readBuf);
    return true;
}

bool FDS_readDisk2(QStringList *messages, uint8_t **rawbuf, int *rawlen, uint8_t **binbuf, int *binlen, void(*callback)(void*,int), void *data) {
    enum { READBUFSIZE = 0x90000 };

    FILE *f;
    uint8_t *readBuf = NULL;
    int result;
    int bytesIn = 0;

    *rawbuf = 0;
    *rawlen = 0;
    *binbuf = 0;
    *binlen = 0;

    messages->append("Started read...\n");
    //if(!(dev_readIO()&MEDIA_SET)) {
    //    printf("Warning - Disk not inserted?\n");
    //}
    if (!dev.DiskReadStart()) {
        messages->append("DiskReadStart failed\n");
        return false;
    }

    readBuf = (uint8_t*)malloc(READBUFSIZE);

    do {
        result = dev.DiskRead(readBuf + bytesIn);
        bytesIn += result;
        if (!(bytesIn % ((DISK_READMAX)* 32))) {
            if(callback) {
                callback(data,bytesIn);
            }
//            printf(".");
        }
    } while (result == DISK_READMAX && bytesIn<READBUFSIZE - DISK_READMAX);
    printf("\n");

    if (result<0) {
        messages->append("Read error.\n");
        free(readBuf);
        free(*rawbuf);
        return false;
    }

    *rawbuf = (uint8_t*)malloc(bytesIn);
    *rawlen = bytesIn;
    memcpy(*rawbuf,readBuf,bytesIn);

    raw_to_raw03(readBuf, bytesIn);

    raw03_to_bin(messages,readBuf, bytesIn, binbuf, binlen);

/*    if (filename_raw) {
        if ((f = fopen(filename_raw, "wb"))) {
            fwrite(readBuf, 1, bytesIn, f);
            fclose(f);
            printf("Wrote %s\n", filename_raw);
        }
    }

    raw_to_raw03(readBuf, bytesIn);

    //decode to .fds
    if (filename_fds) {
        uint8_t *fds = (uint8_t*)malloc(FDSSIZE + 16);   //extra room for CRC junk
        raw03_to_fds(readBuf, fds, bytesIn);
        if ((f = fopen(filename_fds, "wb"))) {
            fwrite(fds, 1, FDSSIZE, f);
            fclose(f);
            printf("Wrote %s\n", filename_fds);
        }
        free(fds);

        //decode to .bin
    }
    else if (filename_bin) {
        uint8_t *binBuf;
        int binSize;

        raw03_to_bin(readBuf, bytesIn, &binBuf, &binSize);
        if ((f = fopen(filename_bin, "wb"))) {
            fwrite(binBuf, 1, binSize, f);
            fclose(f);
            printf("Wrote %s\n", filename_bin);
        }
        free(binBuf);
    }*/

    free(readBuf);
    return true;
}

static bool writeDisk2(uint8_t *bin, int binSize) {
    //	printf("writeDisk2: sending bin image to adaptor, size = %d\n", binSize);

    //	hexdump("bin", bin+ 3537, 256);

    if (dev.Sram->Write(bin, 0, binSize) == false) {
        printf("Sram write failed.\n");
        return(false);
    }

    if (!dev.DiskWriteStart())
        return false;

    return(true);

}

bool FDS_writeDisk(char *filename) {
    enum {
        LEAD_IN = DEFAULT_LEAD_IN / 8,
        DISKSIZE = 0x11000,               //whole disk contents including lead-in
    };

    uint8_t *inbuf = 0;       //.FDS buffer
    uint8_t *bin = 0;         //.FDS with gaps/CRC
    uint8_t *zero = 0;
    int filesize;
    int binSize;

    if (!loadfile(filename, &inbuf, &filesize))
    {
        printf("Can't read %s\n", filename); return false;
    }

    bin = (uint8_t*)malloc(DISKSIZE);
    zero = (uint8_t*)malloc(0x10000);
    memset(zero, 0, 0x10000);

    int inpos = 0, side = 0;
    if (inbuf[0] == 'F')
        inpos = 16;      //skip fwNES header

    filesize -= (filesize - inpos) % FDSSIZE;  //truncate down to whole disk

    char prompt;
    do {
        printf("Side %d\n", side + 1);

        if (dev.Sram->Write(zero, 0, 0x10000) == false) {
            printf("Sram write failed (zero).\n");
            return(false);
        }
        memset(bin, 0, LEAD_IN);
        binSize = fds_to_bin(bin + LEAD_IN, inbuf + inpos, DISKSIZE - LEAD_IN);
        if (!binSize)
            break;
        if (!writeDisk2(bin, binSize + LEAD_IN))
            break;
        inpos += FDSSIZE;
        side++;

        //		printf("finished write, inpos = %d, filesize = %d, inbuf[inpos] = %X", inpos, filesize, inbuf[inpos]);
        //prompt for disk change
        prompt = 0;
        if (inpos<filesize && inbuf[inpos] == 0x01) {
            printf("\nPlease wait for disk activity to stop before pressing ENTER to write the next disk side.\n");
            prompt = readKb();
            if(QMessageBox::information(NULL,"Message","Please wait for disk activity to stop, then flip the disk and press OK to begin writing the next disk side.",
                                     QMessageBox::Ok,QMessageBox::Cancel) == QMessageBox::Ok) {
                prompt = 0x0D;
            }
            else {
                prompt = 0;
            }

        }
        else {
            QMessageBox::information(NULL,"Message","\nDisk image sent to SRAM on device and is currently writing.\nPlease wait for disk activity to stop before removing disk.\n");
        }
    } while (prompt == 0x0d);

    free(bin);
    free(zero);
    free(inbuf);
    return true;
}

bool read_flash(char *filename_fds, int slot)
{
    enum {
        RAWSIZE = SLOTSIZE * 8,
    };

    static uint8_t fwnesHdr[16] = { 0x46, 0x44, 0x53, 0x1a, };

    FILE *f;
    uint8_t *bin, *raw, *fds;
    bool result = true;

    f = fopen(filename_fds, "wb");
    if (!f) {
        printf("Can't create %s\n", filename_fds);
        return false;
    }

    printf("Writing %s\n", filename_fds);
    fwnesHdr[4] = 0;
    fwrite(fwnesHdr, 1, sizeof(fwnesHdr), f);

    bin = (uint8_t*)malloc(SLOTSIZE);     //single side from flash
    raw = (uint8_t*)malloc(RAWSIZE);      //..to raw03
    fds = (uint8_t*)malloc(FDSSIZE);      //..to FDS

    int side = 0;
    for (; side + slot <= (int)dev.Slots; side++) {
        if (!dev.Flash->Read(bin,(slot + side)*SLOTSIZE, SLOTSIZE)) {
            result = false;
            break;
        }

        if (bin[0] == 0xff || (bin[0] != 0 && side != 0)) {    //stop on empty slot or next game
            break;
        }
        else if (bin[0] == 0 && side == 0) {
            printf("Warning! Not first side of game\n");
        }

        printf("Side %d\n", side + 1);
        memset(bin, 0, FLASHHEADERSIZE);  //clear header, use it as lead-in
        bin_to_raw03(bin, raw, SLOTSIZE, RAWSIZE);
        if (!raw03_to_fds(raw, fds, RAWSIZE)) {
            result = false;
            break;
        }
        fwrite(fds, 1, FDSSIZE, f);
        fwnesHdr[4]++;  //count sides written
    }

    fseek(f, 0, SEEK_SET);
    fwrite(fwnesHdr, 1, sizeof(fwnesHdr), f);      //update disk side count

    free(fds);
    free(raw);
    free(bin);
    fclose(f);
    return result;
}

bool write_flash(char *filename, int slot, void *data, void(*callback)(void*,uint32_t))
{
    enum { FILENAMELENGTH = 240, };   //number of characters including null

    uint8_t *inbuf = 0;
    uint8_t *outbuf = 0;
    int filesize;
    uint32_t i, j;
    char *shortName,headerName[256];
    TFlashHeader *headers;
    int duplicate = 0;

    dev.FlashUtil->ReadHeaders();
    headers = dev.FlashUtil->GetHeaders();
    if(headers == 0) {
        return(false);
    }
    if (!loadfile(filename, &inbuf, &filesize))
    {
        printf("Can't read %s\n", filename); return false;
    }

    int pos = 0, side = 0;
    if (inbuf[0] == 'F')
        pos = 16;      //skip fwNES header

    filesize -= (filesize - pos) % FDSSIZE;  //truncate down to whole disks
                                                          //strip path from filename
    shortName = strrchr(filename, '/');      // ...dir/file.fds
#ifdef _WIN32
    if (!shortName)
        shortName = strrchr(filename, '\\');        // ...dir\file.fds
    if (!shortName)
        shortName = strchr(filename, ':');         // C:file.fds
#endif
    if (!shortName)
        shortName = filename;
    else
        shortName++;

    //check if an image of the same name is is already stored
    for (i = 1; i < dev.Slots; i++) {
        if (strncmp(shortName, (char*)headers[i].filename, 240) == 0) {
            if (duplicate == 0) {
                if(QMessageBox::information(NULL,"Error","An image of the same name is already stored in flash.\n\nDo you want to write another copy?",
                                            QMessageBox::Yes,QMessageBox::No) == QMessageBox::No) {
                    delete[] inbuf;
                    return(false);
                }
            }
            duplicate++;
        }
    }

    //try to find an area to store the disk image
    if (slot == -1) {
        uint32_t totalslots = filesize / FDSSIZE;

        if (headers == 0) {
            delete[] inbuf;
            return(false);
        }
        for (i = 1; i < dev.Slots; i++) {

            //check if slot is empty
            if (headers[i].filename[0] == 0xFF) {

                //check for more empty slots adjacent to this one
                for (j = 1; j < totalslots; j++) {
                    if (headers[i + j].filename[0] != 0xFF) {
                        break;
                    }
                }

                //found an area sufficient to store this flash image
                if (j == totalslots) {
                    slot = i;
                    break;
                }

            }
        }
        if (slot == -1) {
            char buf[256];

            sprintf(buf,"Cannot find %d adjacent slots for storing disk image.\nPlease make room on the flash to store this disk image.\n", totalslots);
            QMessageBox::information(NULL,"Error",buf);
            delete[] inbuf;
            return(false);
        }

    }

    if (slot == -2) {
        printf("Updating loader...\n");
        slot = 0;
    }

    else if (slot == 0) {
        QMessageBox::information(NULL,"Error","Cannot write into slot 0, it holds the loader");
        delete[] inbuf;
        return(false);
    }

    outbuf = new uint8_t[SLOTSIZE];

    while (pos<filesize && inbuf[pos] == 0x01) {
        printf("Side %d", side + 1);
        if (fds_to_bin(outbuf + FLASHHEADERSIZE, inbuf + pos, SLOTSIZE - FLASHHEADERSIZE)) {
            memset(outbuf, 0, FLASHHEADERSIZE);
            uint32_t chksum = chksum_calc(outbuf + FLASHHEADERSIZE, SLOTSIZE - FLASHHEADERSIZE);
            outbuf[240] = (uint8_t)(chksum >> 0);
            outbuf[241] = (uint8_t)(chksum >> 8);
            outbuf[242] = (uint8_t)(chksum >> 16);
            outbuf[243] = (uint8_t)(chksum >> 24);
            outbuf[244] = DEFAULT_LEAD_IN & 0xff;
            outbuf[245] = DEFAULT_LEAD_IN / 256;

            if (side == 0) {
                if(duplicate == 0) {
                    strcpy(headerName,shortName);
                }
                else {
                    sprintf(headerName,"%s (%d)",shortName,duplicate);
                }
                strncpy((char*)outbuf, headerName, 240);
            }
            if(callback) {
                callback(data, (side << 24) | 0x10000000);
            }
            if (dev.Flash->Write(outbuf, (slot + side)*SLOTSIZE, SLOTSIZE, callback, data) == false) {
                printf("error.\n");
                break;
            }
            printf("done.\n");
        }
        pos += FDSSIZE;
        side++;
    }
    delete[] inbuf;
    delete[] outbuf;
    printf("\n");
    return true;
}

int FDS_getDiskSides(char *filename)
{
    uint8_t *buf;
    int size;

    if(loadfile(filename,&buf,&size) == false) {
        return(-1);
    }
    free(buf);
    return(size / 65500);
}

int FDS_findSlot(char *name)
{
    TFlashHeader *headers = dev.FlashUtil->GetHeaders();
    uint8_t *buf;

    for(uint32_t slot=1;slot<dev.Slots;slot++) {
        buf = headers[slot].filename;

        //match!
        if(strncmp(name,(char*)buf,256) == 0) {
            return(slot);
        }
    }
    return(-1);
}

int FDS_eraseSlot(int slot)
{
    TFlashHeader *headers = dev.FlashUtil->GetHeaders();
    uint8_t *buf;

    if(slot == 0) {
        QMessageBox::information(NULL,"Error","Cannot delete slot 0, it contains the loader.");
        return(1);
    }
    dev.Flash->EraseSlot(slot);
    for(uint32_t i=(slot + 1);i<dev.Slots;i++) {
        buf = headers[i].filename;
        if(buf[0]==0xff) {          //empty
            return(0);
        } else if(buf[0]!=0) {      //filename present
            return(0);
        } else {                    //next side
            dev.Flash->EraseSlot(i);
        }
    }
    return(0);
}

char loaderid[] = "]|<=--LOADER.FDS--=>|[";

bool DetectLoader(uint8_t *buf)
{
    int pos, len, count;
    uint8_t byte;

    for (pos = 0; pos<65500;) {

        //read a byte
        byte = buf[pos++];

        //first byte matches
        if (byte == loaderid[0]) {
            len = strlen(loaderid);
            for (count = 0; byte == loaderid[count] && count < len; count++) {
                byte = buf[pos + count];
            }
            if (count == len) {
                len = buf[pos + count];
                printf("Valid loader image found, version %d.%02d\n", len / 100, len % 100);
                return(true);
            }
        }
    }
    return(false);
}

/*bool loader_update(char *fn)
{
    bool ret = false;
    uint8_t *buf;
    int len;

    if (loadfile(fn, &buf, &len) == false) {
        printf("Error loading file '%s'\n", fn);
        return(false);
    }
    if (DetectLoader(buf) == false) {
        printf("Specified image doesnt appear to be the loader.\n");
    }
    else {
        ret = write_flash(fn, 0x10000, data, callback);
    }
    delete[] buf;
    return(ret);
}*/

/*bool WriteLoader(char *fn)
{
    bool ret = false;
    uint8_t *buf;
    int len;

    if (loadFile(fn, &buf, &len) == false) {
        printf("Error loading file '%s'\n", fn);
        return(false);
    }
    if (DetectLoader(buf) == false) {
        printf("Specified image doesnt appear to be the loader.\n");
    }
    else {
        ret = FDS_writeFlash(fn, 0);
    }
    free(buf);
    return(ret);
}*/

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    QString str;

    statusLabel = new QLabel("Ready");
    ui->setupUi(this);
    ui->statusBar->addWidget(statusLabel);

    setAcceptDrops(true);

    ui->listWidget->addAction(ui->action_Save);
//    ui->listWidget->addAction(ui->action_Info);
    ui->listWidget->addAction(ui->action_Delete);

    ui->action_Write_disk->setEnabled(false);

    if(dev.Open() == false) {
        QMessageBox::information(NULL,"Error","FDSemu not connected.");
        exit(1);
    }

    str.sprintf("Opened %s, %dMB flash (firmware build %d, flashID %06X)\n", dev.DeviceName, dev.FlashSize / 0x100000, dev.Version, dev.FlashID);
    statusLabel->setText(str);
    statusLabel->adjustSize();

    updateList();

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::updateList()
{
    TFlashHeader *headers;
    QStringList list;
    QString str;
    int empty = 0;
    uint32_t i;

    str.sprintf("Updating list...");
    ui->listWidget->setEnabled(false);
    ui->label->setText(str);
    ui->label->adjustSize();
    qApp->processEvents();

    dev.FlashUtil->ReadHeaders();
    headers = dev.FlashUtil->GetHeaders();
    list.clear();
    if(headers == 0) {
        QMessageBox::information(NULL, "Error", "Device was disconnected or not found");
        return;
    }
    i = (dev.Version > 792) ? 0 : 1;
    for(;i<dev.Slots;i++) {
        if(headers[i].filename[0] == 0xFF) {
            empty++;
        }
        else if(headers[i].filename[0] != 0) {
            QString str;

            str.sprintf("%s",headers[i].filename);
            list.append(str);
        }
    }

    str.sprintf("%d empty slots.", empty);
    ui->listWidget->clear();
    ui->listWidget->addItems(list);
    ui->listWidget->sortItems(Qt::AscendingOrder);
    ui->label->setText(str);
    ui->label->adjustSize();
    ui->listWidget->setEnabled(true);
    qApp->processEvents();
}

void MainWindow::openFiles(QStringList &list)
{
    WriteFilesDialog *wfd = new WriteFilesDialog(this);
    QString str;

    wfd->writeFiles(list);

    delete wfd;
    qApp->processEvents();

    updateList();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData* mimeData = event->mimeData();

    // check for our needed mime type, here a file or a list of files
    if (mimeData->hasUrls()) {
        QStringList pathList;
        QList<QUrl> urlList = mimeData->urls();

        // extract the local paths of the files
        for (int i = 0; i < urlList.size() && i < 64; i++) {
            pathList.append(urlList.at(i).toLocalFile());
        }

        event->acceptProposedAction();

        // call a function to open the files
        openFiles(pathList);
    }
}

void MainWindow::on_actionE_xit_triggered()
{
    this->close();
}

void MainWindow::on_action_Delete_triggered()
{
    if(ui->listWidget->selectedItems().count() == 1) {
        QString str = ui->listWidget->selectedItems().at(0)->text();
        int slot = FDS_findSlot((char*)str.toStdString().c_str());

        ui->listWidget->setEnabled(false);
        qApp->processEvents();
        FDS_eraseSlot(slot);
        updateList();
    }
    else {
        QMessageBox::information(NULL,"Error","Please select a disk image to delete in the list.");
    }
}

void MainWindow::on_action_About_triggered()
{
    QString str;

    str.sprintf("Qt GUI code by James Holodnak\n\nDisk utility code by loopy\n\nVersion %d.%d",VERSION_HI, VERSION_LO);
    QMessageBox::information(NULL,"About",str);
}

void MainWindow::on_action_Write_disk_triggered()
{

}

void MainWindow::on_action_Write_disk_image_triggered()
{
    QFileDialog dialog(this);
    QStringList filenames;

    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilter(tr("fwNES FDS Images (*.fds)"));
    if (dialog.exec()) {
        filenames = dialog.selectedFiles();
        openFiles(filenames);
    }
}

void MainWindow::on_action_Save_disk_image_triggered()
{
    //save disk
    if(ui->listWidget->selectedItems().count() == 1) {
        QString str, filename;
        int slot;

        str = ui->listWidget->selectedItems().at(0)->text();
        slot = FDS_findSlot((char*)str.toStdString().c_str());
        if(slot == -1) {
            QMessageBox::information(NULL,"Error (bug)","Selected disk image slot not able to be determined.\n\nPlease email james@fdsemu.com with the following information:\nScreenshot of fdsemu-qt with disk image filename visible.\nFull name of disk image.\nFDSemu flash size.");
            return;
        }
        filename = QFileDialog::getSaveFileName(this, tr("Save File"),str,tr("fwNES FDS Images (*.fds)"));
        if(filename != NULL) {
            if(read_flash((char*)filename.toStdString().c_str(),slot) == false) {
                QMessageBox::information(NULL,"Error","Cannot save disk image.");
            }
        }
        return;
    }
    QMessageBox::information(NULL,"Error","Please select one disk image to save in the list.");
}

void MainWindow::on_action_Save_triggered()
{
    on_action_Save_disk_image_triggered();
}

void MainWindow::on_actionUpdate_loader_triggered()
{
    QString filename;

    if(dev.Version > 792) {
        QMessageBox::information(NULL,"Error","Firmware builds after 792 have the loader built-in.  No updating is possible.");
        return;
    }
    filename = QFileDialog::getOpenFileName(this, tr("Open loader disk image"), "", tr("fwNES FDS Images (*.fds)"));
    if(filename != "") {
        char *fn = (char*)filename.toStdString().c_str();
        uint8_t *buf;
        int len;

        if (loadfile(fn, &buf, &len) == false) {
//            printf("Error loading file '%s'\n", fn);
            //return(false);
        }
        else if (DetectLoader(buf) == false) {
            QMessageBox::information(NULL,"Error","Specified image doesnt appear to be a valid loader image.");
            printf("Specified image doesnt appear to be the loader.\n");
        }
        else {
            WriteStatus *fw = new WriteStatus(this);

            fw->writeloader(filename);
        }
        delete[] buf;
    }
}

void MainWindow::on_action_Erase_triggered()
{
    on_action_Delete_triggered();
}

void MainWindow::on_actionUpdate_firmware_triggered()
{
    QString filename;

    filename = QFileDialog::getOpenFileName(this, tr("Open firmware image"), "", tr("Firmware Images (*.bin)"));
    if(filename != "") {
        WriteStatus *fw = new WriteStatus(this);
        QString str;

        fw->writefirmware(filename);
        str.sprintf("Opened %s, %dMB flash (firmware build %d, flashID %06X)\n", dev.DeviceName, dev.FlashSize / 0x100000, dev.Version, dev.FlashID);
        statusLabel->setText(str);
        statusLabel->adjustSize();
    }

}

void MainWindow::on_action_Read_disk_triggered()
{
    DiskReadDialog *drd = new DiskReadDialog(this);

    drd->exec();
}
