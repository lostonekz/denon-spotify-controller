/*
 * HTTP fallback transport (docs/adr/0003 point 2/3). Target-only glue --
 * reuses net_http_get() (components/net_http), not host-testable.
 *
 * KNOWN GAP, flagged rather than hidden (same treatment as
 * denon_codec_tables.h's SI/MS tables): Denon's documented state-query
 * endpoint (AppCommand.xml) is commonly implemented as a POST with an XML
 * request body in real-world Denon integrations -- net_http_get() is
 * GET-only (docs/ROADMAP.md Phase 3's plan confirmed this against
 * net_http.h directly), and adding POST support to net_http purely to
 * speculatively match a schema this project cannot currently verify was
 * judged worse than the alternative below. Real-hardware verification
 * (Tier 6, against an actual AVR-X3800H) is expected to correct this.
 *
 * Conservative fallback used instead: formiPhoneAppDirect.xml (already the
 * documented GET-based command endpoint) also accepts a bare 2-letter
 * prefix as a query-only request on several real Denon integrations (e.g.
 * "?MV" returning the current volume rather than setting anything) --
 * poll() rotates through MV/PW/MU/SI/MS, one field per call, reusing the
 * exact same GET endpoint send_cmd() already uses. This means any single
 * field can lag up to ~10s (5 fields x the 2s cadence) rather than the 2s
 * NETWORKING.md §1.4 states for AppCommand.xml specifically -- a documented,
 * deliberate deviation, not silently different from the doc.
 */
#ifndef DENON_HTTP_H
#define DENON_HTTP_H

#include <stdbool.h>
#include <stdint.h>

#include "net_http.h"

#include "denon_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DENON_HTTP_PORT 8080

typedef struct {
    net_http_t     *http;    /* injected, shared with the rest of the app -- see MEMORY.md §4's TLS arbiter */
    http_policy_t     policy;
    char                host[64];
    int                   poll_field_index;      /* rotates 0..4 across MV/PW/MU/SI/MS */
    int64_t                 next_poll_due_us;
} denon_http_ctx_t;

/* Fills out a ready-to-use denon_transport_if_t bound to ctx. ctx must
 * outlive the interface. http/policy are stored by value/pointer at
 * construction, before open() is ever called. */
denon_transport_if_t denon_http_transport(denon_http_ctx_t *ctx, net_http_t *http,
                                              const http_policy_t *policy);

/* The capability probe (NETWORKING.md §1.4's Deviceinfo.xml) -- always an
 * HTTP GET regardless of which transport currently carries commands/
 * events, called directly by denon_service.c's probe_capabilities action,
 * not through the denon_transport_if_t vtable (which represents "the
 * currently active command/event transport," not "any HTTP capability the
 * service happens to use"). Returns false on any failure; the caller
 * treats a failed probe as informational, not fatal (RISKS.md R5). */
bool denon_http_probe_capabilities(net_http_t *http, const http_policy_t *policy, const char *host);

#ifdef __cplusplus
}
#endif

#endif /* DENON_HTTP_H */
