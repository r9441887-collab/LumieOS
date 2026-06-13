#ifndef __SHELL_H__
#define __SHELL_H__

#include "efi.h"
#include "lumie.h"

void shell_run();
void shell_printf(const char *fmt, ...);

#endif
