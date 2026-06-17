#include "pit.h"
#include "lumie.h"

#define PIT_DATA 0x40
#define PIT_CMD  0x43

static u8 inb(u16 port) {
    u8 val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void pit_init(u32 hz) {
    u32 divisor = 1193182 / hz;
    if (divisor < 2) divisor = 2;
    if (divisor > 65535) divisor = 65535;

    outb(PIT_CMD, 0x36);
    outb(PIT_DATA, (u8)(divisor & 0xFF));
    outb(PIT_DATA, (u8)((divisor >> 8) & 0xFF));
}

void pit_stall(u32 us) {
    if (us == 0) return;

    u64 ticks = (u64)us * 1193182 / 1000000;
    if (ticks > 65535) ticks = 65535;
    if (ticks < 2) ticks = 2;

    outb(PIT_CMD, 0x30);
    outb(PIT_DATA, (u8)(ticks & 0xFF));
    outb(PIT_DATA, (u8)((ticks >> 8) & 0xFF));

    u32 timeout = ticks * 2 + 10000;
    while (timeout--) {
        outb(PIT_CMD, 0xE2);
        u8 lo = inb(PIT_DATA);
        u8 hi = inb(PIT_DATA);
        u16 count = lo | ((u16)hi << 8);
        if (count == 0 || count > (u16)ticks) break;
        __asm__ volatile("pause");
    }
}

u64 pit_get_ticks(void) {
    return 0;
}
