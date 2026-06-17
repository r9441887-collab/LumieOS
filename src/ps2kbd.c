#include "ps2kbd.h"
#include "lumie.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64
#define PS2_CMD  0x64

static int g_kbd_ready = 0;
static int g_shift = 0;
static int g_ctrl = 0;
static int g_alt = 0;
static int g_caps = 0;

static int key_available = 0;
static int last_char = 0;

static u8 inb(u16 port) {
    u8 val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void ps2_wait_write(void) {
    for (int i = 0; i < 200000; i++) {
        if (!(inb(PS2_STAT) & 2)) return;
        __asm__ volatile("pause");
    }
}

static void ps2_wait_read(void) {
    for (int i = 0; i < 200000; i++) {
        if (inb(PS2_STAT) & 1) return;
        __asm__ volatile("pause");
    }
}

static int ps2_controller_exists(void) {
    u8 status = inb(PS2_STAT);
    return (status != 0xFF && status != 0);
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

static u8 ps2_cmd_with_data(u8 cmd, u8 data) {
    (void)data;
    ps2_write_cmd(cmd);
    return ps2_read_data();
}

int ps2kbd_init(void) {
    /* Already initialized */
    if (g_kbd_ready) return 0;

    /* Check if PS/2 controller exists */
    if (!ps2_controller_exists()) return -1;

    /* Flush output buffer first */
    for (int i = 0; i < 100; i++) {
        if (!(inb(PS2_STAT) & 1)) break;
        inb(PS2_DATA);
    }

    /* Disable devices */
    ps2_write_cmd(0xAD);
    ps2_write_cmd(0xA7);

    /* Flush output buffer again */
    while ((inb(PS2_STAT) & 1)) inb(PS2_DATA);

    /* Set configuration byte */
    u8 config = ps2_cmd_with_data(0x20, 0);
    config &= ~0x47;
    config |= 0x01;
    ps2_write_cmd(0x60);
    ps2_write_data(config);

    /* Enable keyboard */
    ps2_write_cmd(0xAE);

    /* Reset keyboard */
    ps2_write_data(0xFF);
    u8 ack = ps2_read_data();
    if (ack != 0xFA) return -1;
    /* Read self-test result (0xAA = OK) */
    u8 self_test = ps2_read_data();

    /* Enable scanning */
    ps2_write_data(0xF4);
    ack = ps2_read_data();
    if (ack != 0xFA) return -1;

    /* Set scancode set 2 (default, matches our table) */
    ps2_write_data(0xF0);
    ps2_read_data();
    ps2_write_data(0x02);
    ps2_read_data();

    g_kbd_ready = 1;
    return 0;
}

static int ps2_scancode_to_ascii(u8 key, int shifted) {
    switch (key) {
        case 0x16: return shifted ? '!' : '1';
        case 0x1E: return shifted ? '@' : '2';
        case 0x26: return shifted ? '#' : '3';
        case 0x25: return shifted ? '$' : '4';
        case 0x2E: return shifted ? '%' : '5';
        case 0x36: return shifted ? '^' : '6';
        case 0x3D: return shifted ? '&' : '7';
        case 0x3E: return shifted ? '*' : '8';
        case 0x46: return shifted ? '(' : '9';
        case 0x45: return shifted ? ')' : '0';
        case 0x4E: return shifted ? '_' : '-';
        case 0x55: return shifted ? '+' : '=';
        case 0x15: return shifted ? 'Q' : 'q';
        case 0x1D: return shifted ? 'W' : 'w';
        case 0x24: return shifted ? 'E' : 'e';
        case 0x2D: return shifted ? 'R' : 'r';
        case 0x2C: return shifted ? 'T' : 't';
        case 0x35: return shifted ? 'Y' : 'y';
        case 0x3C: return shifted ? 'U' : 'u';
        case 0x43: return shifted ? 'I' : 'i';
        case 0x44: return shifted ? 'O' : 'o';
        case 0x4D: return shifted ? 'P' : 'p';
        case 0x1C: return shifted ? 'A' : 'a';
        case 0x1B: return shifted ? 'S' : 's';
        case 0x23: return shifted ? 'D' : 'd';
        case 0x2B: return shifted ? 'F' : 'f';
        case 0x34: return shifted ? 'G' : 'g';
        case 0x33: return shifted ? 'H' : 'h';
        case 0x3B: return shifted ? 'J' : 'j';
        case 0x42: return shifted ? 'K' : 'k';
        case 0x4B: return shifted ? 'L' : 'l';
        case 0x1A: return shifted ? 'Z' : 'z';
        case 0x22: return shifted ? 'X' : 'x';
        case 0x21: return shifted ? 'C' : 'c';
        case 0x2A: return shifted ? 'V' : 'v';
        case 0x32: return shifted ? 'B' : 'b';
        case 0x31: return shifted ? 'N' : 'n';
        case 0x3A: return shifted ? 'M' : 'm';
        case 0x41: return shifted ? '<' : ',';
        case 0x49: return shifted ? '>' : '.';
        case 0x4A: return shifted ? '?' : '/';
        case 0x4C: return shifted ? ':' : ';';
        case 0x52: return shifted ? '"' : '\'';
        case 0x54: return shifted ? '{' : '[';
        case 0x5B: return shifted ? '}' : ']';
        case 0x5D: return shifted ? '|' : '\\';
        case 0x0E: return shifted ? '~' : '`';
        case 0x29: return ' ';
        case 0x66: return '\b';
        case 0x5A: return '\n';
        default: return 0;
    }
}

static int ps2_process_scancode(u8 code) {
    /* 
     * Handle multi-byte sequences:
     *   Set 2: make = byte, break = 0xF0 + byte
     *   Extended: 0xE0 + make, 0xE0 + 0xF0 + break
     */
    static int ext = 0;
    static int release = 0;

    if (code == 0xE0) { ext = 1; return 0; }
    if (code == 0xE1) { ext = 2; return 0; }
    if (code == 0xF0) { release = 1; return 0; }

    u8 key = code;
    int pressed = !release;
    release = 0;

    /* Handle extended keys */
    if (ext == 1) {
        ext = 0;
        if (!pressed) return 0;
        /* Extended keys - we only care about press events */
        if (key == 0x1C) return '\n'; /* keypad Enter (main Enter = 0x5A handled below) */
        if (key == 0x4A) return '/'; /* / on keypad */
        if (key == 0x70) return 0xE0 + 0x0B; /* Insert */
        if (key == 0x6C) return 0xE0 + 0x07; /* Home */
        if (key == 0x7D) return 0xE0 + 0x09; /* Page Up */
        if (key == 0x69) return 0xE0 + 0x08; /* End */
        if (key == 0x7A) return 0xE0 + 0x0A; /* Page Down */
        if (key == 0x71) return 0xE0 + 0x06; /* Delete */
        if (key == 0x75) return 0xE0 + 0x01; /* Up */
        if (key == 0x6B) return 0xE0 + 0x03; /* Left */
        if (key == 0x72) return 0xE0 + 0x02; /* Down */
        if (key == 0x74) return 0xE0 + 0x04; /* Right */
        return 0;
    }
    if (ext == 2) {
        ext--;
        return 0;
    }

    /* Modifier keys (handle both press and release) */
    if (key == 0x12) { g_shift = pressed; return 0; } /* LShift */
    if (key == 0x59) { g_shift = pressed; return 0; } /* RShift */
    if (key == 0x14) { g_ctrl = pressed; return 0; }
    if (key == 0x11) { g_alt = pressed; return 0; }
    if (key == 0x58) { if (pressed) g_caps = !g_caps; return 0; }

    if (!pressed) return 0;

    int c = ps2_scancode_to_ascii(key, g_shift);
    if (c >= 'a' && c <= 'z' && g_caps) c -= 32;
    if (c >= 'A' && c <= 'Z' && g_caps && g_shift) c += 32;
    return c;
}

int ps2kbd_getchar(void) {
    while (1) {
        if (key_available) {
            key_available = 0;
            return last_char;
        }
        if (inb(PS2_STAT) & 1) {
            u8 code = inb(PS2_DATA);
            int c = ps2_process_scancode(code);
            if (c) return c;
        } else {
            __asm__ volatile("pause");
        }
    }
}

int ps2kbd_kbhit(void) {
    if (key_available) return 1;
    if (inb(PS2_STAT) & 1) {
        u8 code = inb(PS2_DATA);
        int c = ps2_process_scancode(code);
        if (c) {
            last_char = c;
            key_available = 1;
            return 1;
        }
    }
    return 0;
}

int ps2kbd_getch_noblock(void) {
    if (key_available) {
        key_available = 0;
        return last_char;
    }
    return 0;
}
