#include "ps2mouse.h"
#include "lumie.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64
#define PS2_CMD  0x64

static int g_mouse_ready = 0;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static u8 g_mouse_buttons = 0;
static int g_mouse_packet_index = 0;
static u8 g_mouse_packet[3];

static u8 inb(u16 port) {
    u8 val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void ps2_wait_write(void) {
    for (int i = 0; i < 10000; i++) {
        if (!(inb(PS2_STAT) & 2)) return;
        __asm__ volatile("pause");
    }
}

static void ps2_wait_read(void) {
    for (int i = 0; i < 10000; i++) {
        if (inb(PS2_STAT) & 1) return;
        __asm__ volatile("pause");
    }
}

static u8 ps2_read_data(void) {
    ps2_wait_read();
    return inb(PS2_DATA);
}

static void ps2_write_data(u8 val) {
    ps2_wait_write();
    outb(PS2_DATA, val);
}

static void ps2_write_cmd(u8 cmd) {
    ps2_wait_write();
    outb(PS2_CMD, cmd);
}

int ps2mouse_init(void) {
    /* Enable auxiliary device (mouse) */
    ps2_write_cmd(0xA8);

    /* Flush output buffer */
    while (inb(PS2_STAT) & 1) inb(PS2_DATA);

    /* Set defaults */
    ps2_write_cmd(0xD4);
    ps2_write_data(0xF6);
    if (ps2_read_data() != 0xFA) return -1;

    /* Enable data reporting */
    ps2_write_cmd(0xD4);
    ps2_write_data(0xF4);
    if (ps2_read_data() != 0xFA) return -1;

    g_mouse_ready = 1;
    return 0;
}

int ps2mouse_poll(int *dx, int *dy, u8 *buttons) {
    if (!g_mouse_ready) return 0;

    int moved = 0;
    while (inb(PS2_STAT) & 1) {
        u8 data = inb(PS2_DATA);
        if (g_mouse_packet_index == 0) {
            if (!(data & 0x08)) continue;
        }
        g_mouse_packet[g_mouse_packet_index++] = data;
        if (g_mouse_packet_index >= 3) {
            g_mouse_packet_index = 0;
            int x = (int)(signed char)g_mouse_packet[1];
            int y = -(int)(signed char)g_mouse_packet[2];
            g_mouse_buttons = g_mouse_packet[0] & 0x07;
            if (x || y || g_mouse_buttons) {
                g_mouse_x += x;
                g_mouse_y += y;
                if (dx) *dx = x;
                if (dy) *dy = y;
                if (buttons) *buttons = g_mouse_buttons;
                moved = 1;
            }
        }
    }
    return moved;
}

void ps2mouse_get_pos(int *x, int *y) {
    if (x) *x = g_mouse_x;
    if (y) *y = g_mouse_y;
}

void ps2mouse_set_pos(int x, int y) {
    g_mouse_x = x;
    g_mouse_y = y;
}

int ps2mouse_is_ready(void) {
    return g_mouse_ready;
}
