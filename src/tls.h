#ifndef __TLS_H__
#define __TLS_H__

#include "efi.h"

typedef int (*tls_send_fn)(const void *data, u32 len);
typedef int (*tls_recv_fn)(void *buf, u32 *len, u64 timeout_us);

typedef struct {
    tls_send_fn send;
    tls_recv_fn recv;
} tls_stream;

int tls_connect(tls_stream *s, const char *hostname, u16 port);
int tls_send(tls_stream *s, const void *data, u32 len);
int tls_recv(tls_stream *s, void *buf, u32 *len, u64 timeout_us);
void tls_close(tls_stream *s);

#endif
