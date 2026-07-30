/*
 * Table-driven FSM engine. docs/EVENTS_AND_STATE.md §6.4.
 *
 * (state, event) -> (action, next_state). Anything with more than three
 * states gets one of these: net_wifi, spotify_session, denon_link, ota,
 * power, provisioning. Knows nothing about any specific domain (core_fsm
 * cannot include a service header -- ARCHITECTURE.md §1.2) and cannot call
 * ESP_LOGx directly (core may only use FreeRTOS/esp_err, verified against
 * tools/check_includes.py), so transition logging is an injected callback;
 * the svc_* caller that owns a real FSM instance supplies a real logger and
 * produces the documented "[fsm:denon_link] CONNECTED --(EVT_SOCKET_EOF)-->
 * BACKOFF" line.
 *
 * A plain caller-owned struct, not an opaque handle: an FSM instance holds
 * nothing but a table pointer and the current state, no resources to
 * release, so there's nothing create()/destroy() would buy over init() --
 * same reasoning as core_pool's plain structs.
 */
#ifndef CORE_FSM_H
#define CORE_FSM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FSM_OK = 0,
    FSM_NO_TRANSITION,   /* no table entry for (current_state, event) --
                          * explicit, not silently ignored: consistent with
                          * this project's aversion to silent failure modes
                          * elsewhere (domain_meta_t staleness, the OTA
                          * gate's "never silent revert"). */
} fsm_result_t;

typedef void (*fsm_action_fn)(void *action_ctx, uint32_t event, const void *event_data);

typedef struct {
    uint32_t       state;
    uint32_t       event;
    fsm_action_fn  action;      /* nullable -- some transitions are a pure state change */
    uint32_t       next_state;
} fsm_transition_t;

typedef struct {
    const fsm_transition_t *transitions;
    size_t                  count;
} fsm_table_t;

typedef void (*fsm_transition_log_fn)(void *log_ctx, const char *fsm_name,
                                      uint32_t from_state, uint32_t event,
                                      uint32_t to_state);

typedef struct {
    const char             *name;             /* for the log line, e.g. "denon_link" */
    const fsm_table_t      *table;
    uint32_t                initial_state;
    fsm_transition_log_fn   on_transition;    /* nullable */
    void                   *log_ctx;
    void                   *action_ctx;       /* passed to every action_fn */
} fsm_cfg_t;

typedef struct {
    fsm_cfg_t cfg;
    uint32_t  current_state;
} core_fsm_t;

void core_fsm_init(core_fsm_t *fsm, const fsm_cfg_t *cfg);

/* Looks up (current_state, event) in the table. FSM_NO_TRANSITION and no
 * state change if absent. On a match: runs the action (if any) BEFORE
 * updating current_state, then the transition log (if any), then commits
 * next_state. */
fsm_result_t core_fsm_dispatch(core_fsm_t *fsm, uint32_t event, const void *event_data);

uint32_t core_fsm_current_state(const core_fsm_t *fsm);

#ifdef __cplusplus
}
#endif

#endif /* CORE_FSM_H */
