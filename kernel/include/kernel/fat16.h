#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>

#define FAT16_ATTR_READ_ONLY  0x01
#define FAT16_ATTR_HIDDEN     0x02
#define FAT16_ATTR_SYSTEM     0x04
#define FAT16_ATTR_VOLUME_ID  0x08
#define FAT16_ATTR_DIRECTORY  0x10
#define FAT16_ATTR_ARCHIVE    0x20

#define FAT16_EOF 0xFFF8

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint8_t media_descriptor;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;

    uint16_t root_dir_sectors;
    uint16_t root_dir_lba;
    uint16_t data_lba;
    uint16_t total_clusters;
    uint16_t cur_cluster;
} fat16_t;

typedef struct {
    char name[11];
    uint8_t attr;
    uint8_t reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t first_cluster;
    uint32_t size;
} __attribute__((packed)) fat16_entry_t;

int fat16_init(void);
void fat16_set_write_attr(uint8_t attr);
int fat16_cd(const char* name);
int fat16_mkdir(const char* name);
int fat16_read_file(const char* name, void* buffer, uint32_t max_size, uint32_t* out_size);
int fat16_read_file_in(uint16_t dir, const char* name, void* buffer, uint32_t max_size, uint32_t* out_size);
int fat16_write_file(const char* name, const void* data, uint32_t size);
int fat16_write_file_in(uint16_t dir, const char* name, const void* data, uint32_t size);
int fat16_delete_file(const char* name);
int fat16_list_dir(fat16_entry_t* entries, int max_entries, int* out_count);
int fat16_dir_total_size(uint16_t cluster, uint32_t* out);
int fat16_get_file_size(const char* name, uint32_t* out_size);

#endif
