#include "mouse.h"
#include "gop.h"
#include "terminal.h"
#include "kernel.h"
#include "ps2mouse.h"

static efi_simple_pointer_protocol *g_pointer = NULL;
static efi_absolute_pointer_protocol *g_abs_pointer = NULL;
static int mouse_x = 0, mouse_y = 0;
static int mouse_present = 0;
static u32 cursor_bg[16][16];
static int cursor_drawn = 0;
static int cursor_last_x = 0, cursor_last_y = 0;

/* 16x16 blue mouse cursor bitmap (1=blue/white, 0=transparent) */
static const u8 cursor_bits[16][2] = {
    {0x80, 0x00},
    {0xC0, 0x00},
    {0xE0, 0x00},
    {0xF0, 0x00},
    {0xF8, 0x00},
    {0xFC, 0x00},
    {0xFE, 0x00},
    {0xFF, 0x00},
    {0xFF, 0x80},
    {0xFE, 0xC0},
    {0xFC, 0xE0},
    {0xF0, 0x70},
    {0xE0, 0x38},
    {0xC0, 0x1C},
    {0x80, 0x0E},
    {0x00, 0x04},
};

static u32 mouse_blue_fg;
static u32 mouse_white_fg;
static u32 mouse_border;

void mouse_init(efi_system_table *st) {
    efi_boot_services *bs = st->BootServices;
    efi_guid pointer_guid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
    efi_guid abs_guid = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;
    efi_status status;

    mouse_blue_fg = gop_make_color(0x00, 0x55, 0xFF);
    mouse_white_fg = gop_make_color(0xFF, 0xFF, 0xFF);
    mouse_border = gop_make_color(0x00, 0x00, 0x00);

    status = ((efi_bs_locate_protocol)bs->LocateProtocol)(&pointer_guid, NULL, (void**)&g_pointer);
    if (EFI_ERROR(status) || !g_pointer) {
        status = ((efi_bs_locate_protocol)bs->LocateProtocol)(&abs_guid, NULL, (void**)&g_abs_pointer);
        if (EFI_ERROR(status) || !g_abs_pointer) {
            mouse_present = 0;
            return;
        }
    }

    if (g_pointer) {
        typedef efi_status (*ptr_reset_t)(void*, u8);
        ((ptr_reset_t)g_pointer->Reset)(g_pointer, FALSE);
    }
    if (g_abs_pointer) {
        typedef efi_status (*abs_reset_t)(void*, u8);
        ((abs_reset_t)g_abs_pointer->Reset)(g_abs_pointer, FALSE);
    }

    mouse_x = gop_get_width() / 2;
    mouse_y = gop_get_height() / 2;
    mouse_present = 1;

    /* Initialize PS/2 mouse for post-EBS operation */
    ps2mouse_init();
    if (ps2mouse_is_ready()) {
        ps2mouse_set_pos(mouse_x, mouse_y);
    }
}

int mouse_poll(mouse_state *state) {
    if (!mouse_present) return 0;

    /* PS/2 mouse works after ExitBootServices */
    if (ps2mouse_is_ready()) {
        int dx = 0, dy = 0;
        u8 btns = 0;
        if (ps2mouse_poll(&dx, &dy, &btns)) {
            mouse_x += dx;
            mouse_y += dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= (int)gop_get_width()) mouse_x = (int)gop_get_width() - 1;
            if (mouse_y >= (int)gop_get_height()) mouse_y = (int)gop_get_height() - 1;
            if (state) {
                state->x = mouse_x;
                state->y = mouse_y;
                state->dx = dx;
                state->dy = dy;
                state->buttons = btns;
                state->present = 1;
            }
            return 1;
        }
        return 0;
    }

    if (g_pointer) {
        efi_simple_pointer_state ps;
        lumie_memset(&ps, 0, sizeof(ps));
        efi_status status = g_pointer->GetState(g_pointer, &ps);
        if (EFI_ERROR(status)) return 0;

        if (ps.RelativeMovementX == 0 && ps.RelativeMovementY == 0 && ps.Buttons == 0)
            return 0;

        mouse_x += (int)(ps.RelativeMovementX >> 8);
        mouse_y += (int)(ps.RelativeMovementY >> 8);

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= (int)gop_get_width()) mouse_x = (int)gop_get_width() - 1;
        if (mouse_y >= (int)gop_get_height()) mouse_y = (int)gop_get_height() - 1;

        if (state) {
            state->x = mouse_x;
            state->y = mouse_y;
            state->dx = (int)(ps.RelativeMovementX >> 8);
            state->dy = (int)(ps.RelativeMovementY >> 8);
            state->buttons = 0;
            if (ps.Buttons & EFI_SIMPLE_POINTER_LEFT_BUTTON) state->buttons |= MOUSE_LEFT_BUTTON;
            if (ps.Buttons & EFI_SIMPLE_POINTER_RIGHT_BUTTON) state->buttons |= MOUSE_RIGHT_BUTTON;
            if (ps.Buttons & EFI_SIMPLE_POINTER_MIDDLE_BUTTON) state->buttons |= MOUSE_MIDDLE_BUTTON;
            state->present = 1;
        }
        return 1;
    }

    if (g_abs_pointer) {
        return 0;
    }

    return 0;
}

void mouse_draw(int x, int y) {
    if (!mouse_present) return;

    u32 w = gop_get_width();
    u32 h = gop_get_height();

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= (int)w || py < 0 || py >= (int)h) continue;

            cursor_bg[row][col] = gop_get_pixel(px, py);

            u16 bits = ((u16)cursor_bits[row][0] << 8) | cursor_bits[row][1];
            if (bits & (0x8000 >> col)) {
                u32 color = mouse_blue_fg;
                if (row == 0 || col == 0) color = mouse_white_fg;
                gop_put_pixel(px, py, color);
            }
        }
    }
    cursor_drawn = 1;
    cursor_last_x = x;
    cursor_last_y = y;
}

void mouse_restore(int x, int y) {
    if (!mouse_present || !cursor_drawn) return;

    u32 w = gop_get_width();
    u32 h = gop_get_height();

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= (int)w || py < 0 || py >= (int)h) continue;
            gop_put_pixel(px, py, cursor_bg[row][col]);
        }
    }
    cursor_drawn = 0;
}

void mouse_get_pos(int *x, int *y) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
}

void mouse_set_pos(int x, int y) {
    mouse_x = x;
    mouse_y = y;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= (int)gop_get_width()) mouse_x = (int)gop_get_width() - 1;
    if (mouse_y >= (int)gop_get_height()) mouse_y = (int)gop_get_height() - 1;
}
