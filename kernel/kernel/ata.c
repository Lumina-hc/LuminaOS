#include "../include/kernel/ata.h"
#include "kernel/io.h"

#define ATA_DATA        0x1F0
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_COMMAND     0x1F7
#define ATA_STATUS      0x1F7

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30

static int ata_poll(void)
{
    int timeout = 10000000;
    while (timeout--) {
        uint8_t st = inb(ATA_STATUS);
        if (st & 0x80) continue;
        if (!(st & 0x08)) continue;
        if (st & 0x01) return -1;
        return 0;
    }
    return -1;
}

int ata_read_sector(uint32_t lba, void* buffer)
{
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_READ);

    if (ata_poll() != 0) return -1;
    for (int i = 0; i < 256; i++)
        ((uint16_t*)buffer)[i] = inw(ATA_DATA);
    return 0;
}

int ata_write_sector(uint32_t lba, const void* buffer)
{
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    if (ata_poll() != 0) return -1;
    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, ((uint16_t*)buffer)[i]);

    int timeout = 10000000;
    while (timeout--) {
        uint8_t st = inb(ATA_STATUS);
        if (st & 0x80) continue;
        if (st & 0x01) return -1;
        return 0;
    }
    return -1;
}
