#ifndef __GOP_H__
#define __GOP_H__

#include "efi.h"

typedef struct {
    u64 base;
    u64 size;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
    u32 pixel_format;
} fb_info;

efi_status gop_init(efi_handle image_handle, efi_system_table *st);
void gop_put_pixel(u32 x, u32 y, u32 color);
u32 gop_get_pixel(u32 x, u32 y);
void gop_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void gop_draw_char(u32 x, u32 y, u32 fg, u32 bg, char c);
void gop_draw_string(u32 x, u32 y, u32 fg, u32 bg, const char *str);
u32 gop_make_color(u8 r, u8 g, u8 b);
fb_info *gop_get_fb();
u32 gop_get_width();
u32 gop_get_height();

#endif
