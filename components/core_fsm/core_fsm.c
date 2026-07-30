#include "core_fsm.h"

void core_fsm_init(core_fsm_t *fsm, const fsm_cfg_t *cfg)
{
    fsm->cfg           = *cfg;
    fsm->current_state = cfg->initial_state;
}

fsm_result_t core_fsm_dispatch(core_fsm_t *fsm, uint32_t event, const void *event_data)
{
    const fsm_table_t *table = fsm->cfg.table;

    for (size_t i = 0; i < table->count; i++) {
        const fsm_transition_t *t = &table->transitions[i];
        if (t->state != fsm->current_state || t->event != event) {
            continue;
        }

        if (t->action != NULL) {
            t->action(fsm->cfg.action_ctx, event, event_data);
        }

        if (fsm->cfg.on_transition != NULL) {
            fsm->cfg.on_transition(fsm->cfg.log_ctx, fsm->cfg.name,
                                   fsm->current_state, event, t->next_state);
        }

        fsm->current_state = t->next_state;
        return FSM_OK;
    }

    return FSM_NO_TRANSITION;
}

uint32_t core_fsm_current_state(const core_fsm_t *fsm)
{
    return fsm->current_state;
}
