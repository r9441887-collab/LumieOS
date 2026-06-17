#include "lumie.h"
#include "kernel.h"

void *lumie_memset(void *ptr, int val, size_t num) {
    u8 *p = (u8*)ptr;
    for (size_t i = 0; i < num; i++) p[i] = (u8)val;
    return ptr;
}

void *lumie_memcpy(void *dest, const void *src, size_t num) {
    u8 *d = (u8*)dest;
    const u8 *s = (const u8*)src;
    for (size_t i = 0; i < num; i++) d[i] = s[i];
    return dest;
}

void *lumie_memmove(void *dest, const void *src, size_t num) {
    u8 *d = (u8*)dest;
    const u8 *s = (const u8*)src;
    if (d < s) { for (size_t i = 0; i < num; i++) d[i] = s[i]; }
    else { for (size_t i = num; i > 0; i--) d[i-1] = s[i-1]; }
    return dest;
}

int lumie_memcmp(const void *p1, const void *p2, size_t num) {
    const u8 *a = (const u8*)p1;
    const u8 *b = (const u8*)p2;
    for (size_t i = 0; i < num; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

size_t lumie_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int lumie_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(u8*)s1 - *(u8*)s2;
}

int lumie_strncmp(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(u8*)s1 - *(u8*)s2;
}

char *lumie_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *lumie_strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *lumie_strchr(const char *str, int ch) {
    while (*str) {
        if (*str == (char)ch) return (char*)str;
        str++;
    }
    return NULL;
}

void lumie_itoa(i64 num, char *buf, int base) {
    char digits[] = "0123456789abcdef";
    char tmp[65];
    int i = 0;
    int neg = 0;

    if (num == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }

    if (num < 0 && base == 10) {
        neg = 1;
        if (num == (-9223372036854775807LL - 1)) {
            lumie_strcpy(buf, "-9223372036854775808");
            return;
        }
        num = -num;
    }

    while (num > 0) {
        tmp[i++] = digits[num % base];
        num /= base;
    }

    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

int lumie_atoi(const char *str) {
    int result = 0;
    int neg = 0;

    while (*str == ' ') str++;
    if (*str == '-') { neg = 1; str++; }
    else if (*str == '+') str++;

    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return neg ? -result : result;
}

char *lumie_strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

int lumie_snprintf(char *buf, size_t sz, const char *fmt, ...) {
    int pos = 0;
    va_list args;
    va_start(args, fmt);
    for (int i = 0; fmt[i] && pos < (int)sz - 1; i++) {
        if (fmt[i] != '%') { buf[pos++] = fmt[i]; continue; }
        i++;
        if (!fmt[i]) break;
        switch (fmt[i]) {
            case 's': {
                const char *s = va_arg(args, const char*);
                while (*s && pos < (int)sz - 1) buf[pos++] = *s++;
                break;
            }
            case 'd': {
                int val = va_arg(args, int);
                char tmp[32];
                lumie_itoa(val, tmp, 10);
                for (int j = 0; tmp[j] && pos < (int)sz - 1; j++) buf[pos++] = tmp[j];
                break;
            }
            case 'u': {
                u32 val = va_arg(args, u32);
                char tmp[32];
                lumie_itoa((i64)val, tmp, 10);
                for (int j = 0; tmp[j] && pos < (int)sz - 1; j++) buf[pos++] = tmp[j];
                break;
            }
            case 'x':
            case 'X': {
                u32 val = va_arg(args, u32);
                char tmp[32];
                lumie_itoa((i64)val, tmp, 16);
                for (int j = 0; tmp[j] && pos < (int)sz - 1; j++) buf[pos++] = tmp[j];
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                buf[pos++] = c;
                break;
            }
            case '%':
                buf[pos++] = '%';
                break;
            default:
                buf[pos++] = '%';
                buf[pos++] = fmt[i];
                break;
        }
    }
    va_end(args);
    buf[pos] = 0;
    return pos;
}

u32 lumie_xor32(const u8 *data, u32 len) {
    u32 x = 0;
    for (u32 i = 0; i < len; i++) x ^= (u32)data[i] << ((i & 3) * 8);
    return x;
}

/* Pack buffer into .lkrn/.ldrv format (allocates via pool) */
int lumie_pack_module(const void *data, u32 data_sz, u32 magic, u32 subtype, const char *name, u8 **out, u32 *out_sz) {
    u32 total = LUMIE_HDR_SIZE + data_sz;
    u8 *buf = NULL;
    efi_status st = ((efi_bs_allocate_pool)g_BS->AllocatePool)(4, total, (void**)&buf);
    if (EFI_ERROR(st) || !buf) return -1;

    lumie_mod_header *hdr = (lumie_mod_header*)buf;
    lumie_memset(hdr, 0, sizeof(lumie_mod_header));
    hdr->magic = magic;
    hdr->ver_major = 0;
    hdr->ver_minor = 1;
    hdr->hdr_size = LUMIE_HDR_SIZE;
    hdr->data_size = data_sz;
    hdr->data_off = LUMIE_HDR_SIZE;
    hdr->subtype = subtype;
    if (name) {
        int nl = lumie_strlen(name);
        if (nl > 31) nl = 31;
        lumie_memcpy(hdr->name, name, nl);
    }

    /* Copy payload */
    lumie_memcpy(buf + LUMIE_HDR_SIZE, data, data_sz);

    /* Calculate XOR32 checksum (with checksum field = 0) */
    hdr->checksum = lumie_xor32(buf, total);

    *out = buf;
    *out_sz = total;
    return 0;
}

/* lumie_stall is implemented in kernel.c */
