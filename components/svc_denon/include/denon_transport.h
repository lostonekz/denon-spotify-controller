/*
 * The injected transport interface. docs/adr/0003: "one injected interface
 * denon_transport_if, automatic selection" -- telnet and HTTP are two
 * genuinely different I/O models (telnet pushes unsolicited lines; HTTP is
 * polled), unified behind one vtable so denon_service.c's task loop calls
 * poll() the same way regardless of which transport denon_link_fsm has
 * currently selected.
 *
 * poll()'s timeout_ms means something different per transport, by design:
 * telnet blocks in select() up to timeout_ms waiting for a pushed line;
 * HTTP either performs its due 2s-interval AppCommand.xml GET (returning
 * one synthetic line now, buffering any others for subsequent poll() calls
 * without a new GET) or returns POLL_NOTHING immediately without blocking.
 * denon_service.c calls poll() with a short, fixed timeout (~250ms) --
 * short enough to keep denon_cmdq's 100ms rate-limited drain responsive;
 * telnet's poll() simply returns emptier more often than a telnet-only
 * design would, an accepted tradeoff for sharing one loop shape.
 *
 * send_cmd()/poll() carry raw wire command text ("MV525", "PWON", "SICD"),
 * never a parsed value -- neither transport implementation needs to know
 * what MV/PW/SI mean. denon_service.c is the one place that feeds raw
 * lines through denon_codec.h/denon_codec_tables.h.
 */
#ifndef DENON_TRANSPORT_H
#define DENON_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest raw wire line either transport ever produces or consumes.
 * DENON_TABLE_BUF_LEN (denon_codec_tables.h) is 24; round up for headroom
 * without this header needing to include that one (transports don't need
 * to know the protocol's own buffer math, just move text). */
#define DENON_TRANSPORT_LINE_BUF_LEN 32u

/* A plain bool return for poll() can't distinguish "nothing happened yet,
 * still fine" from "the connection just died" -- and denon_service.c needs
 * that distinction to know when to dispatch TELNET_EOF/HTTP_FAILED. Same
 * reasoning as fsm_result_t's explicit FSM_NO_TRANSITION elsewhere in this
 * codebase: don't conflate two different outcomes into one boolean. */
typedef enum {
    DENON_TRANSPORT_POLL_NOTHING = 0,   /* nothing arrived/due this call -- connection still fine */
    DENON_TRANSPORT_POLL_LINE,          /* out_line has a fresh raw command line */
    DENON_TRANSPORT_POLL_LOST,          /* connection died (EOF/error/HTTP failure) -- caller should close() */
} denon_transport_poll_result_t;

typedef struct {
    bool (*open)(void *ctx, const char *host);
    bool (*send_cmd)(void *ctx, const char *cmd);
    denon_transport_poll_result_t (*poll)(void *ctx, char *out_line, size_t out_cap, uint32_t timeout_ms);
    void (*close)(void *ctx);
    void *ctx;
} denon_transport_if_t;

#ifdef __cplusplus
}
#endif

#endif /* DENON_TRANSPORT_H */
