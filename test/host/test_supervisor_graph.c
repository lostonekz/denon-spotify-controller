/*
 * Host test for svc_supervisor_graph: topological order for a linear chain
 * and a diamond, cycle rejection, missing-dependency rejection, and a
 * valid permutation when nothing depends on anything.
 */

#include "unity.h"

#include "svc_supervisor_graph.h"

#include <stdbool.h>

static service_desc_t make_desc(service_id_t id, uint32_t requires)
{
    service_desc_t d = { 0 };
    d.id       = id;
    d.requires = requires;
    d.restart  = RESTART_REBOOT;
    return d;
}

void setUp(void) {}
void tearDown(void) {}

static void test_linear_chain_order(void)
{
    service_desc_t descs[] = {
        make_desc(SVC_ID_STORAGE, 0),
        make_desc(SVC_ID_SETTINGS, SVC_BIT(SVC_ID_STORAGE)),
        make_desc(SVC_ID_LOGGING, SVC_BIT(SVC_ID_SETTINGS)),
    };
    size_t order[3];

    TEST_ASSERT_EQUAL(GRAPH_OK, svc_supervisor_graph_order(descs, 3, order));
    TEST_ASSERT_EQUAL_UINT32(0, order[0]);
    TEST_ASSERT_EQUAL_UINT32(1, order[1]);
    TEST_ASSERT_EQUAL_UINT32(2, order[2]);
}

static void test_diamond_dependency_order(void)
{
    /* 0=A (nothing), 1=B (needs A), 2=C (needs A), 3=D (needs B and C) */
    service_desc_t descs[] = {
        make_desc(SVC_ID_STORAGE, 0),
        make_desc(SVC_ID_SETTINGS, SVC_BIT(SVC_ID_STORAGE)),
        make_desc(SVC_ID_LOGGING, SVC_BIT(SVC_ID_STORAGE)),
        make_desc(SVC_ID_DIAGNOSTICS, SVC_BIT(SVC_ID_SETTINGS) | SVC_BIT(SVC_ID_LOGGING)),
    };
    size_t order[4];

    TEST_ASSERT_EQUAL(GRAPH_OK, svc_supervisor_graph_order(descs, 4, order));

    size_t pos[4];
    for (size_t slot = 0; slot < 4; slot++) {
        pos[order[slot]] = slot;
    }

    TEST_ASSERT_TRUE(pos[0] < pos[1]);
    TEST_ASSERT_TRUE(pos[0] < pos[2]);
    TEST_ASSERT_TRUE(pos[1] < pos[3]);
    TEST_ASSERT_TRUE(pos[2] < pos[3]);
}

static void test_cycle_is_rejected_and_out_order_untouched(void)
{
    service_desc_t descs[] = {
        make_desc(SVC_ID_STORAGE, SVC_BIT(SVC_ID_SETTINGS)),
        make_desc(SVC_ID_SETTINGS, SVC_BIT(SVC_ID_STORAGE)),
    };
    size_t order[2] = { 99, 99 };

    TEST_ASSERT_EQUAL(GRAPH_CYCLE, svc_supervisor_graph_order(descs, 2, order));
    TEST_ASSERT_EQUAL_UINT32(99, order[0]);
    TEST_ASSERT_EQUAL_UINT32(99, order[1]);
}

static void test_missing_dependency_is_rejected_and_out_order_untouched(void)
{
    /* SVC_ID_STORAGE is required but never appears in descs[]. */
    service_desc_t descs[] = {
        make_desc(SVC_ID_SETTINGS, SVC_BIT(SVC_ID_STORAGE)),
    };
    size_t order[1] = { 77 };

    TEST_ASSERT_EQUAL(GRAPH_MISSING_DEPENDENCY, svc_supervisor_graph_order(descs, 1, order));
    TEST_ASSERT_EQUAL_UINT32(77, order[0]);
}

static void test_independent_services_yield_a_valid_permutation(void)
{
    service_desc_t descs[] = {
        make_desc(SVC_ID_STORAGE, 0),
        make_desc(SVC_ID_LOGGING, 0),
        make_desc(SVC_ID_SCHEDULER, 0),
    };
    size_t order[3];

    TEST_ASSERT_EQUAL(GRAPH_OK, svc_supervisor_graph_order(descs, 3, order));

    bool seen[3] = { false, false, false };
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(order[i] < 3);
        TEST_ASSERT_FALSE(seen[order[i]]);
        seen[order[i]] = true;
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_linear_chain_order);
    RUN_TEST(test_diamond_dependency_order);
    RUN_TEST(test_cycle_is_rejected_and_out_order_untouched);
    RUN_TEST(test_missing_dependency_is_rejected_and_out_order_untouched);
    RUN_TEST(test_independent_services_yield_a_valid_permutation);
    return UNITY_END();
}
