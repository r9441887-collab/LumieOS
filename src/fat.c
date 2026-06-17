#include "fat.h"
#include "mm.h"
#include "ahci.h"

/* BIOS Parameter Block (FAT32) */
#pragma pack(push, 1)
typedef struct {
    u8 jmp[3];
    u8 oem[8];
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 num_fats;
    u16 root_entries;
    u16 total_sectors_16;
    u8 media_descriptor;
    u16 sectors_per_fat_16;
    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 total_sectors_32;

    /* FAT32 specific */
    u32 sectors_per_fat_32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fs_info;
    u16 backup_boot_sector;
    u8 reserved[12];
    u8 drive_number;
    u8 reserved1;
    u8 boot_signature;
    u32 volume_id;
    u8 volume_label[11];
    u8 fs_type[8];
} __attribute__((packed)) fat_bpb;

typedef struct {
    u8 name[11];
    u8 attr;
    u8 nt_reserved;
    u8 tenths;
    u16 time_created;
    u16 date_created;
    u16 date_accessed;
    u16 cluster_high;
    u16 time_modified;
    u16 date_modified;
    u16 cluster_low;
    u32 size;
} __attribute__((packed)) fat_dirent;
#pragma pack(pop)

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN 0x02
#define FAT_ATTR_SYSTEM 0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE 0x20
#define FAT_ATTR_LFN 0x0F

#define FAT_END_OF_CHAIN 0x0FFFFFF8

/* We need disk I/O from UEFI */
static efi_boot_services *g_bs = NULL;
static efi_handle g_image = NULL;
static efi_system_table *g_st = NULL;

static int g_use_ahci = 0;

static int fat_init_bpb(void);

int fat_use_ahci(void) {
    if (!ahci_is_ready()) return -1;
    if (fat_init_bpb() != 0) return -1;
    g_use_ahci = 1;
    return 0;
}

/* FAT32 state */
static fat_bpb bpb;
static int fat_initialized = 0;
static u32 first_data_sector;
static u32 root_dir_sectors;
static u32 total_clusters;
static u32 fat_size;

/* Disk read helper */
static efi_block_io_protocol *block_io = NULL;

static int read_sectors(u32 lba, u32 count, void *buffer) {
    if (g_use_ahci) {
        return ahci_read_sectors(lba, count, buffer);
    }
    if (!block_io) return -1;
    u32 sector_size = bpb.bytes_per_sector ? bpb.bytes_per_sector : 512;

    efi_status status = block_io->ReadBlocks(block_io, block_io->Media->MediaId, (u64)lba, (u64)count * sector_size, buffer);
    if (EFI_ERROR(status)) return -1;
    return 0;
}

static int write_sectors(u32 lba, u32 count, void *buffer) {
    if (g_use_ahci) {
        return ahci_write_sectors(lba, count, buffer);
    }
    if (!block_io) return -1;
    u32 sector_size = bpb.bytes_per_sector ? bpb.bytes_per_sector : 512;
    efi_status status = block_io->WriteBlocks(block_io, block_io->Media->MediaId, (u64)lba, (u64)count * sector_size, buffer);
    if (EFI_ERROR(status)) return -1;
    return 0;
}

static u32 fat_read_fat_entry(u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    u32 byte_offset = fat_offset % bpb.bytes_per_sector;
    u8 *sector = kmalloc(bpb.bytes_per_sector);
    if (!sector) return 0xFFFFFFFF;
    if (read_sectors(fat_sector, 1, sector) != 0) { kfree(sector); return 0xFFFFFFFF; }
    u32 val = *(u32*)(sector + byte_offset) & 0x0FFFFFFF;
    kfree(sector);
    return val;
}

static int fat_write_fat_entry(u32 cluster, u32 value) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    u32 byte_offset = fat_offset % bpb.bytes_per_sector;
    u8 *sector = kmalloc(bpb.bytes_per_sector);
    if (!sector) return -1;
    if (read_sectors(fat_sector, 1, sector) != 0) { kfree(sector); return -1; }

    *(u32*)(sector + byte_offset) = (*(u32*)(sector + byte_offset) & 0xF0000000) | (value & 0x0FFFFFFF);

    for (int fat_idx = 0; fat_idx < bpb.num_fats; fat_idx++) {
        u32 fs = fat_sector + fat_idx * bpb.sectors_per_fat_32;
        if (write_sectors(fs, 1, sector) != 0) { kfree(sector); return -1; }
    }
    kfree(sector);
    return 0;
}

static u32 fat_cluster_to_sector(u32 cluster) {
    return first_data_sector + (cluster - 2) * bpb.sectors_per_cluster;
}

static u32 fat_get_next_cluster(u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    u32 byte_offset = fat_offset % bpb.bytes_per_sector;
    u8 *sector = kmalloc(bpb.bytes_per_sector);
    if (!sector) return FAT_END_OF_CHAIN;

    if (read_sectors(fat_sector, 1, sector) != 0) { kfree(sector); return FAT_END_OF_CHAIN; }

    u32 next = *(u32*)(sector + byte_offset) & 0x0FFFFFFF;
    kfree(sector);
    return next;
}

static int fat_read_cluster(u32 cluster, void *buffer) {
    u32 sector = fat_cluster_to_sector(cluster);
    return read_sectors(sector, bpb.sectors_per_cluster, buffer);
}

static u8 parse_filename_char(u8 c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int compare_filename(const u8 *fat_name, const char *name) {
    u8 name_8dot3[12];
    int name_len = 0;
    while (name[name_len] && name[name_len] != '.') name_len++;

    /* Build 8.3 name from input */
    int fi = 0;
    for (int i = 0; i < 8; i++) {
        if (i < name_len) name_8dot3[fi++] = (u8)name[i];
        else name_8dot3[fi++] = ' ';
    }
    name_8dot3[fi++] = '.';
    int ext_len = 0;
    while (name[name_len + 1 + ext_len]) ext_len++;
    for (int i = 0; i < 3; i++) {
        if (i < ext_len) name_8dot3[fi++] = (u8)name[name_len + 1 + i];
        else name_8dot3[fi++] = ' ';
    }

    for (int i = 0; i < 11; i++) {
        if (parse_filename_char(fat_name[i]) != parse_filename_char(name_8dot3[i]))
            return 0;
    }
    return 1;
}

static void dir_name_to_str(const u8 *fat_name, char *out) {
    int oi = 0;
    int trailing = 1;
    for (int i = 9; i >= 0; i--) {
        if (fat_name[i] != ' ') { trailing = 0; break; }
    }
    if (trailing && fat_name[10] == ' ') {
        for (int i = 0; i < 8 && fat_name[i] != ' '; i++)
            out[oi++] = parse_filename_char(fat_name[i]);
        out[oi] = 0;
        return;
    }
    for (int i = 0; i < 8 && fat_name[i] != ' '; i++)
        out[oi++] = parse_filename_char(fat_name[i]);
    if (fat_name[8] != ' ') {
        out[oi++] = '.';
        for (int i = 8; i < 11 && fat_name[i] != ' '; i++)
            out[oi++] = parse_filename_char(fat_name[i]);
    }
    out[oi] = 0;
}

static int fat_init_bpb() {
    /* Read BPB from sector 0 */
    u8 sector[512];
    if (read_sectors(0, 1, sector) != 0) return -1;
    lumie_memcpy(&bpb, sector, sizeof(fat_bpb));

    /* Verify it's FAT32 */
    if (bpb.sectors_per_fat_32 == 0) return -1;

    fat_size = bpb.sectors_per_fat_32 * bpb.bytes_per_sector;
    root_dir_sectors = 0;
    first_data_sector = bpb.reserved_sectors + (bpb.num_fats * bpb.sectors_per_fat_32);
    total_clusters = (bpb.total_sectors_32 - first_data_sector) / bpb.sectors_per_cluster;

    return 0;
}

static int fat_find_cluster(const char *path, fat_dirent *out_ent) {
    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    u8 *sector = kmalloc(cluster_size);
    if (!sector) return 0;

    if (path[0] == '/') path++;

    u32 cluster = bpb.root_cluster;
    int path_len = lumie_strlen(path);

    if (path_len == 0) {
        kfree(sector);
        out_ent->cluster_low = cluster & 0xFFFF;
        out_ent->cluster_high = (cluster >> 16) & 0xFFFF;
        out_ent->attr = FAT_ATTR_DIRECTORY;
        out_ent->size = 0;
        return 1;
    }

    char component[256];
    int ci = 0;

    while (1) {
        while (*path == '/') path++;
        ci = 0;
        while (*path && *path != '/' && ci < 255) {
            component[ci++] = *path++;
        }
        component[ci] = 0;

        if (ci == 0) break;

        int found = 0;
        while (cluster < FAT_END_OF_CHAIN) {
            if (fat_read_cluster(cluster, sector) != 0) break;

            int entries_per_cluster = cluster_size / 32;
            fat_dirent *dent = (fat_dirent*)sector;

            for (int i = 0; i < entries_per_cluster; i++) {
                if (dent[i].name[0] == 0) break;
                if (dent[i].name[0] == 0xE5) continue;
                if ((dent[i].attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;

                if (compare_filename(dent[i].name, component)) {
                    u32 next_cluster = dent[i].cluster_low | (dent[i].cluster_high << 16);
                    lumie_memcpy(out_ent, &dent[i], sizeof(fat_dirent));

                    if (*path == 0) { kfree(sector); return 1; }

                    if (dent[i].attr & FAT_ATTR_DIRECTORY) {
                        cluster = next_cluster;
                        found = 1;
                    } else {
                        kfree(sector);
                        return 0;
                    }
                    break;
                }
            }
            if (found) break;
            cluster = fat_get_next_cluster(cluster);
        }
        if (!found) { kfree(sector); return 0; }
    }

    kfree(sector);
    return 1;
}

int fat_init() {
    efi_status status;

    /* Get loaded image protocol to find device handle */
    efi_guid loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_loaded_image_protocol *loaded_image;
    status = ((efi_bs_handle_protocol)g_bs->HandleProtocol)(g_image, &loaded_image_guid, (void**)&loaded_image);
    if (EFI_ERROR(status)) return -1;

    /* Get block I/O protocol from the loaded image's device */
    efi_guid block_io_guid = EFI_BLOCK_IO_GUID;
    status = ((efi_bs_handle_protocol)g_bs->HandleProtocol)(loaded_image->DeviceHandle, &block_io_guid, (void**)&block_io);
    if (EFI_ERROR(status)) return -1;

    if (fat_init_bpb() != 0) return -1;
    fat_initialized = 1;
    return 0;
}

int fat_read_file(const char *path, void *buffer, u32 max_size) {
    if (!fat_initialized) return -1;
    fat_dirent ent;
    if (!fat_find_cluster(path, &ent)) return -1;
    if (ent.attr & FAT_ATTR_DIRECTORY) return -1;

    u32 cluster = ent.cluster_low | (ent.cluster_high << 16);
    u32 size = ent.size;
    u32 read_size = size < max_size ? size : max_size;
    u32 offset = 0;
    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    u8 *temp = kmalloc(cluster_size);
    if (!temp) return -1;

    while (cluster < FAT_END_OF_CHAIN && offset < read_size) {
        if (fat_read_cluster(cluster, temp) != 0) break;
        u32 to_copy = (read_size - offset) < cluster_size ? (read_size - offset) : cluster_size;
        lumie_memcpy((u8*)buffer + offset, temp, to_copy);
        offset += to_copy;
        cluster = fat_get_next_cluster(cluster);
    }

    kfree(temp);
    return offset;
}

static int fat_create_8dot3_name(const char *name, u8 *fat_name) {
    int name_len = 0;
    while (name[name_len] && name[name_len] != '.') name_len++;
    int ext_len = 0;
    if (name[name_len] == '.') {
        ext_len = 0;
        while (name[name_len + 1 + ext_len]) ext_len++;
    }

    for (int i = 0; i < 8; i++) {
        if (i < name_len) {
            u8 c = (u8)name[i];
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            fat_name[i] = c;
        } else {
            fat_name[i] = ' ';
        }
    }
    for (int i = 0; i < 3; i++) {
        if (i < ext_len) {
            u8 c = (u8)name[name_len + 1 + i];
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            fat_name[8 + i] = c;
        } else {
            fat_name[8 + i] = ' ';
        }
    }
    return 11;
}

/* FIX: The sector_offset calculation was potentially incorrect.
 * When finding a directory entry slot, we need to correctly calculate
 * which sector the entry is in and the offset within that sector.
 * 
 * The formula was: sector_offset = (i * 32) / bytes_per_sector
 * But this doesn't account for multi-sector clusters correctly.
 * 
 * Fixed formula: 
 *   entries_per_sector = bytes_per_sector / 32
 *   sector_in_cluster = (entry_index * 32) / bytes_per_sector
 *   byte_offset_in_sector = (entry_index * 32) % bytes_per_sector
 *   sector = cluster_start + sector_in_cluster
 */
static int fat_find_dir_slot(u32 dir_cluster, const char *name, fat_dirent *out_ent, u32 *out_sector, u32 *out_offset) {
    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    u8 *sector = kmalloc(cluster_size);
    if (!sector) return 0;
    u32 cluster = dir_cluster;
    u32 entries_per_sector = bpb.bytes_per_sector / 32;
    u32 cluster_start = fat_cluster_to_sector(cluster);

    u8 fat_name[11];
    fat_create_8dot3_name(name, fat_name);

    while (cluster < FAT_END_OF_CHAIN) {
        if (fat_read_cluster(cluster, sector) != 0) break;

        int entries_per_cluster = cluster_size / 32;
        fat_dirent *dent = (fat_dirent*)sector;

        for (int i = 0; i < entries_per_cluster; i++) {
            int sector_in_cluster = i / entries_per_sector;
            u32 entry_sector = cluster_start + sector_in_cluster;
            u32 entry_byte_offset = (i % entries_per_sector) * 32;
            
            if (dent[i].name[0] == 0 || dent[i].name[0] == 0xE5) {
                *out_ent = dent[i];
                *out_sector = entry_sector;
                *out_offset = entry_byte_offset;
                kfree(sector);
                return 1;
            }
            if ((dent[i].attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;
            if (lumie_memcmp(dent[i].name, fat_name, 11) == 0) {
                lumie_memcpy(out_ent, &dent[i], sizeof(fat_dirent));
                *out_sector = entry_sector;
                *out_offset = entry_byte_offset;
                kfree(sector);
                return 2;
            }
        }
        cluster = fat_get_next_cluster(cluster);
        cluster_start = fat_cluster_to_sector(cluster);
    }
    kfree(sector);
    return 0;
}

static int fat_extend_directory(u32 dir_cluster) {
    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    u32 fat_entries = fat_size / 4;
    u8 *zero_buf = kmalloc(cluster_size);
    if (!zero_buf) return -1;

    for (u32 i = 2; i < fat_entries; i++) {
        if (fat_read_fat_entry(i) == 0) {
            lumie_memset(zero_buf, 0, cluster_size);
            u32 sector = fat_cluster_to_sector(i);
            if (write_sectors(sector, bpb.sectors_per_cluster, zero_buf) != 0) { kfree(zero_buf); return -1; }
            if (fat_write_fat_entry(i, 0x0FFFFFFF) != 0) { kfree(zero_buf); return -1; }

            u32 c = dir_cluster;
            while (1) {
                u32 next = fat_read_fat_entry(c);
                if (next >= FAT_END_OF_CHAIN) {
                    fat_write_fat_entry(c, i);
                    break;
                }
                c = next;
            }
            kfree(zero_buf);
            return i;
        }
    }
    kfree(zero_buf);
    return -1;
}

int fat_write_file(const char *path, const void *data, u32 size) {
    if (!fat_initialized) return -1;

    /* Parse path into directory and filename */
    char dir_path[256];
    char fname[256];
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        lumie_strcpy(dir_path, "/");
        int fnlen = lumie_strlen(path);
        if (fnlen >= 256) fnlen = 255;
        lumie_memcpy(fname, path, fnlen);
        fname[fnlen] = 0;
    } else {
        int len = last_slash;
        if (len >= 256) len = 255;
        for (int i = 0; i < len; i++) dir_path[i] = path[i];
        dir_path[len] = 0;
        if (len == 0) { dir_path[0] = '/'; dir_path[1] = 0; }
        int fnlen = lumie_strlen(path + last_slash + 1);
        if (fnlen >= 256) fnlen = 255;
        lumie_memcpy(fname, path + last_slash + 1, fnlen);
        fname[fnlen] = 0;
    }

    /* Find parent directory */
    fat_dirent parent_ent;
    u32 parent_cluster;
    if (fat_find_cluster(dir_path, &parent_ent)) {
        if (!(parent_ent.attr & FAT_ATTR_DIRECTORY)) return -1;
        parent_cluster = parent_ent.cluster_low | (parent_ent.cluster_high << 16);
    } else {
        return -1;
    }
    if (parent_cluster == 0) parent_cluster = bpb.root_cluster;

    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    u32 needed = (size + cluster_size - 1) / cluster_size;
    if (needed == 0) needed = 1;
    if (needed > 256) return -1;

    /* Find existing entry or slot for new entry */
    u32 entry_sector = 0, entry_offset = 0;
    fat_dirent entry_buf;
    int slot_type = fat_find_dir_slot(parent_cluster, fname, &entry_buf, &entry_sector, &entry_offset);

    if (slot_type == 0) {
        /* No slot found - try to extend directory */
        u32 new_cluster = fat_extend_directory(parent_cluster);
        if (new_cluster == (u32)-1) return -1;
        /* Retry */
        slot_type = fat_find_dir_slot(parent_cluster, fname, &entry_buf, &entry_sector, &entry_offset);
        if (slot_type == 0) return -1;
    }

    int file_exists = (slot_type == 2);

    /* If file exists and has clusters, free the old chain */
    if (file_exists) {
        u32 old_cluster = entry_buf.cluster_low | (entry_buf.cluster_high << 16);
        if (old_cluster != 0) {
            while (old_cluster < FAT_END_OF_CHAIN) {
                u32 next = fat_read_fat_entry(old_cluster);
                fat_write_fat_entry(old_cluster, 0);
                old_cluster = next;
            }
        }
    }

    /* Allocate new clusters */
    u32 clusters[256];
    u32 found = 0;
    u32 fat_entries = fat_size / 4;
    for (u32 i = 2; i < fat_entries && found < needed; i++) {
        if (fat_read_fat_entry(i) == 0) {
            clusters[found++] = i;
        }
    }
    if (found < needed) return -1;

    /* Link clusters in FAT */
    for (u32 i = 0; i < needed; i++) {
        u32 next = (i < needed - 1) ? clusters[i + 1] : 0x0FFFFFFF;
        if (fat_write_fat_entry(clusters[i], next) != 0) return -1;
    }

    /* Write data to clusters */
    u32 offset = 0;
    u8 *temp = kmalloc(cluster_size);
    if (!temp) return -1;
    for (u32 i = 0; i < needed && offset < size; i++) {
        u32 to_write = size - offset;
        if (to_write > cluster_size) to_write = cluster_size;
        lumie_memcpy(temp, (u8*)data + offset, to_write);
        if (to_write < cluster_size) lumie_memset(temp + to_write, 0, cluster_size - to_write);

        u32 sector = fat_cluster_to_sector(clusters[i]);
        if (write_sectors(sector, bpb.sectors_per_cluster, temp) != 0) { kfree(temp); return -1; }
        offset += to_write;
    }
    kfree(temp);

    /* Read entry sector, modify entry, write back */
    u8 sect[512];
    if (read_sectors(entry_sector, 1, sect) != 0) return -1;

    fat_dirent *entry = (fat_dirent*)(sect + entry_offset);
    lumie_memset(entry->name, ' ', 11);
    fat_create_8dot3_name(fname, entry->name);
    entry->attr = FAT_ATTR_ARCHIVE;
    entry->nt_reserved = 0;
    entry->tenths = 0;
    entry->time_created = 0;
    entry->date_created = 0;
    entry->date_accessed = 0;
    entry->cluster_high = (clusters[0] >> 16) & 0xFFFF;
    entry->time_modified = 0;
    entry->date_modified = 0;
    entry->cluster_low = clusters[0] & 0xFFFF;
    entry->size = size;

    if (write_sectors(entry_sector, 1, sect) != 0) return -1;

    return 0;
}

int fat_list_dir(const char *path, lumie_dirent *entries, int max_entries) {
    if (!fat_initialized) return -1;

    fat_dirent ent;
    if (!fat_find_cluster(path, &ent)) {
        /* Try root */
        if (!path || path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
            /* List root */
            return fat_list_dir("/", entries, max_entries);
        }
        return -1;
    }

    if (!(ent.attr & FAT_ATTR_DIRECTORY)) return -1;

    u32 cluster = ent.cluster_low | (ent.cluster_high << 16);
    if (cluster == 0) cluster = bpb.root_cluster;

    int count = 0;
    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    u8 *sector = kmalloc(cluster_size);
    if (!sector) return -1;

    while (cluster < FAT_END_OF_CHAIN && count < max_entries) {
        if (fat_read_cluster(cluster, sector) != 0) break;

        int entries_per_cluster = cluster_size / 32;
        fat_dirent *dent = (fat_dirent*)sector;

        for (int i = 0; i < entries_per_cluster && count < max_entries; i++) {
            if (dent[i].name[0] == 0) break;
            if (dent[i].name[0] == 0xE5) continue;
            if ((dent[i].attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;

            dir_name_to_str(dent[i].name, entries[count].name);
            entries[count].is_dir = (dent[i].attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
            entries[count].size = dent[i].size;
            count++;
        }
        cluster = fat_get_next_cluster(cluster);
    }

    kfree(sector);
    return count;
}

int fat_exists(const char *path) {
    if (!fat_initialized) return 0;
    fat_dirent ent;
    return fat_find_cluster(path, &ent);
}

int fat_get_file_size(const char *path) {
    if (!fat_initialized) return -1;
    fat_dirent ent;
    if (!fat_find_cluster(path, &ent)) return -1;
    if (ent.attr & FAT_ATTR_DIRECTORY) return -1;
    return ent.size;
}

int fat_delete(const char *path) {
    if (!fat_initialized) return -1;

    /* Find parent directory and the entry */
    char dir_path[256];
    char fname[256];
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        lumie_strcpy(dir_path, "/");
        int fnlen = lumie_strlen(path);
        if (fnlen >= 256) fnlen = 255;
        lumie_memcpy(fname, path, fnlen);
        fname[fnlen] = 0;
    } else {
        int len = last_slash;
        if (len >= 256) len = 255;
        for (int i = 0; i < len; i++) dir_path[i] = path[i];
        dir_path[len] = 0;
        if (len == 0) { dir_path[0] = '/'; dir_path[1] = 0; }
        int fnlen = lumie_strlen(path + last_slash + 1);
        if (fnlen >= 256) fnlen = 255;
        lumie_memcpy(fname, path + last_slash + 1, fnlen);
        fname[fnlen] = 0;
    }

    fat_dirent parent_ent;
    if (!fat_find_cluster(dir_path, &parent_ent)) return -1;
    if (!(parent_ent.attr & FAT_ATTR_DIRECTORY)) return -1;

    u32 parent_cluster = parent_ent.cluster_low | (parent_ent.cluster_high << 16);
    if (parent_cluster == 0) parent_cluster = bpb.root_cluster;

    /* Find the directory entry */
    u32 entry_sector = 0, entry_offset = 0;
    fat_dirent entry_buf;
    int slot_type = fat_find_dir_slot(parent_cluster, fname, &entry_buf, &entry_sector, &entry_offset);
    if (slot_type != 2) return -1;

    /* If directory, check it's empty first */
    if (entry_buf.attr & FAT_ATTR_DIRECTORY) {
        u32 cluster = entry_buf.cluster_low | (entry_buf.cluster_high << 16);
        u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
        u8 *sector = kmalloc(cluster_size);
        if (!sector) return -1;

        int empty = 1;
        while (cluster < FAT_END_OF_CHAIN) {
            if (fat_read_cluster(cluster, sector) != 0) break;
            int entries_per_cluster = cluster_size / 32;
            fat_dirent *dent = (fat_dirent*)sector;
            for (int i = 0; i < entries_per_cluster; i++) {
                if (dent[i].name[0] == 0) break;
                if (dent[i].name[0] == 0xE5) continue;
                if ((dent[i].attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;
                if (dent[i].name[0] == '.' && dent[i].name[1] == ' ') continue;
                if (dent[i].name[0] == '.' && dent[i].name[1] == '.' && dent[i].name[2] == ' ') continue;
                empty = 0;
                break;
            }
            if (!empty) break;
            cluster = fat_get_next_cluster(cluster);
        }
        kfree(sector);
        if (!empty) return -2;
    }

    /* Free cluster chain */
    u32 cluster = entry_buf.cluster_low | (entry_buf.cluster_high << 16);
    if (cluster != 0) {
        while (cluster < FAT_END_OF_CHAIN) {
            u32 next = fat_read_fat_entry(cluster);
            fat_write_fat_entry(cluster, 0);
            cluster = next;
        }
    }

    /* Mark entry as deleted (0xE5) */
    u8 sect[512];
    if (read_sectors(entry_sector, 1, sect) != 0) return -1;
    fat_dirent *entry = (fat_dirent*)(sect + entry_offset);
    entry->name[0] = 0xE5;
    if (write_sectors(entry_sector, 1, sect) != 0) return -1;

    return 0;
}

int fat_mkdir(const char *path) {
    if (!fat_initialized) return -1;

    char dir_path[256];
    char fname[256];
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        lumie_strcpy(dir_path, "/");
        int fnlen = lumie_strlen(path);
        if (fnlen >= 256) fnlen = 255;
        lumie_memcpy(fname, path, fnlen);
        fname[fnlen] = 0;
    } else {
        int len = last_slash;
        if (len >= 256) len = 255;
        for (int i = 0; i < len; i++) dir_path[i] = path[i];
        dir_path[len] = 0;
        if (len == 0) { dir_path[0] = '/'; dir_path[1] = 0; }
        int fnlen = lumie_strlen(path + last_slash + 1);
        if (fnlen >= 256) fnlen = 255;
        lumie_memcpy(fname, path + last_slash + 1, fnlen);
        fname[fnlen] = 0;
    }

    fat_dirent parent_ent;
    u32 parent_cluster;
    if (fat_find_cluster(dir_path, &parent_ent)) {
        if (!(parent_ent.attr & FAT_ATTR_DIRECTORY)) return -1;
        parent_cluster = parent_ent.cluster_low | (parent_ent.cluster_high << 16);
    } else {
        return -1;
    }
    if (parent_cluster == 0) parent_cluster = bpb.root_cluster;

    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;

    /* Allocate a cluster for the directory */
    u32 new_cluster = 0;
    u32 fat_entries = fat_size / 4;
    for (u32 i = 2; i < fat_entries; i++) {
        if (fat_read_fat_entry(i) == 0) {
            new_cluster = i;
            break;
        }
    }
    if (new_cluster == 0) return -1;
    if (fat_write_fat_entry(new_cluster, 0x0FFFFFFF) != 0) return -1;

    /* Zero out the cluster */
    u8 *zero_buf = kmalloc(cluster_size);
    if (!zero_buf) return -1;
    lumie_memset(zero_buf, 0, cluster_size);

    fat_dirent *dot = (fat_dirent*)zero_buf;
    lumie_memset(dot->name, ' ', 11);
    dot->name[0] = '.';
    dot->attr = FAT_ATTR_DIRECTORY;
    dot->cluster_low = new_cluster & 0xFFFF;
    dot->cluster_high = (new_cluster >> 16) & 0xFFFF;

    fat_dirent *dotdot = (fat_dirent*)(zero_buf + 32);
    lumie_memset(dotdot->name, ' ', 11);
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = FAT_ATTR_DIRECTORY;
    dotdot->cluster_low = parent_cluster & 0xFFFF;
    dotdot->cluster_high = (parent_cluster >> 16) & 0xFFFF;

    u32 sector = fat_cluster_to_sector(new_cluster);
    if (write_sectors(sector, bpb.sectors_per_cluster, zero_buf) != 0) { kfree(zero_buf); return -1; }
    kfree(zero_buf);

    /* Find slot in parent directory */
    u32 entry_sector = 0, entry_offset = 0;
    fat_dirent entry_buf;
    int slot_type = fat_find_dir_slot(parent_cluster, fname, &entry_buf, &entry_sector, &entry_offset);
    if (slot_type == 0) {
        u32 ext = fat_extend_directory(parent_cluster);
        if (ext == (u32)-1) return -1;
        slot_type = fat_find_dir_slot(parent_cluster, fname, &entry_buf, &entry_sector, &entry_offset);
        if (slot_type == 0) return -1;
    }

    u8 sect[512];
    if (read_sectors(entry_sector, 1, sect) != 0) return -1;
    fat_dirent *entry = (fat_dirent*)(sect + entry_offset);
    lumie_memset(entry->name, ' ', 11);
    fat_create_8dot3_name(fname, entry->name);
    entry->attr = FAT_ATTR_DIRECTORY;
    entry->cluster_low = new_cluster & 0xFFFF;
    entry->cluster_high = (new_cluster >> 16) & 0xFFFF;
    entry->size = 0;
    if (write_sectors(entry_sector, 1, sect) != 0) return -1;

    return 0;
}

int fat_install_bootloader(void) {
    if (!g_bs) return -1;

    efi_guid loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_loaded_image_protocol *loaded_image;
    efi_status status = ((efi_bs_handle_protocol)g_bs->HandleProtocol)(g_image, &loaded_image_guid, (void**)&loaded_image);
    if (EFI_ERROR(status) || !loaded_image || !loaded_image->ImageBase || loaded_image->ImageSize == 0) return -1;

    u8 *image_base = (u8*)loaded_image->ImageBase;
    u64 image_size_u64 = loaded_image->ImageSize;
    if (image_size_u64 > 0xFFFFFFFF) return -1;
    u32 image_size = (u32)image_size_u64;

    if (!fat_exists("/EFI")) fat_mkdir("/EFI");
    if (!fat_exists("/EFI/BOOT")) fat_mkdir("/EFI/BOOT");
    if (!fat_exists("/EFI/LumieOS")) fat_mkdir("/EFI/LumieOS");

    int current_ok = 0;
    if (fat_write_file("/EFI/BOOT/BOOTX64.EFI", image_base, image_size) == 0) current_ok = 1;
    if (fat_write_file("/EFI/LumieOS/BOOTX64.EFI", image_base, image_size) == 0) current_ok = 1;

    efi_guid fs_guid = EFI_SIMPLE_FILE_SYSTEM_GUID;
    u64 handle_count = 0;
    efi_handle *handles = NULL;
    status = ((efi_bs_locate_handle_buffer)g_bs->LocateHandleBuffer)(
        EFI_LOCATE_BY_PROTOCOL, &fs_guid, NULL, &handle_count, &handles);

    if (!EFI_ERROR(status) && handles && handle_count > 0) {
        for (u64 i = 0; i < handle_count; i++) {
            efi_simple_file_system_protocol *fs;
            status = ((efi_bs_handle_protocol)g_bs->HandleProtocol)(handles[i], &fs_guid, (void**)&fs);
            if (EFI_ERROR(status) || !fs) continue;

            efi_file_protocol *root = NULL;
            status = fs->OpenVolume(fs, (void**)&root);
            if (EFI_ERROR(status) || !root) continue;

            efi_file_protocol *efi_dir = NULL;
            status = root->Open(root, &efi_dir, L"\\EFI", EFI_FILE_MODE_READ, 0);
            if (EFI_ERROR(status) || !efi_dir) {
                status = root->Open(root, &efi_dir, L"\\EFI",
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
                if (EFI_ERROR(status)) { root->Close(root); continue; }
            }

            efi_file_protocol *boot_dir = NULL;
            status = efi_dir->Open(efi_dir, &boot_dir, L"BOOT", EFI_FILE_MODE_READ, 0);
            if (EFI_ERROR(status) || !boot_dir) {
                status = efi_dir->Open(efi_dir, &boot_dir, L"BOOT",
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
                if (EFI_ERROR(status)) { efi_dir->Close(efi_dir); root->Close(root); continue; }
            }

            efi_file_protocol *file = NULL;
            status = boot_dir->Open(boot_dir, &file, L"BOOTX64.EFI",
                EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
            if (!EFI_ERROR(status) && file) {
                u64 write_size = image_size;
                file->Write(file, &write_size, image_base);
                file->Close(file);
            }
            boot_dir->Close(boot_dir);

            efi_file_protocol *lumieos_dir = NULL;
            status = efi_dir->Open(efi_dir, &lumieos_dir, L"LumieOS", EFI_FILE_MODE_READ, 0);
            if (EFI_ERROR(status) || !lumieos_dir) {
                status = efi_dir->Open(efi_dir, &lumieos_dir, L"LumieOS",
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
                if (EFI_ERROR(status)) { efi_dir->Close(efi_dir); root->Close(root); continue; }
            }

            file = NULL;
            status = lumieos_dir->Open(lumieos_dir, &file, L"BOOTX64.EFI",
                EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
            if (!EFI_ERROR(status) && file) {
                u64 write_size = image_size;
                file->Write(file, &write_size, image_base);
                file->Close(file);
            }
            lumieos_dir->Close(lumieos_dir);
            efi_dir->Close(efi_dir);
            root->Close(root);
        }
        ((efi_bs_free_pool)g_bs->FreePool)(handles);
    }

    return current_ok ? 0 : -1;
}

int fat_set_device(efi_handle device_handle) {
    efi_guid block_io_guid = EFI_BLOCK_IO_GUID;
    efi_status status = ((efi_bs_handle_protocol)g_bs->HandleProtocol)(device_handle, &block_io_guid, (void**)&block_io);
    if (EFI_ERROR(status) || !block_io) return -1;
    if (fat_init_bpb() != 0) return -1;
    return 0;
}

void fat_set_bs(efi_boot_services *bs, efi_handle img, efi_system_table *st) {
    g_bs = bs;
    g_image = img;
    g_st = st;
}