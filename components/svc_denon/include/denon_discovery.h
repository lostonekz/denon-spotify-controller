/*
 * SSDP discovery only -- not SSDP->mDNS->manual as NETWORKING.md §1.4 and
 * MODULES.md's denon_discovery.c sketch both specify. Real, explicit
 * deviation from both docs (docs/ROADMAP.md Phase 3's plan spells out the
 * reasoning): no Denon-specific technical reason for mDNS was found in any
 * doc, SSDP already covers Denon, and manual entry (the debug console's
 * `denon host <ip>`, writing SETTING_DENON_HOST) is the ultimate fallback
 * regardless.
 *
 * KNOWN GAP: the exact SSDP Search Target Denon devices respond to
 * ("urn:schemas-denon-com:device...", a specific version, or something
 * else) is not verified against a real unit -- this searches broadly
 * (ST: ssdp:all) and filters responses by a case-insensitive "denon"
 * substring match instead of an exact ST, which is more robust against
 * getting that exact string wrong but is itself unverified. Tier 6
 * (real-hardware verification) is expected to correct this if needed.
 *
 * Target-only -- raw UDP multicast, not host-testable.
 */
#ifndef DENON_DISCOVERY_H
#define DENON_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sends one SSDP M-SEARCH and listens for up to timeout_ms for a response
 * that looks like a Denon device. On a match, writes the responder's IPv4
 * address (dotted-decimal string) into out_host and returns true. false on
 * timeout or any transport error -- the caller (denon_link_fsm, via
 * DENON_LINK_EVT_DISCOVERY_FAILED) treats this as "try again later," not
 * fatal. */
bool denon_discovery_run(char *out_host, size_t out_cap, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* DENON_DISCOVERY_H */
