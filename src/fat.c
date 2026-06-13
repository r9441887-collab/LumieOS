#include "fat.h"

/* BIOS Parameter Block (FAT32) */
#pragma pack(push, 1)
typedef struct {
    u8   jmp[3];
    u8   oem[8];
    u16  bytes_per_sector;
    u8   sectors_per_cluster;
    u16  reserved_sectors;
    u8   num_fats;
    u16  root_entries;
    u16  total_sectors_16;
    u8   media_descriptor;
    u16  sectors_per_fat_16;
    u16  sectors_per_track;
    u16  num_heads;
    u32  hidden_sectors;
    u32  total_sectors_32;

    /* FAT32 specific */
    u32  sectors_per_fat_32;
    u16  ext_flags;
    u16  fs_version;
    u32  root_cluster;
    u16  fs_info;
    u16  backup_boot_sector;
    u8   reserved[12];
    u8   drive_number;
    u8   reserved1;
    u8   boot_signature;
    u32  volume_id;
    u8   volume_label[11];
    u8   fs_type[8];
} __attribute__((packed)) fat_bpb;

typedef struct {
    u8   name[11];
    u8   attr;
    u8   nt_reserved;
    u8   tenths;
    u16  time_created;
    u16  date_created;
    u16  date_accessed;
    u16  cluster_high;
    u16  time_modified;
    u16  date_modified;
    u16  cluster_low;
    u32  size;
} __attribute__((packed)) fat_dirent;
#pragma pack(pop)

#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

#define FAT_END_OF_CHAIN   0x0FFFFFF8

/* We need disk I/O from UEFI */
static efi_boot_services *g_bs = NULL;
static efi_handle g_image = NULL;
static efi_system_table *g_st = NULL;

/* Disk I/O protocol GUID */
#define EFI_BLOCK_IO_GUID \
    {0x964e5b21, 0x6459, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

#define EFI_DISK_IO_GUID \
    {0xce345171, 0xba0b, 0x11d2, {0x8e,0x4f,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

#define EFI_SIMPLE_FILE_SYSTEM_GUID \
    {0x0964e5b22, 0x6459, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    {0x5B1B4A42, 0x46AE, 0x4D47, {0xA4,0xCD,0xC4,0x0E,0x86,0xD7,0xFB,0x87}}

typedef struct {
    u64 Revision;
    void *Media;
    efi_status (*Reset)(void*, u8);
    efi_status (*ReadBlocks)(void*, u32, u64, u64, void*);
    efi_status (*WriteBlocks)(void*, u32, u64, u64, void*);
    efi_status (*FlushBlocks)(void*);
} efi_block_io_protocol;

typedef struct {
    u32 Revision;
    efi_handle ParentHandle;
    efi_system_table *SystemTable;
    efi_handle DeviceHandle;
    void *FilePath;
    void *Reserved;
    u32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    u64 ImageSize;
    u64 ImageCodeType;
    u64 ImageDataType;
    efi_status (*Unload)(efi_handle);
} efi_loaded_image_protocol;

typedef struct {
    u64 Revision;
    efi_status (*OpenVolume)(void*, void**);
} efi_simple_file_system_protocol;

typedef struct {
    u64 Revision;
    efi_status (*Open)(void*, void**, char16*, u64, u64);
    efi_status (*Close)(void*);
    void *Delete;
    efi_status (*Read)(void*, u64*, void*);
    void *Write;
    void *GetPosition;
    void *SetPosition;
    void *GetInfo;
    void *SetInfo;
    void *Flush;
} efi_file_protocol;

#define EFI_FILE_MODE_READ  1
#define EFI_FILE_MODE_WRITE 2
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

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
    if (!block_io) return -1;
    u32 sector_size = 512;

    efi_status status = block_io->ReadBlocks(block_io, 0, (u64)lba, (u64)count * sector_size, buffer);
    if (EFI_ERROR(status)) return -1;
    return 0;
}

static int write_sectors(u32 lba, u32 count, void *buffer) {
    if (!block_io) return -1;
    efi_status status = block_io->WriteBlocks(block_io, 0, (u64)lba, (u64)count * 512, buffer);
    if (EFI_ERROR(status)) return -1;
    return 0;
}

static u32 fat_read_fat_entry(u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    u32 byte_offset = fat_offset % bpb.bytes_per_sector;
    u8 sector[512];
    if (read_sectors(fat_sector, 1, sector) != 0) return 0xFFFFFFFF;
    return *(u32*)(sector + byte_offset) & 0x0FFFFFFF;
}

static int fat_write_fat_entry(u32 cluster, u32 value) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    u32 byte_offset = fat_offset % bpb.bytes_per_sector;
    u8 sector[512];
    if (read_sectors(fat_sector, 1, sector) != 0) return -1;

    *(u32*)(sector + byte_offset) = (*(u32*)(sector + byte_offset) & 0xF0000000) | (value & 0x0FFFFFFF);

    /* Write to all FAT copies */
    for (int fat_idx = 0; fat_idx < bpb.num_fats; fat_idx++) {
        u32 fs = fat_sector + fat_idx * bpb.sectors_per_fat_32;
        if (write_sectors(fs, 1, sector) != 0) return -1;
    }
    return 0;
}

static u32 fat_cluster_to_sector(u32 cluster) {
    return first_data_sector + (cluster - 2) * bpb.sectors_per_cluster;
}

static u32 fat_get_next_cluster(u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = bpb.reserved_sectors + (fat_offset / bpb.bytes_per_sector);
    u32 byte_offset = fat_offset % bpb.bytes_per_sector;
    u8 sector[512];

    if (read_sectors(fat_sector, 1, sector) != 0) return FAT_END_OF_CHAIN;

    u32 next = *(u32*)(sector + byte_offset) & 0x0FFFFFFF;
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
    root_dir_sectors = ((bpb.root_entries * 32) + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;
    first_data_sector = bpb.reserved_sectors + (bpb.num_fats * bpb.sectors_per_fat_32) + root_dir_sectors;
    total_clusters = (bpb.total_sectors_32 - first_data_sector) / bpb.sectors_per_cluster;

    return 0;
}

static int fat_find_cluster(const char *path, fat_dirent *out_ent) {
    u8 sector[4096];
    if (path[0] == '/') path++;

    /* Start from root */
    u32 cluster = bpb.root_cluster;
    int path_len = lumie_strlen(path);

    if (path_len == 0) {
        out_ent->cluster_low = cluster & 0xFFFF;
        out_ent->cluster_high = (cluster >> 16) & 0xFFFF;
        out_ent->attr = FAT_ATTR_DIRECTORY;
        out_ent->size = 0;
        return 1;
    }

    /* Find the file in directory tree */
    char component[256];
    int ci = 0;

    while (1) {
        /* Extract next path component */
        while (*path == '/') path++;
        ci = 0;
        while (*path && *path != '/') {
            component[ci++] = *path++;
        }
        component[ci] = 0;

        if (ci == 0) break;

        /* Read directory cluster chain */
        int found = 0;
        while (cluster < FAT_END_OF_CHAIN) {
            if (fat_read_cluster(cluster, sector) != 0) break;

            int entries_per_cluster = (bpb.bytes_per_sector * bpb.sectors_per_cluster) / 32;
            fat_dirent *dent = (fat_dirent*)sector;

            for (int i = 0; i < entries_per_cluster; i++) {
                if (dent[i].name[0] == 0) break;
                if (dent[i].name[0] == 0xE5) continue;
                if ((dent[i].attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;

                if (compare_filename(dent[i].name, component)) {
                    u32 next_cluster = dent[i].cluster_low | (dent[i].cluster_high << 16);
                    lumie_memcpy(out_ent, &dent[i], sizeof(fat_dirent));

                    if (*path == 0) return 1;

                    if (dent[i].attr & FAT_ATTR_DIRECTORY) {
                        cluster = next_cluster;
                        found = 1;
                    } else {
                        return 0;
                    }
                    break;
                }
            }
            if (found) break;
            cluster = fat_get_next_cluster(cluster);
        }
        if (!found) return 0;
    }

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
    u8 temp[4096];
    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;

    while (cluster < FAT_END_OF_CHAIN && offset < read_size) {
        if (fat_read_cluster(cluster, temp) != 0) break;
        u32 to_copy = (read_size - offset) < cluster_size ? (read_size - offset) : cluster_size;
        lumie_memcpy((u8*)buffer + offset, temp, to_copy);
        offset += to_copy;
        cluster = fat_get_next_cluster(cluster);
    }

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

static int fat_find_dir_slot(u32 dir_cluster, const char *name, fat_dirent *out_ent, u32 *out_sector, u32 *out_offset) {
    u8 sector[4096];
    u32 cluster = dir_cluster;
    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;

    u8 fat_name[11];
    fat_create_8dot3_name(name, fat_name);

    while (cluster < FAT_END_OF_CHAIN) {
        if (fat_read_cluster(cluster, sector) != 0) break;

        int entries_per_cluster = cluster_size / 32;
        fat_dirent *dent = (fat_dirent*)sector;

        for (int i = 0; i < entries_per_cluster; i++) {
            if (dent[i].name[0] == 0 || dent[i].name[0] == 0xE5) {
                *out_ent = dent[i];
                u32 cluster_start = fat_cluster_to_sector(cluster);
                u32 sector_offset = (i * 32) / bpb.bytes_per_sector;
                *out_sector = cluster_start + sector_offset;
                *out_offset = (i * 32) % bpb.bytes_per_sector;
                return 1;
            }
            if ((dent[i].attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) continue;
            if (lumie_memcmp(dent[i].name, fat_name, 11) == 0) {
                lumie_memcpy(out_ent, &dent[i], sizeof(fat_dirent));
                u32 cluster_start = fat_cluster_to_sector(cluster);
                u32 sector_offset = (i * 32) / bpb.bytes_per_sector;
                *out_sector = cluster_start + sector_offset;
                *out_offset = (i * 32) % bpb.bytes_per_sector;
                return 2;
            }
        }
        cluster = fat_get_next_cluster(cluster);
    }
    return 0;
}

static int fat_extend_directory(u32 dir_cluster) {
    u32 cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    u32 fat_entries = fat_size / 4;

    for (u32 i = 2; i < fat_entries; i++) {
        if (fat_read_fat_entry(i) == 0) {
            u8 zero_buf[4096];
            lumie_memset(zero_buf, 0, cluster_size);
            u32 sector = fat_cluster_to_sector(i);
            if (write_sectors(sector, bpb.sectors_per_cluster, zero_buf) != 0) return -1;
            if (fat_write_fat_entry(i, 0x0FFFFFFF) != 0) return -1;

            /* Link at end of directory chain */
            u32 c = dir_cluster;
            while (1) {
                u32 next = fat_read_fat_entry(c);
                if (next >= FAT_END_OF_CHAIN) {
                    fat_write_fat_entry(c, i);
                    break;
                }
                c = next;
            }
            return i;
        }
    }
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
        lumie_strcpy(fname, path);
    } else {
        int len = last_slash;
        for (int i = 0; i < len; i++) dir_path[i] = path[i];
        dir_path[len] = 0;
        if (len == 0) { dir_path[0] = '/'; dir_path[1] = 0; }
        lumie_strcpy(fname, path + last_slash + 1);
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
    u8 temp[4096];
    for (u32 i = 0; i < needed && offset < size; i++) {
        u32 to_write = size - offset;
        if (to_write > cluster_size) to_write = cluster_size;
        lumie_memcpy(temp, (u8*)data + offset, to_write);
        if (to_write < cluster_size) lumie_memset(temp + to_write, 0, cluster_size - to_write);

        u32 sector = fat_cluster_to_sector(clusters[i]);
        if (write_sectors(sector, bpb.sectors_per_cluster, temp) != 0) return -1;
        offset += to_write;
    }

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
    u8 sector[4096];

    while (cluster < FAT_END_OF_CHAIN && count < max_entries) {
        if (fat_read_cluster(cluster, sector) != 0) break;

        int entries_per_cluster = (bpb.bytes_per_sector * bpb.sectors_per_cluster) / 32;
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

void fat_set_bs(efi_boot_services *bs, efi_handle img, efi_system_table *st) {
    g_bs = bs;
    g_image = img;
    g_st = st;
}
