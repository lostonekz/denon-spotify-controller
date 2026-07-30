/*
 * Host test for core_fsm: table lookup, injected action + transition-log
 * callback shapes, and FSM_NO_TRANSITION for an undefined (state, event).
 */

#include "unity.h"

#include "core_fsm.h"

/* A tiny 3-state door FSM: CLOSED -> OPENING -> OPEN -> CLOSING -> CLOSED. */
enum { DOOR_CLOSED = 0, DOOR_OPENING, DOOR_OPEN, DOOR_CLOSING };
enum { EVT_OPEN = 0, EVT_OPENED, EVT_CLOSE, EVT_CLOSED };

typedef struct {
    int action_calls;
    uint32_t last_action_event;

    int  log_calls;
    char log_fsm_name[16];
    uint32_t log_from, log_event, log_to;
} fixture_t;

static void action_count(void *ctx, uint32_t event, const void *event_data)
{
    (void)event_data;
    fixture_t *f = (fixture_t *)ctx;
    f->action_calls++;
    f->last_action_event = event;
}

static void log_capture(void *ctx, const char *name, uint32_t from, uint32_t event, uint32_t to)
{
    fixture_t *f = (fixture_t *)ctx;
    f->log_calls++;
    f->log_from  = from;
    f->log_event = event;
    f->log_to    = to;
    /* Deliberately not using strncpy from <string.h> to keep this test
     * dependency-free; the table names are short and fixed. */
    for (int i = 0; i < 15 && name[i] != '\0'; i++) {
        f->log_fsm_name[i] = name[i];
        f->log_fsm_name[i + 1] = '\0';
    }
}

static const fsm_transition_t door_transitions[] = {
    { DOOR_CLOSED,  EVT_OPEN,   action_count, DOOR_OPENING },
    { DOOR_OPENING, EVT_OPENED, NULL,         DOOR_OPEN },
    { DOOR_OPEN,    EVT_CLOSE,  action_count, DOOR_CLOSING },
    { DOOR_CLOSING, EVT_CLOSED, NULL,         DOOR_CLOSED },
};

static const fsm_table_t door_table = {
    .transitions = door_transitions,
    .count       = sizeof(door_transitions) / sizeof(door_transitions[0]),
};

static fixture_t g_fx;
static core_fsm_t g_fsm;

void setUp(void)
{
    g_fx = (fixture_t){ 0 };
    fsm_cfg_t cfg = {
        .name          = "door",
        .table         = &door_table,
        .initial_state = DOOR_CLOSED,
        .on_transition = log_capture,
        .log_ctx       = &g_fx,
        .action_ctx    = &g_fx,
    };
    core_fsm_init(&g_fsm, &cfg);
}

void tearDown(void) {}

static void test_initial_state(void)
{
    TEST_ASSERT_EQUAL_UINT32(DOOR_CLOSED, core_fsm_current_state(&g_fsm));
}

static void test_matching_transition_advances_state(void)
{
    fsm_result_t r = core_fsm_dispatch(&g_fsm, EVT_OPEN, NULL);
    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL_UINT32(DOOR_OPENING, core_fsm_current_state(&g_fsm));
}

static void test_action_is_called_with_context_and_event(void)
{
    core_fsm_dispatch(&g_fsm, EVT_OPEN, NULL);
    TEST_ASSERT_EQUAL(1, g_fx.action_calls);
    TEST_ASSERT_EQUAL_UINT32(EVT_OPEN, g_fx.last_action_event);
}

static void test_transition_with_no_action_does_not_crash(void)
{
    core_fsm_dispatch(&g_fsm, EVT_OPEN, NULL);      /* CLOSED -> OPENING, has action */
    core_fsm_dispatch(&g_fsm, EVT_OPENED, NULL);    /* OPENING -> OPEN, no action */
    TEST_ASSERT_EQUAL(1, g_fx.action_calls);        /* still 1, not 2 */
    TEST_ASSERT_EQUAL_UINT32(DOOR_OPEN, core_fsm_current_state(&g_fsm));
}

static void test_on_transition_logs_from_event_to(void)
{
    core_fsm_dispatch(&g_fsm, EVT_OPEN, NULL);
    TEST_ASSERT_EQUAL(1, g_fx.log_calls);
    TEST_ASSERT_EQUAL_UINT32(DOOR_CLOSED, g_fx.log_from);
    TEST_ASSERT_EQUAL_UINT32(EVT_OPEN, g_fx.log_event);
    TEST_ASSERT_EQUAL_UINT32(DOOR_OPENING, g_fx.log_to);
    TEST_ASSERT_EQUAL_STRING("door", g_fx.log_fsm_name);
}

static void test_undefined_transition_returns_no_transition(void)
{
    /* CLOSED has no entry for EVT_CLOSED. */
    fsm_result_t r = core_fsm_dispatch(&g_fsm, EVT_CLOSED, NULL);
    TEST_ASSERT_EQUAL(FSM_NO_TRANSITION, r);
}

static void test_undefined_transition_does_not_change_state_or_fire_callbacks(void)
{
    core_fsm_dispatch(&g_fsm, EVT_CLOSED, NULL);
    TEST_ASSERT_EQUAL_UINT32(DOOR_CLOSED, core_fsm_current_state(&g_fsm));
    TEST_ASSERT_EQUAL(0, g_fx.action_calls);
    TEST_ASSERT_EQUAL(0, g_fx.log_calls);
}

static void test_full_cycle_returns_to_initial_state(void)
{
    core_fsm_dispatch(&g_fsm, EVT_OPEN, NULL);
    core_fsm_dispatch(&g_fsm, EVT_OPENED, NULL);
    core_fsm_dispatch(&g_fsm, EVT_CLOSE, NULL);
    core_fsm_dispatch(&g_fsm, EVT_CLOSED, NULL);
    TEST_ASSERT_EQUAL_UINT32(DOOR_CLOSED, core_fsm_current_state(&g_fsm));
    TEST_ASSERT_EQUAL(2, g_fx.action_calls);   /* OPEN and CLOSE both have actions */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state);
    RUN_TEST(test_matching_transition_advances_state);
    RUN_TEST(test_action_is_called_with_context_and_event);
    RUN_TEST(test_transition_with_no_action_does_not_crash);
    RUN_TEST(test_on_transition_logs_from_event_to);
    RUN_TEST(test_undefined_transition_returns_no_transition);
    RUN_TEST(test_undefined_transition_does_not_change_state_or_fire_callbacks);
    RUN_TEST(test_full_cycle_returns_to_initial_state);
    return UNITY_END();
}
