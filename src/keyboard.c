#include "keyboard.h"
#include "lumie.h"
#include "ps2kbd.h"

/* Keyboard mode: 0 = UEFI ConIn, 1 = PS/2, 2 = dead */
static int kbd_mode = 2;

/* UEFI keyboard state (only valid in mode 0) */
static efi_simple_text_input_protocol *con_in = NULL;
static efi_simple_text_input_ex_protocol *con_in_ex = NULL;
static efi_boot_services *g_bs_kbd = NULL;

static int key_available = 0;
static int last_char = 0;

static char map_unicode_to_ascii(char16 c) {
    if (c == 0x08) return '\b';
    if (c == 0x09) return '\t';
    if (c == 0x0D) return '\n';
    if (c == 0x1B) return '\x1b';
    if (c >= 0x01 && c <= 0x7e) return (char)c;
    return 0;
}

void kbd_init(efi_system_table *st) {
    if (!st || !st->ConIn || !st->BootServices) return;
    con_in = st->ConIn;
    g_bs_kbd = st->BootServices;
    con_in_ex = NULL;
    kbd_mode = 0;

    efi_guid ex_guid = EFI_SIMPLE_TEXT_INPUT_EX_GUID;
    efi_status status = ((efi_bs_locate_protocol)g_bs_kbd->LocateProtocol)(&ex_guid, NULL, (void**)&con_in_ex);
    if (EFI_ERROR(status)) {
        con_in_ex = NULL;
    }

    /* Also try to initialize PS/2 (may fail, that's OK) */
    if (ps2kbd_init() == 0) {
        /* PS/2 is available, but we stay in UEFI mode until ExitBootServices */
    }
}

void kbd_switch_to_ps2(void) {
    /* Try to init PS/2 (if not already initialized) */
    if (ps2kbd_init() == 0) {
        kbd_mode = 1;
        key_available = 0;
        last_char = 0;
    } else {
        kbd_mode = 2;
    }
}

static int read_key(efi_input_key *key) {
    if (!con_in) return 0;
    if (con_in_ex) {
        efi_input_key_ex key_ex;
        lumie_memset(&key_ex, 0, sizeof(key_ex));
        efi_status status = con_in_ex->ReadKeyStrokeEx(con_in_ex, &key_ex);
        if (!EFI_ERROR(status)) {
            key->UnicodeChar = key_ex.UnicodeChar;
            key->ScanCode = key_ex.ScanCode;
            return 1;
        }
    }
    return con_in->ReadKeyStroke(con_in, key) == EFI_SUCCESS;
}

static int process_key(int unicode, int scan) {
    char c = map_unicode_to_ascii(unicode);
    if (c) return c;
    if (scan) {
        switch (scan) {
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
        }
    }
    return 0;
}

int kbd_getchar(void) {
    if (kbd_mode == 1) {
        return ps2kbd_getchar();
    }
    if (kbd_mode != 0) return 0;

    efi_input_key key;
    if (!con_in || !g_bs_kbd) return 0;

    while (1) {
        if (read_key(&key)) {
            int c = process_key(key.UnicodeChar, key.ScanCode);
            if (c) return c;
        } else {
            efi_event wait_event = con_in->WaitForKey;
            if (wait_event) {
                u64 index;
                ((efi_bs_wait_for_event)g_bs_kbd->WaitForEvent)(1, &wait_event, &index);
            } else {
                ((efi_bs_stall)g_bs_kbd->Stall)(1000);
            }
        }
    }
}

int kbd_kbhit(void) {
    if (key_available) return 1;

    if (kbd_mode == 1) {
        return ps2kbd_kbhit();
    }
    if (kbd_mode != 0) return 0;

    efi_input_key key;
    if (!con_in) return 0;

    if (read_key(&key)) {
        int c = process_key(key.UnicodeChar, key.ScanCode);
        if (c) {
            last_char = c;
            key_available = 1;
            return 1;
        }
    }
    return 0;
}

int kbd_getch_noblock(void) {
    if (key_available) {
        key_available = 0;
        return last_char;
    }

    if (kbd_mode == 1) {
        return ps2kbd_getch_noblock();
    }
    if (kbd_mode != 0) return 0;

    return 0;
}

void kbd_flush(void) {
    key_available = 0;
    last_char = 0;

    if (kbd_mode == 0 && con_in) {
        efi_input_key key;
        while (con_in->ReadKeyStroke(con_in, &key) == EFI_SUCCESS);
        if (con_in_ex) {
            efi_input_key_ex key_ex;
            while (con_in_ex->ReadKeyStrokeEx(con_in_ex, &key_ex) == EFI_SUCCESS);
        }
    }
}
