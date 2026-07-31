/*
 * The telnet transport (docs/adr/0003's primary path): a raw socket to
 * port 23, push events, \r-terminated ASCII, one client device-wide.
 * Target-only glue -- ESP-IDF/lwIP socket dependent, not host-testable.
 * See denon_transport.h for the vtable contract this implements.
 */
#ifndef DENON_TELNET_H
#define DENON_TELNET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "denon_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DENON_TELNET_PORT 23

/* Bytes of not-yet-line-terminated data this can hold across poll() calls.
 * Generous relative to any single Denon status line, with headroom for a
 * few lines arriving in one recv() before the caller drains them. */
#define DENON_TELNET_RXBUF_LEN 128u

typedef struct {
    int      fd;            /* -1 when closed */
    char       rxbuf[DENON_TELNET_RXBUF_LEN];
    size_t       rxlen;        /* bytes currently buffered, not yet consumed */
} denon_telnet_ctx_t;

/* Fills out a ready-to-use denon_transport_if_t bound to ctx. ctx must
 * outlive the interface. */
denon_transport_if_t denon_telnet_transport(denon_telnet_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DENON_TELNET_H */
