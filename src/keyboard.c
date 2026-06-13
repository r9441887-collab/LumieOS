#include "keyboard.h"

static efi_simple_text_input_protocol *con_in = NULL;
static efi_boot_services *g_bs = NULL;
static int key_available = 0;
static int last_char = 0;

static char map_unicode_to_ascii(char16 c) {
    if (c >= 0x20 && c <= 0x7e) return (char)c;
    if (c == 0x08) return '\b';
    if (c == 0x09) return '\t';
    if (c == 0x0D) return '\n';
    if (c == 0x1B) return '\x1b';
    return 0;
}

void kbd_init(efi_system_table *st) {
    con_in = st->ConIn;
    g_bs = st->BootServices;
}

int kbd_getchar() {
    efi_input_key key;
    efi_status status;

    while (1) {
        status = con_in->ReadKeyStroke(con_in, &key);
        if (!EFI_ERROR(status)) {
            char c = map_unicode_to_ascii(key.UnicodeChar);
            if (c) return c;
            if (key.ScanCode) {
                /* Map scan codes */
                switch (key.ScanCode) {
                    case 0x01: return KBD_UP;
                    case 0x02: return KBD_DOWN;
                    case 0x03: return KBD_LEFT;
                    case 0x04: return KBD_RIGHT;
                    case 0x05: return KBD_ESC;
                    case 0x06: return KBD_DEL;
                    case 0x07: return KBD_HOME;
                    case 0x08: return KBD_END;
                    case 0x09: return KBD_PGUP;
                    case 0x0A: return KBD_PGDN;
                    case 0x0B: return KBD_INS;
                    default: return 0;
                }
            }
        }
        /* Stall a bit to avoid busy-looping */
        ((efi_bs_stall)g_bs->Stall)(1000);
    }
}

int kbd_kbhit() {
    efi_input_key key;
    efi_status status;

    status = con_in->ReadKeyStroke(con_in, &key);
    if (!EFI_ERROR(status)) {
        last_char = map_unicode_to_ascii(key.UnicodeChar);
        if (last_char == 0 && key.ScanCode) {
            switch (key.ScanCode) {
                case 0x01: last_char = KBD_UP; break;
                case 0x02: last_char = KBD_DOWN; break;
                case 0x03: last_char = KBD_LEFT; break;
                case 0x04: last_char = KBD_RIGHT; break;
                case 0x05: last_char = KBD_ESC; break;
                case 0x06: last_char = KBD_DEL; break;
                case 0x07: last_char = KBD_HOME; break;
                case 0x08: last_char = KBD_END; break;
                case 0x09: last_char = KBD_PGUP; break;
                case 0x0A: last_char = KBD_PGDN; break;
                case 0x0B: last_char = KBD_INS; break;
                default: last_char = 0; break;
            }
        }
        if (last_char) {
            key_available = 1;
            return 1;
        }
    }
    return 0;
}

int kbd_getch_noblock() {
    if (key_available) {
        key_available = 0;
        return last_char;
    }
    return 0;
}

void kbd_flush() {
    efi_input_key key;
    while (con_in->ReadKeyStroke(con_in, &key) == EFI_SUCCESS);
    key_available = 0;
}
