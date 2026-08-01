/*
 * A trivial placeholder screen (a label + background) -- Phase 5 Tier 4
 * registers one of these for all three pager peers (Now Playing included;
 * Tier 6 swaps its slot for screen_now_playing.c's real composition, Phase
 * 6 swaps Denon/Settings) so the pager mechanics are proven against real
 * screen_if_t create/mount/unmount dispatch now, not against nothing
 * (Phase 5 plan §8).
 */
#ifndef SCREEN_STUB_H
#define SCREEN_STUB_H

#include <stdint.h>

#include "ui_router.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;       /* e.g. "Now Playing", "Denon", "Settings" --
                                * so Tier 7 can visually confirm which pager
                                * page is which on the real panel */
    uint32_t      bg_rgb888;
} screen_stub_cfg_t;

/* Every callback in the returned screen_if_t expects ctx->user_ctx to be a
 * `const screen_stub_cfg_t *` (set at ui_router_register_screen() time). */
screen_if_t screen_stub_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_STUB_H */
