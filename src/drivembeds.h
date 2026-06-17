#ifndef __DRIVEMBEDS_H__
#define __DRIVEMBEDS_H__
#include "lumie.h"
#define DRV_EMBED_COUNT 19
typedef struct { const char *name; u32 subtype; const u8 *data; u32 size; } drv_embed;
extern const drv_embed drv_embed_table[];
#endif
