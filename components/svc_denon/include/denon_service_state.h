/*
 * The software state that must correctly reset across a svc_denon
 * restart: the link FSM, the command queue, cached settings, and the
 * link-driver's own bookkeeping (denon_service_link.c's *_just_ran flags,
 * backoff/promotion deadlines). Deliberately does NOT include anything
 * that owns an OS or network resource (sockets, the cmdq mutex, the denon
 * task) -- those live in svc_denon_t (denon_service.h) instead.
 *
 * WHY THE SPLIT: svc_denon_t necessarily embeds denon_http_ctx_t, which
 * embeds a net_http_t* -- and net_http_t (components/net_http) is an
 * anonymous-struct typedef around a real SemaphoreHandle_t, so anything
 * that so much as forward-references it pulls in FreeRTOS headers that
 * test/host's harness cannot see at all (a structural property of that
 * harness, not a Phase 3 gap -- confirmed by every other svc_*.c in this
 * codebase, e.g. svc_ota.c/net_wifi.c, also never being host-tested
 * directly for exactly this reason). This file is the piece that CAN be
 * -- and is -- host-tested (test/host/test_denon_service_state.c),
 * capturing exactly the property a restartability test cares about: does
 * every pure field return to the same baseline across repeated
 * init->reset->init cycles. The OS-resource-owning wrapper around it is
 * verified only by a real target build and real hardware, the same as
 * every other service in this codebase.
 */
#ifndef DENON_SERVICE_STATE_H
#define DENON_SERVICE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"
#include "core_fsm.h"

#include "denon_cmdq.h"
#include "denon_link_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Deliberately NOT named denon_link_transport_pref_t: svc_settings.h already
 * defines that exact name (Phase 1, for SETTING_DENON_TRANSPORT_PREF's
 * stored representation) with the same 3 values -- svc_denon cannot
 * #include svc_settings.h to reuse it (no svc->svc edge,
 * tools/arch_rules.json), and a second, different typedef of the same
 * name would collide the moment a single translation unit (app_wiring.c)
 * includes both headers. app_wiring.c does the small explicit conversion
 * when relaying the setting into svc_denon_set_transport_pref(). */
typedef enum {
    DENON_LINK_TRANSPORT_TELNET = 0,
    DENON_LINK_TRANSPORT_HTTP,
    DENON_LINK_TRANSPORT_AUTO,
} denon_link_transport_pref_t;

typedef struct {
    char                           initial_host[64];   /* SETTING_DENON_HOST snapshot; may be empty */
    denon_link_transport_pref_t           transport_pref;      /* SETTING_DENON_TRANSPORT_PREF snapshot */
    uint16_t                           volume_limit_half_db;   /* SETTING_VOLUME_LIMIT_MAX snapshot */

    /* Nullable, matches core_fsm's own contract -- NULL in host tests,
     * a real ESP_LOGI-based logger from app_wiring in the target build. */
    fsm_transition_log_fn link_fsm_log;
    void                    *link_fsm_log_ctx;
} denon_service_state_cfg_t;

typedef struct {
    denon_link_fsm_t  link_fsm;
    denon_cmdq_t        cmdq;

    char                  host[64];
    denon_link_transport_pref_t transport_pref;
    uint16_t                  volume_limit_half_db;

    denon_state_t local_state;   /* mirror of the last value written to core_state -- delta detection for events */

    /* Real bug, found live: this project's whole design (ADR-0003) assumed
     * the AVR proactively broadcasts every state change to every connected
     * telnet client -- verified true on real hardware in Phase 3. Real-
     * hardware testing later in the same session found a live counter-
     * example: a long-idle connection that kept sending commands
     * successfully (the AVR's volume/power genuinely changed) while never
     * receiving a single broadcast line back, for the connection's entire
     * ~23-minute lifetime -- confirmed by direct inspection of denon_
     * telnet.c's poll() (correct, no client-side receive bug) and the link
     * FSM (never transitioned away from CONNECTED_TELNET, so nothing
     * detected this as a dead link either). Whatever the AVR-side or
     * network-side cause, this codebase had no fallback: broadcast-only,
     * no active query. next_status_poll_due_us (denon_service_io.c) tracks
     * when to next actively ask ("PW?"/"MV?"/"MU?"/"SI?") rather than
     * waiting indefinitely on a broadcast that may never come -- reset
     * whenever a real line actually arrives, so this stays quiet while
     * broadcasts are flowing normally and only kicks in when they aren't.
     * Zero-initialized by memset in _init()/_reset() below -- 0 means
     * "due immediately," so a fresh/restarted link queries right away
     * rather than waiting a full interval for its first real state. */
    int64_t next_status_poll_due_us;

    /* Real-world research (community reports of this exact symptom on
     * Denon/Marantz's shared HEOS control platform -- their own mobile
     * apps affected too, not something specific to this codebase) confirms
     * the active-poll queries above can go unanswered indefinitely: the
     * receiver's control service can wedge in a way a client-side query
     * alone can't unstick -- the documented, working recovery is a real
     * reconnect, not just asking again on the same socket. Counts
     * consecutive DENON_STATUS_POLL_INTERVAL_US rounds with zero real
     * lines received; once it crosses DENON_SILENT_POLL_ROUNDS_BEFORE_
     * RECONNECT (denon_service_io.c), that file dispatches DENON_LINK_EVT_
     * TELNET_EOF itself -- the same event a real detected socket failure
     * already uses, falling back to the already-proven HTTP transport
     * immediately (denon_link_fsm.c's own DENON_LINK_EVT_PROMOTE_DUE
     * mechanism retries telnet again ~60s later on its own, no new code
     * needed for that half). Reset to 0 alongside next_status_poll_due_us
     * whenever a real line arrives. Zero-initialized by memset, matching
     * that field. */
    int consecutive_silent_poll_rounds;

    /* Cross-boundary results from denon_service_link.c's actions, and its
     * own backoff/promotion timers -- see that file's top comment for why
     * an action cannot safely dispatch back into the FSM itself. */
    bool discovery_just_ran;   bool discovery_ok;
    bool telnet_open_just_ran; bool telnet_open_ok;
    bool http_open_just_ran;   bool http_open_ok;
    int64_t backoff_deadline_us;
    int64_t promote_deadline_us;

    /* Set by svc_denon_set_host() (may run on a different task than the
     * denon task); the drive loop notices it -- see that function's own
     * comment in denon_service.c. */
    volatile bool host_changed_pending;
} denon_service_state_t;

bool denon_service_state_init(denon_service_state_t *s, const denon_service_state_cfg_t *cfg);
void denon_service_state_reset(denon_service_state_t *s);

#ifdef __cplusplus
}
#endif

#endif /* DENON_SERVICE_STATE_H */
