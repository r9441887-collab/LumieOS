#ifndef __NET_H__
#define __NET_H__

#include "efi.h"

typedef struct {
    char name[64];
    char https_url[512];
    char http_url[512];
} net_url_entry;

int net_init();
int net_tls_available(void);
int net_download(const char *url, void **out_buf, u64 *out_size);
int net_renet_download(const char *name);

#endif
