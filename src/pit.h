#ifndef __PIT_H__
#define __PIT_H__

#include "efi.h"

void pit_init(u32 hz);
void pit_stall(u32 us);
u64 pit_get_ticks(void);

#endif
