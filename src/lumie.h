#ifndef __LUMIE_H__
#define __LUMIE_H__

/* lumie.h - LumieOS Standard Library
 * Custom API for the Lumie Operating System
 */

#include "efi.h"

/* ======================== BASIC TYPES ======================== */
typedef u8  byte;
typedef u16 word;
typedef u32 dword;
typedef u64 qword;
typedef usize size_t;
typedef i64  ssize_t;

/* ======================== COLOR ======================== */
typedef enum {
    LUMIE_BLACK       = 0,
    LUMIE_BLUE        = 1,
    LUMIE_GREEN       = 2,
    LUMIE_CYAN        = 3,
    LUMIE_RED         = 4,
    LUMIE_MAGENTA     = 5,
    LUMIE_BROWN       = 6,
    LUMIE_LIGHTGRAY   = 7,
    LUMIE_DARKGRAY    = 8,
    LUMIE_LIGHTBLUE   = 9,
    LUMIE_LIGHTGREEN  = 10,
    LUMIE_LIGHTCYAN   = 11,
    LUMIE_LIGHTRED    = 12,
    LUMIE_LIGHTMAGENTA = 13,
    LUMIE_YELLOW      = 14,
    LUMIE_WHITE       = 15,
} lumie_color;

/* ======================== TERMINAL API ======================== */
void lumie_init(efi_handle image_handle, efi_system_table *system_table);
void lumie_clear(lumie_color bg);
void lumie_set_fg(lumie_color c);
void lumie_set_bg(lumie_color c);
void lumie_set_pos(int x, int y);
void lumie_putchar(char c);
void lumie_write(const char *str);
void lumie_writeln(const char *str);
void lumie_printf(const char *fmt, ...);
int  lumie_getchar();
int  lumie_kbhit();
int  lumie_get_width();
int  lumie_get_height();

/* ======================== MEMORY ======================== */
void *lumie_memset(void *ptr, int val, size_t num);
void *lumie_memcpy(void *dest, const void *src, size_t num);
void *lumie_memmove(void *dest, const void *src, size_t num);
int   lumie_memcmp(const void *p1, const void *p2, size_t num);

/* ======================== STRING ======================== */
size_t lumie_strlen(const char *str);
int    lumie_strcmp(const char *s1, const char *s2);
int    lumie_strncmp(const char *s1, const char *s2, size_t n);
char  *lumie_strcpy(char *dest, const char *src);
char  *lumie_strcat(char *dest, const char *src);
char  *lumie_strchr(const char *str, int ch);
char  *lumie_strstr(const char *haystack, const char *needle);
void   lumie_itoa(i64 num, char *buf, int base);
int    lumie_atoi(const char *str);
int    lumie_snprintf(char *buf, size_t sz, const char *fmt, ...);

/* ======================== FILE SYSTEM ======================== */
typedef struct {
    char name[256];
    u8   is_dir;
    u32  size;
} lumie_dirent;

typedef struct {
    char name[256];
    u32  cluster;
    u32  size;
    u8   is_dir;
    int  valid;
    void *priv;
} lumie_file;

int  lumie_fs_init();
int  lumie_fs_read(const char *path, void *buffer, u32 max_size);
int  lumie_fs_write(const char *path, const void *data, u32 size);
int  lumie_fs_list(const char *path, lumie_dirent *entries, int max_entries);
int  lumie_fs_exists(const char *path);

/* ======================== TEXT EDITOR ======================== */
int  lumie_edit(const char *filename);

/* ======================== SHELL ======================== */
void lumie_shell_run();

/* ======================== SYSTEM ======================== */
void lumie_reboot();
void lumie_shutdown();
void lumie_stall(u64 microseconds);

#endif
