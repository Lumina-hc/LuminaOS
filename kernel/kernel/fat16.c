#include <kernel/fat16.h>
#include <kernel/ata.h>
#include <kernel/io.h>

static fat16_t fs;
static uint8_t fat16_next_attr = FAT16_ATTR_ARCHIVE;

void fat16_set_write_attr(uint8_t attr)
{
    fat16_next_attr = attr;
}

static char fat16_toupper(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}

static void fat16_build_name(const char* name, char* out)
{
    int i;
    for (i = 0; i < 11; i++)
        out[i] = ' ';
    int j;
    for (j = 0; name[j] && name[j] != '.' && j < 8; j++)
        out[j] = fat16_toupper(name[j]);
    if (name[j] == '.') {
        j++;
        for (int k = 0; name[j + k] && k < 3; k++)
            out[8 + k] = fat16_toupper(name[j + k]);
    }
}

static int fat16_read_sector(uint16_t lba, void* buffer)
{
    return ata_read_sector(lba, buffer);
}

static int fat16_write_sector(uint16_t lba, const void* buffer)
{
    return ata_write_sector(lba, buffer);
}

int fat16_init(void)
{
    uint8_t bpb[512];
    if (fat16_read_sector(0, bpb) != 0) {
        debug_puts("Failed to read BPB from ATA\n");
        return -1;
    }

    fs.bytes_per_sector = bpb[11] | (bpb[12] << 8);
    fs.sectors_per_cluster = bpb[13];
    fs.reserved_sectors = bpb[14] | (bpb[15] << 8);
    fs.fat_count = bpb[16];
    fs.root_entries = bpb[17] | (bpb[18] << 8);
    fs.total_sectors = bpb[19] | (bpb[20] << 8);
    fs.media_descriptor = bpb[21];
    fs.sectors_per_fat = bpb[22] | (bpb[23] << 8);
    fs.sectors_per_track = bpb[24] | (bpb[25] << 8);
    fs.head_count = bpb[26] | (bpb[27] << 8);
    fs.hidden_sectors = bpb[28] | (bpb[29] << 8) | (bpb[30] << 16) | (bpb[31] << 24);

    fs.root_dir_sectors = (fs.root_entries * 32 + fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    fs.root_dir_lba = fs.reserved_sectors + (fs.fat_count * fs.sectors_per_fat);
    fs.data_lba = fs.root_dir_lba + fs.root_dir_sectors;
    fs.total_clusters = (fs.total_sectors - fs.data_lba) / fs.sectors_per_cluster;
    fs.cur_cluster = 0;

    return 0;
}

static int fat16_get_fat_entry(uint16_t cluster, uint16_t* out)
{
    uint8_t fat_buf[512];
    uint16_t fat_sector = (uint16_t)((cluster * 2) / fs.bytes_per_sector);
    uint16_t entry_off = (uint16_t)((cluster * 2) % fs.bytes_per_sector);

    if (fat16_read_sector((uint16_t)(fs.reserved_sectors + fat_sector), fat_buf) != 0)
        return -1;

    *out = fat_buf[entry_off] | (fat_buf[entry_off + 1] << 8);
    return 0;
}

static int fat16_set_fat_entry(uint16_t cluster, uint16_t value)
{
    uint8_t fat_buf[512];
    uint16_t fat_lba = fs.reserved_sectors;
    uint32_t entry_offset = cluster * 2;
    uint16_t fat_sector = (uint16_t)(entry_offset / fs.bytes_per_sector);
    uint16_t entry_in_sector = (uint16_t)(entry_offset % fs.bytes_per_sector);

    if (fat16_read_sector(fat_lba + fat_sector, fat_buf) != 0)
        return -1;

    fat_buf[entry_in_sector] = value & 0xFF;
    fat_buf[entry_in_sector + 1] = (value >> 8) & 0xFF;

    if (fat16_write_sector(fat_lba + fat_sector, fat_buf) != 0)
        return -1;

    for (uint8_t copy = 1; copy < fs.fat_count; copy++) {
        uint16_t copy_lba = fs.reserved_sectors + copy * fs.sectors_per_fat;
        if (fat16_read_sector(copy_lba + fat_sector, fat_buf) != 0)
            return -1;

        fat_buf[entry_in_sector] = value & 0xFF;
        fat_buf[entry_in_sector + 1] = (value >> 8) & 0xFF;

        if (fat16_write_sector(copy_lba + fat_sector, fat_buf) != 0)
            return -1;
    }

    return 0;
}

static uint16_t fat16_find_free_cluster(void)
{
    uint8_t fat_buf[512];
    uint16_t fat_lba = fs.reserved_sectors;
    uint16_t entries_per_sector = fs.bytes_per_sector / 2;
    uint16_t total_fat_entries = fs.sectors_per_fat * entries_per_sector;

    for (uint16_t c = 2; c < total_fat_entries && c < fs.total_clusters + 2; c++) {
        uint16_t fat_sector = (uint16_t)((c * 2) / fs.bytes_per_sector);
        uint16_t entry_off = (uint16_t)((c * 2) % fs.bytes_per_sector);

        if (fat16_read_sector(fat_lba + fat_sector, fat_buf) != 0)
            return 0;

        uint16_t val = fat_buf[entry_off] | (fat_buf[entry_off + 1] << 8);
        if (val == 0x0000)
            return c;
    }
    return 0;
}

static void fat16_free_clusters(uint16_t cluster)
{
    uint16_t c = cluster;
    while (c >= 2 && c < FAT16_EOF) {
        uint16_t next;
        if (fat16_get_fat_entry(c, &next) != 0)
            break;
        fat16_set_fat_entry(c, 0x0000);
        if (next >= FAT16_EOF)
            break;
        c = next;
    }
}

static uint32_t fat16_dir_blocks(uint16_t dir)
{
    if (dir == 0)
        return fs.root_dir_sectors;

    uint32_t blocks = 0;
    uint16_t c = dir;
    while (c >= 2 && c < fs.total_clusters + 2) {
        blocks += fs.sectors_per_cluster;
        uint16_t next;
        if (fat16_get_fat_entry(c, &next) != 0)
            break;
        if (next >= FAT16_EOF)
            break;
        c = next;
    }
    return blocks;
}

static int fat16_dir_read_block(uint16_t dir, uint32_t block, uint8_t* buf)
{
    if (dir == 0) {
        if (block >= fs.root_dir_sectors)
            return -1;
        return fat16_read_sector((uint16_t)(fs.root_dir_lba + block), buf);
    }

    uint16_t c = dir;
    while (c >= 2 && c < fs.total_clusters + 2) {
        if (block < fs.sectors_per_cluster)
            return fat16_read_sector((uint16_t)(fs.data_lba + (c - 2) * fs.sectors_per_cluster + block), buf);
        block -= fs.sectors_per_cluster;
        uint16_t next;
        if (fat16_get_fat_entry(c, &next) != 0)
            return -1;
        if (next >= FAT16_EOF)
            break;
        c = next;
    }
    return -1;
}

static int fat16_dir_write_block(uint16_t dir, uint32_t block, const uint8_t* buf)
{
    if (dir == 0) {
        if (block >= fs.root_dir_sectors)
            return -1;
        return fat16_write_sector((uint16_t)(fs.root_dir_lba + block), buf);
    }

    uint16_t c = dir;
    while (c >= 2 && c < fs.total_clusters + 2) {
        if (block < fs.sectors_per_cluster)
            return fat16_write_sector((uint16_t)(fs.data_lba + (c - 2) * fs.sectors_per_cluster + block), buf);
        block -= fs.sectors_per_cluster;
        uint16_t next;
        if (fat16_get_fat_entry(c, &next) != 0)
            return -1;
        if (next >= FAT16_EOF)
            break;
        c = next;
    }
    return -1;
}

static int fat16_dir_extend(uint16_t dir)
{
    if (dir == 0)
        return -1;

    uint16_t c = dir;
    while (c >= 2 && c < fs.total_clusters + 2) {
        uint16_t next;
        if (fat16_get_fat_entry(c, &next) != 0)
            return -1;
        if (next >= FAT16_EOF) {
            uint16_t nc = fat16_find_free_cluster();
            if (nc == 0)
                return -1;
            if (fat16_set_fat_entry(c, nc) != 0)
                return -1;
            if (fat16_set_fat_entry(nc, FAT16_EOF) != 0)
                return -1;
            uint8_t zero[512];
            for (int i = 0; i < 512; i++)
                zero[i] = 0;
            for (uint32_t s = 0; s < fs.sectors_per_cluster; s++)
                fat16_write_sector((uint16_t)(fs.data_lba + (nc - 2) * fs.sectors_per_cluster + s), zero);
            return 0;
        }
        c = next;
    }
    return -1;
}

static void fat16_copy_entry(fat16_entry_t* dst, const fat16_entry_t* src)
{
    int i;
    for (i = 0; i < 11; i++) dst->name[i] = src->name[i];
    dst->attr = src->attr;
    for (i = 0; i < 10; i++) dst->reserved[i] = src->reserved[i];
    dst->time = src->time;
    dst->date = src->date;
    dst->first_cluster = src->first_cluster;
    dst->size = src->size;
}

static int fat16_dir_find(uint16_t dir, const char* name, fat16_entry_t* out_entry, int* out_index)
{
    char fat_name[11];
    fat16_build_name(name, fat_name);

    uint32_t total_entries = fat16_dir_blocks(dir) * (fs.bytes_per_sector / 32);
    uint8_t buf[512];
    int entries_per_block = fs.bytes_per_sector / 32;
    uint32_t cur_block = 0xFFFFFFFF;

    for (uint32_t i = 0; i < total_entries; i++) {
        uint32_t b = i / entries_per_block;
        if (b != cur_block) {
            if (fat16_dir_read_block(dir, b, buf) != 0)
                return -1;
            cur_block = b;
        }
        fat16_entry_t* entry = (fat16_entry_t*)(buf + (i % entries_per_block) * 32);
        if (entry->name[0] == 0) {
            if (out_index) *out_index = (int)i;
            return 0;
        }
        if ((uint8_t)entry->name[0] == 0xE5) continue;
        if (entry->attr == 0x0F) continue;
        if (entry->attr & FAT16_ATTR_VOLUME_ID) continue;

        int match = 1;
        for (int k = 0; k < 11; k++) {
            if (entry->name[k] != fat_name[k]) { match = 0; break; }
        }
        if (match) {
            if (out_entry)
                fat16_copy_entry(out_entry, entry);
            if (out_index) *out_index = (int)i;
            return 1;
        }
    }
    return -1;
}

static int fat16_dir_first_free(uint16_t dir, int* out_index)
{
    uint32_t total_entries = fat16_dir_blocks(dir) * (fs.bytes_per_sector / 32);
    uint8_t buf[512];
    int entries_per_block = fs.bytes_per_sector / 32;
    uint32_t cur_block = 0xFFFFFFFF;

    for (uint32_t i = 0; i < total_entries; i++) {
        uint32_t b = i / entries_per_block;
        if (b != cur_block) {
            if (fat16_dir_read_block(dir, b, buf) != 0)
                return -1;
            cur_block = b;
        }
        fat16_entry_t* entry = (fat16_entry_t*)(buf + (i % entries_per_block) * 32);
        if (entry->name[0] == 0 || (uint8_t)entry->name[0] == 0xE5) {
            *out_index = (int)i;
            return 0;
        }
    }
    return -1;
}

static int fat16_dir_set_entry(uint16_t dir, int index, const fat16_entry_t* entry)
{
    int entries_per_block = fs.bytes_per_sector / 32;
    uint32_t block = index / entries_per_block;
    int offset = (index % entries_per_block) * 32;
    uint8_t buf[512];

    if (fat16_dir_read_block(dir, block, buf) != 0)
        return -1;

    for (int i = 0; i < 11; i++) buf[offset + i] = entry->name[i];
    buf[offset + 11] = entry->attr;
    buf[offset + 12] = 0;
    buf[offset + 13] = 0;
    buf[offset + 14] = 0;
    buf[offset + 15] = 0;
    buf[offset + 16] = 0;
    buf[offset + 17] = 0;
    buf[offset + 18] = 0;
    buf[offset + 19] = 0;
    buf[offset + 20] = 0;
    buf[offset + 21] = 0;
    buf[offset + 22] = (uint8_t)(entry->time & 0xFF);
    buf[offset + 23] = (uint8_t)((entry->time >> 8) & 0xFF);
    buf[offset + 24] = (uint8_t)(entry->date & 0xFF);
    buf[offset + 25] = (uint8_t)((entry->date >> 8) & 0xFF);
    buf[offset + 26] = (uint8_t)(entry->first_cluster & 0xFF);
    buf[offset + 27] = (uint8_t)((entry->first_cluster >> 8) & 0xFF);
    buf[offset + 28] = (uint8_t)(entry->size & 0xFF);
    buf[offset + 29] = (uint8_t)((entry->size >> 8) & 0xFF);
    buf[offset + 30] = (uint8_t)((entry->size >> 16) & 0xFF);
    buf[offset + 31] = (uint8_t)((entry->size >> 24) & 0xFF);

    return fat16_dir_write_block(dir, block, buf);
}

static int fat16_dir_mark_deleted(uint16_t dir, int index, fat16_entry_t* out_entry)
{
    int entries_per_block = fs.bytes_per_sector / 32;
    uint32_t block = index / entries_per_block;
    int offset = (index % entries_per_block) * 32;
    uint8_t buf[512];

    if (fat16_dir_read_block(dir, block, buf) != 0)
        return -1;

    fat16_entry_t* entry = (fat16_entry_t*)(buf + offset);
    if (out_entry)
        fat16_copy_entry(out_entry, entry);

    buf[offset] = 0xE5;
    return fat16_dir_write_block(dir, block, buf);
}

int fat16_read_file_in(uint16_t dir, const char* name, void* buffer, uint32_t max_size, uint32_t* out_size)
{
    fat16_entry_t entry;
    int found = fat16_dir_find(dir, name, &entry, 0);
    if (found != 1) return -1;
    if (entry.attr & FAT16_ATTR_DIRECTORY) return -1;

    uint32_t size = entry.size;
    if (size > max_size) size = max_size;

    uint32_t offset = 0;
    uint16_t current = entry.first_cluster;
    uint32_t cluster_size = fs.bytes_per_sector * fs.sectors_per_cluster;

    while (current >= 2 && current < fs.total_clusters + 2) {
        uint32_t lba = fs.data_lba + (current - 2) * fs.sectors_per_cluster;
        uint32_t remaining = size - offset;
        uint32_t to_read = remaining < cluster_size ? remaining : cluster_size;

        uint32_t full = to_read / fs.bytes_per_sector;
        for (uint32_t i = 0; i < full; i++) {
            if (fat16_read_sector((uint16_t)(lba + i), (uint8_t*)buffer + offset + i * fs.bytes_per_sector) != 0)
                return -1;
        }
        uint32_t tail = to_read % fs.bytes_per_sector;
        if (tail != 0) {
            uint8_t tmp[512];
            if (fat16_read_sector((uint16_t)(lba + full), tmp) != 0)
                return -1;
            for (uint32_t i = 0; i < tail; i++)
                ((uint8_t*)buffer)[offset + full * fs.bytes_per_sector + i] = tmp[i];
        }
        offset += to_read;

        uint16_t next;
        if (fat16_get_fat_entry(current, &next) != 0)
            return -1;
        if (next >= FAT16_EOF)
            break;
        current = next;
    }

    if (out_size) *out_size = offset;
    return 0;
}

int fat16_read_file(const char* name, void* buffer, uint32_t max_size, uint32_t* out_size)
{
    return fat16_read_file_in(fs.cur_cluster, name, buffer, max_size, out_size);
}

int fat16_get_file_size(const char* name, uint32_t* out_size)
{
    fat16_entry_t entry;
    int found = fat16_dir_find(fs.cur_cluster, name, &entry, 0);
    if (found != 1) return -1;
    if (entry.attr & FAT16_ATTR_DIRECTORY) return -1;
    if (out_size) *out_size = entry.size;
    return 0;
}

int fat16_write_file_in(uint16_t dir, const char* name, const void* data, uint32_t size)
{
    uint32_t cluster_size = fs.bytes_per_sector * fs.sectors_per_cluster;
    uint16_t clusters_needed = (uint16_t)((size + cluster_size - 1) / cluster_size);

    fat16_entry_t existing_entry;
    int entry_index = -1;
    int found = fat16_dir_find(dir, name, &existing_entry, &entry_index);

    if (found == 1) {
        fat16_free_clusters(existing_entry.first_cluster);
    } else if (found == -1) {
        if (fat16_dir_first_free(dir, &entry_index) != 0) {
            if (dir != 0) {
                if (fat16_dir_extend(dir) != 0)
                    return -1;
                if (fat16_dir_first_free(dir, &entry_index) != 0)
                    return -1;
            } else {
                return -1;
            }
        }
    }

    uint16_t prev_cluster = 0;
    uint16_t first_cluster = 0;
    const uint8_t* src = (const uint8_t*)data;
    uint32_t remaining = size;

    for (uint16_t i = 0; i < clusters_needed; i++) {
        uint16_t cl = fat16_find_free_cluster();
        if (cl == 0) {
            if (first_cluster) fat16_free_clusters(first_cluster);
            return -1;
        }

        fat16_set_fat_entry(cl, FAT16_EOF);

        if (i == 0) first_cluster = cl;

        if (prev_cluster != 0)
            fat16_set_fat_entry(prev_cluster, cl);

        uint32_t lba = fs.data_lba + (cl - 2) * fs.sectors_per_cluster;
        uint32_t to_write = remaining > cluster_size ? cluster_size : remaining;

        uint8_t sector_buf[512];
        for (uint32_t s = 0; s < (to_write + fs.bytes_per_sector - 1) / fs.bytes_per_sector; s++) {
            for (uint32_t b = 0; b < fs.bytes_per_sector; b++) {
                uint32_t src_idx = s * fs.bytes_per_sector + b;
                sector_buf[b] = (src_idx < to_write) ? src[src_idx] : 0;
            }
            if (fat16_write_sector((uint16_t)(lba + s), sector_buf) != 0) {
                if (first_cluster) fat16_free_clusters(first_cluster);
                return -1;
            }
        }

        src += to_write;
        remaining -= to_write;
        prev_cluster = cl;
    }

    if (prev_cluster != 0)
        fat16_set_fat_entry(prev_cluster, FAT16_EOF);

    fat16_entry_t new_entry;
    fat16_build_name(name, new_entry.name);
    new_entry.attr = fat16_next_attr;
    fat16_next_attr = FAT16_ATTR_ARCHIVE;
    for (int i = 0; i < 10; i++) new_entry.reserved[i] = 0;
    new_entry.time = 0;
    new_entry.date = 0;
    new_entry.first_cluster = first_cluster;
    new_entry.size = size;

    return fat16_dir_set_entry(dir, entry_index, &new_entry);
}

int fat16_write_file(const char* name, const void* data, uint32_t size)
{
    return fat16_write_file_in(fs.cur_cluster, name, data, size);
}

int fat16_delete_file(const char* name)
{
    int entry_index;
    int found = fat16_dir_find(fs.cur_cluster, name, 0, &entry_index);
    if (found != 1) return -1;

    fat16_entry_t entry;
    if (fat16_dir_mark_deleted(fs.cur_cluster, entry_index, &entry) != 0)
        return -1;

    fat16_free_clusters(entry.first_cluster);
    return 0;
}

static int fat16_list_dir_in(uint16_t cluster, fat16_entry_t* entries, int max_entries, int* out_count)
{
    uint32_t total_entries = fat16_dir_blocks(cluster) * (fs.bytes_per_sector / 32);
    uint8_t buf[512];
    int entries_per_block = fs.bytes_per_sector / 32;
    uint32_t cur_block = 0xFFFFFFFF;
    int count = 0;

    for (uint32_t i = 0; i < total_entries && count < max_entries; i++) {
        uint32_t b = i / entries_per_block;
        if (b != cur_block) {
            if (fat16_dir_read_block(cluster, b, buf) != 0)
                return -1;
            cur_block = b;
        }
        fat16_entry_t* entry = (fat16_entry_t*)(buf + (i % entries_per_block) * 32);
        if (entry->name[0] == 0) break;
        if ((uint8_t)entry->name[0] == 0xE5) continue;
        if (entry->attr == 0x0F) continue;
        if (entry->attr & (FAT16_ATTR_HIDDEN | FAT16_ATTR_SYSTEM | FAT16_ATTR_VOLUME_ID)) continue;

        fat16_copy_entry(&entries[count], entry);
        count++;
    }

    if (out_count) *out_count = count;
    return 0;
}

int fat16_list_dir(fat16_entry_t* entries, int max_entries, int* out_count)
{
    return fat16_list_dir_in(fs.cur_cluster, entries, max_entries, out_count);
}

int fat16_dir_total_size(uint16_t cluster, uint32_t* out)
{
    fat16_entry_t entries[64];
    int count;
    if (fat16_list_dir_in(cluster, entries, 64, &count) != 0)
        return -1;
    uint32_t sum = 0;
    for (int i = 0; i < count; i++) {
        if (!(entries[i].attr & FAT16_ATTR_DIRECTORY))
            sum += entries[i].size;
    }
    *out = sum;
    return 0;
}

static void fat16_set_dot_entry(uint8_t* buf, int offset, const char* name, uint16_t cluster)
{
    fat16_entry_t* e = (fat16_entry_t*)(buf + offset);
    int i;
    for (i = 0; i < 8; i++) e->name[i] = ' ';
    for (i = 0; i < 3; i++) e->name[8 + i] = ' ';
    int len = 0;
    while (name[len]) len++;
    for (i = 0; i < len && i < 8; i++) e->name[i] = name[i];
    e->attr = FAT16_ATTR_DIRECTORY;
    for (i = 0; i < 10; i++) e->reserved[i] = 0;
    e->time = 0;
    e->date = 0;
    e->first_cluster = cluster;
    e->size = 0;
}

int fat16_mkdir(const char* name)
{
    int idx;
    int found = fat16_dir_find(fs.cur_cluster, name, 0, &idx);
    if (found == 1) return -1;

    if (found == -1) {
        if (fat16_dir_first_free(fs.cur_cluster, &idx) != 0) {
            if (fs.cur_cluster != 0) {
                if (fat16_dir_extend(fs.cur_cluster) != 0)
                    return -1;
                if (fat16_dir_first_free(fs.cur_cluster, &idx) != 0)
                    return -1;
            } else {
                return -1;
            }
        }
    }

    uint16_t cl = fat16_find_free_cluster();
    if (cl == 0) return -1;
    fat16_set_fat_entry(cl, FAT16_EOF);

    uint8_t buf[512];
    for (int i = 0; i < 512; i++) buf[i] = 0;
    fat16_set_dot_entry(buf, 0, ".", cl);
    fat16_set_dot_entry(buf, 32, "..", fs.cur_cluster);
    for (uint32_t s = 0; s < fs.sectors_per_cluster; s++)
        fat16_write_sector((uint16_t)(fs.data_lba + (cl - 2) * fs.sectors_per_cluster + s), buf);

    fat16_entry_t new_entry;
    fat16_build_name(name, new_entry.name);
    new_entry.attr = FAT16_ATTR_DIRECTORY;
    for (int i = 0; i < 10; i++) new_entry.reserved[i] = 0;
    new_entry.time = 0;
    new_entry.date = 0;
    new_entry.first_cluster = cl;
    new_entry.size = 0;

    return fat16_dir_set_entry(fs.cur_cluster, idx, &new_entry);
}

int fat16_cd(const char* name)
{
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        if (fs.cur_cluster == 0) return 0;
        uint8_t buf[512];
        if (fat16_dir_read_block(fs.cur_cluster, 0, buf) != 0)
            return -1;
        fat16_entry_t* dd = (fat16_entry_t*)(buf + 32);
        fs.cur_cluster = dd->first_cluster;
        return 0;
    }

    fat16_entry_t entry;
    int found = fat16_dir_find(fs.cur_cluster, name, &entry, 0);
    if (found != 1) return -1;
    if (!(entry.attr & FAT16_ATTR_DIRECTORY)) return -1;

    fs.cur_cluster = entry.first_cluster;
    return 0;
}
