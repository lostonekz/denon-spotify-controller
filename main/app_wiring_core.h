/*
 * Core eventbus/topic-policy bootstrap and svc_logging's redact ruleset.
 * Split out of app_wiring.c purely for the 400-line gate (check_limits.py)
 * -- still part of the composition root, not a reusable component; not
 * installed, no other file includes this.
 */
#ifndef APP_WIRING_CORE_H
#define APP_WIRING_CORE_H

#include <stdbool.h>

#include "app_ctx.h"

/* Builds ctx->bus_topic_policies[], calls core_eventbus_init(), and
 * subscribes the two app_wiring-owned relays (TOPIC_SETTINGS for Denon's
 * settings relay, TOPIC_OTA for the OTA/artwork coexistence relay).
 * Requires ctx->bus_lock_sem to already be created -- bus_lock is built
 * from it by the caller (make_real_lock() stays private to app_wiring.c). */
bool app_wiring_create_bus(app_ctx_t *ctx, os_lock_if_t bus_lock);

/* Phase 4 introduces the first real secret-bearing subsystem (Spotify's
 * tokens) -- this is svc_logging_redact.c's mechanism getting its first
 * real ruleset, not just a synthetic one in its own host test. */
bool app_wiring_create_logging(app_ctx_t *ctx);

#endif /* APP_WIRING_CORE_H */
