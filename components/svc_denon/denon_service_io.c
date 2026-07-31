/*
 * One denon-task iteration's I/O: drain+send a due cmdq entry, poll the
 * currently-active transport for an incoming line, parse it, publish
 * TOPIC_DENON, and write DOMAIN_DENON. Target-only. See
 * svc_denon_internal.h.
 *
 * "Active transport" is derived from denon_link_fsm's current state, not
 * tracked separately -- CONNECTED_TELNET means telnet, CONNECTED_HTTP
 * means HTTP, anything else means no transport is up and this tick is a
 * no-op (pending cmdq entries simply accumulate past their rate-limit
 * window and go out the instant a link exists again).
 */
#include "svc_denon_internal.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "denon_codec.h"
#include "denon_codec_tables.h"

static const char *TAG = "svc_denon";

#define DENON_IO_POLL_TIMEOUT_MS 250u   /* short enough to keep cmdq's 100ms rate-limit drain responsive */

static denon_transport_if_t *active_transport(svc_denon_t *d, denon_link_state_t s)
{
    if (s == DENON_LINK_STATE_CONNECTED_TELNET) return &d->telnet_iface;
    if (s == DENON_LINK_STATE_CONNECTED_HTTP)   return &d->http_iface;
    return NULL;
}

/* The event/state-loss-signalling event differs by which transport was
 * active -- CONNECTED_TELNET only has a TELNET_EOF row in the transition
 * table; CONNECTED_HTTP only has a HOST_UNREACHABLE row (there is no
 * "HTTP_FAILED while already connected" transition -- HTTP_FAILED is only
 * defined from CONNECTING_HTTP). */
static void dispatch_link_lost(svc_denon_t *d, denon_link_state_t s)
{
    if (s == DENON_LINK_STATE_CONNECTED_TELNET) {
        denon_link_fsm_dispatch(&d->state.link_fsm, DENON_LINK_EVT_TELNET_EOF, NULL);
    } else if (s == DENON_LINK_STATE_CONNECTED_HTTP) {
        denon_link_fsm_dispatch(&d->state.link_fsm, DENON_LINK_EVT_HOST_UNREACHABLE, NULL);
    }
}

static void publish_and_write(svc_denon_t *d, denon_evt_kind_t kind)
{
    event_t evt = { .topic = TOPIC_DENON };
    evt.u.denon.kind               = kind;
    evt.u.denon.volume_half_db     = d->state.local_state.volume_half_db;
    evt.u.denon.power_on           = d->state.local_state.power_on;
    evt.u.denon.muted              = d->state.local_state.muted;
    evt.u.denon.input_id           = d->state.local_state.input_id;
    evt.u.denon.listening_mode_id = d->state.local_state.listening_mode_id;
    core_eventbus_publish(d->bus, &evt);

    /* denon_link_fsm's CONNECTED_TELNET means OK (~20ms push), CONNECTED_HTTP
     * means STALE (up to ~2-10s poll cadence, see denon_http.h's KNOWN GAP
     * note) -- matches ADR-0003's "surfaced in diagnostics as HTTP
     * (fallback), never as a user-facing error" via link_status_t, not a
     * second copy of link state inside denon_state_t. */
    link_status_t status = (denon_link_fsm_current_state(&d->state.link_fsm) == DENON_LINK_STATE_CONNECTED_TELNET)
                                ? LINK_STATUS_OK : LINK_STATUS_STALE;
    core_state_write(d->state_store, DOMAIN_DENON, status, &d->state.local_state, sizeof(d->state.local_state));
}

static void handle_line(svc_denon_t *d, const char *line)
{
    uint16_t mv;
    denon_power_t pw;
    denon_mute_t mu;
    denon_input_id_t si;
    denon_listening_mode_id_t ms;

    denon_codec_status_t mv_st = denon_mv_parse(line, &mv);
    if (mv_st == DENON_CODEC_OK) {
        d->state.local_state.volume_half_db = mv;
        publish_and_write(d, DENON_EVT_VOLUME);
        return;
    }
    if (mv_st == DENON_CODEC_MAX_REPORT) {
        /* "MVMAX 98" -- a capability report, not a level. denon_codec.h's
         * whole reason to give this its own status: silently NOT a volume
         * change. */
        return;
    }

    if (denon_pw_parse(line, &pw) == DENON_TABLE_OK) {
        d->state.local_state.power_on = (pw == DENON_PW_ON);
        publish_and_write(d, DENON_EVT_POWER);
        return;
    }
    if (denon_mu_parse(line, &mu) == DENON_TABLE_OK) {
        d->state.local_state.muted = (mu == DENON_MU_ON);
        publish_and_write(d, DENON_EVT_MUTE);
        return;
    }
    if (denon_si_parse(line, &si) == DENON_TABLE_OK) {
        d->state.local_state.input_id = (uint8_t)si;
        publish_and_write(d, DENON_EVT_INPUT);
        return;
    }
    if (denon_ms_parse(line, &ms) == DENON_TABLE_OK) {
        d->state.local_state.listening_mode_id = (uint8_t)ms;
        publish_and_write(d, DENON_EVT_LISTENING_MODE);
        return;
    }
    /* Unrecognised line -- RISKS.md R5's "vendor field drift" case.
     * Dropped, not fatal: one unparsed push event is not a dead link. */
}

/* Returns true if this tick did real blocking work (telnet's select() waited
 * close to its own timeout, or a due HTTP poll performed an actual request),
 * false if it returned essentially immediately (no transport up, or HTTP's
 * poll() found its own interval not yet due). denon_service_task uses this
 * to add a bounded sleep exactly when nothing blocking happened -- see the
 * Tier -1 fix note in denon_service_target.c; without it, every
 * disconnected/backoff state and every idle tick between HTTP polls spun
 * the task at 100% CPU with no delay anywhere in the loop. */
bool denon_service_io_tick(svc_denon_t *d)
{
    denon_link_state_t s = denon_link_fsm_current_state(&d->state.link_fsm);
    denon_transport_if_t *active = active_transport(d, s);
    if (active == NULL) {
        return false;
    }

    int64_t now = d->clock.now_us(d->clock.ctx);
    denon_cmd_t cmd;
    bool have_cmd;

    xSemaphoreTake(d->cmdq_lock, portMAX_DELAY);
    have_cmd = denon_cmdq_pop_due(&d->state.cmdq, now, &cmd);
    xSemaphoreGive(d->cmdq_lock);

    if (have_cmd) {
        if (!active->send_cmd(active->ctx, cmd.cmd)) {
            dispatch_link_lost(d, s);
            return true;   /* don't also poll a link just discovered dead this tick */
        }
        /* EVENTS_AND_STATE.md §6.4's "the log is the entire debugging
         * story" -- this is the one line that lets a real-hardware
         * verification actually see what half-dB value made it past the
         * safety clamp onto the wire. */
        ESP_LOGI(TAG, "sent: %s", cmd.cmd);
    }

    char line[DENON_TRANSPORT_LINE_BUF_LEN];
    int64_t poll_start = d->clock.now_us(d->clock.ctx);
    denon_transport_poll_result_t pr = active->poll(active->ctx, line, sizeof(line), DENON_IO_POLL_TIMEOUT_MS);
    int64_t poll_elapsed_ms = (d->clock.now_us(d->clock.ctx) - poll_start) / 1000;

    if (pr == DENON_TRANSPORT_POLL_LOST) {
        dispatch_link_lost(d, s);
    } else if (pr == DENON_TRANSPORT_POLL_LINE) {
        ESP_LOGI(TAG, "recv: %s", line);
        handle_line(d, line);
    }

    return poll_elapsed_ms >= (DENON_IO_POLL_TIMEOUT_MS / 2);
}
