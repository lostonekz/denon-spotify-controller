/*
 * A read-only "which input is the receiver on" status card -- the Receiver
 * screen's "LISTENING TO" section shows two of these (Spotify/HEOS,
 * TV audio). Deliberately NOT tappable: w_power_row's existing combined
 * power+input toggle already fully determines input_id as a binary choice
 * (on -> DENON_INPUT_NET, off -> DENON_INPUT_TV), so a second, independent
 * tap-to-switch control here would only risk fighting that one real control
 * over the same state. A previous tappable multi-row picker widget
 * (w_device_picker, for Spotify Connect device switching) was built and then
 * fully reverted after a real hardware hang from per-object style
 * allocation in a loop -- this widget avoids that whole hazard class by
 * having no click handler at all.
 */
#ifndef W_SOURCE_STATUS_CARD_H
#define W_SOURCE_STATUS_CARD_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    W_SOURCE_STATUS_CARD_ICON_BARS = 0,   /* Spotify/HEOS -- an abstract equalizer-bars glyph,
                                               deliberately not the real trademarked Spotify logo */
    W_SOURCE_STATUS_CARD_ICON_TV,
    W_SOURCE_STATUS_CARD_ICON_NONE,       /* Connection screen's Telnet/HTTP cards -- no icon
                                               at all, title sits at the card's own top */
} w_source_status_card_icon_t;

typedef struct {
    w_source_status_card_icon_t icon_kind;
    const char                     *title;   /* static per instance, e.g. "Spotify"/"TV audio" --
                                                    set once at creation, never changes */
} w_source_status_card_cfg_t;

#define W_SOURCE_STATUS_CARD_ICON_SEGMENTS_MAX 3   /* bars uses 3; tv uses 2 (screen + stand) */

typedef struct {
    lv_obj_t *root;
    lv_obj_t *icon_container;
    lv_obj_t *title_label;
    lv_obj_t *subtitle_label;
    lv_obj_t *icon_segments[W_SOURCE_STATUS_CARD_ICON_SEGMENTS_MAX];
    uint8_t     icon_segment_count;
    /* Last applied (selected, live) pair, so the ~9 style writes they drive
     * are only issued when one actually changes -- this widget's setter is
     * called every UI-task tick from the Receiver screen's apply glue. See
     * w_source_status_card_set_state()'s own comment. `applied` starts
     * false so the first call always writes, whatever the pair is. */
    bool        applied;
    bool        applied_selected;
    bool        applied_live;
} w_source_status_card_t;

lv_obj_t *w_source_status_card_create(lv_obj_t *parent, w_source_status_card_t *w,
                                          const w_source_status_card_cfg_t *cfg);

/* selected drives the card's own bg/border/icon/title colour: highlighted
 * (filled, dark text) when selected, dark+outlined when not.
 *
 * live picks WHICH highlight a selected card gets -- green
 * (THEME_COLOR_ACCENT_LIVE) when the source is genuinely playing through a
 * powered-on receiver, plain white otherwise. Ignored unless selected. The
 * Receiver screen passes denon power state here, so the active source reads
 * green only while the receiver is actually on; an active-but-powered-off
 * source stays white rather than claiming to be live. The Connection
 * screen's own transport cards pass false -- "which transport are we on" is
 * not a liveness claim.
 *
 * subtitle is live text (e.g. "HEOS - playing"/"paused"/"idle" for the
 * Spotify card, a static "HDMI ARC" string for the TV card). */
void w_source_status_card_set_state(w_source_status_card_t *w, bool selected, bool live,
                                        const char *subtitle);

#ifdef __cplusplus
}
#endif

#endif /* W_SOURCE_STATUS_CARD_H */
