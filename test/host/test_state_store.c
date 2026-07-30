/*
 * Host test for core_state: write-then-read exact, version bumps only the
 * written domain, updated_us reflects the injected clock, and size
 * mismatches (wrong domain/type pairing) are rejected rather than silently
 * truncated or overrun.
 */

#include "unity.h"

#include "core_state.h"
#include "fake_lock.h"
#include "fake_time_source.h"

static core_state_t g_store;
static fake_time_source_t g_clock;

void setUp(void)
{
    g_clock.now_us = 1000;
    core_state_init(&g_store, fake_lock_get(), fake_time_source_get(&g_clock));
}

void tearDown(void) {}

static void test_write_then_read_roundtrip_exact(void)
{
    denon_state_t in = { 0 };
    in.power_on          = true;
    in.muted              = false;
    in.volume_half_db     = 120;
    in.input_id           = 3;
    in.listening_mode_id  = 2;

    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_DENON, LINK_STATUS_OK, &in, sizeof(in)));

    denon_state_t out;
    TEST_ASSERT_TRUE(core_state_read(&g_store, DOMAIN_DENON, &out, sizeof(out)));

    TEST_ASSERT_TRUE(out.power_on);
    TEST_ASSERT_FALSE(out.muted);
    TEST_ASSERT_EQUAL_UINT16(120, out.volume_half_db);
    TEST_ASSERT_EQUAL_UINT8(3, out.input_id);
    TEST_ASSERT_EQUAL_UINT8(2, out.listening_mode_id);
}

static void test_write_sets_status_version_and_updated_us(void)
{
    g_clock.now_us = 42000;

    power_state_t in = { .power_state = 1, .backlight_pct = 80 };
    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_POWER, LINK_STATUS_OK, &in, sizeof(in)));

    power_state_t out;
    TEST_ASSERT_TRUE(core_state_read(&g_store, DOMAIN_POWER, &out, sizeof(out)));

    TEST_ASSERT_EQUAL(LINK_STATUS_OK, out.meta.status);
    TEST_ASSERT_EQUAL_UINT32(1, out.meta.version);
    TEST_ASSERT_EQUAL_INT64(42000, out.meta.updated_us);
}

static void test_writer_supplied_status_is_honored(void)
{
    net_state_t in = { .link_up = false };
    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_NET, LINK_STATUS_RECONNECTING, &in, sizeof(in)));

    net_state_t out;
    TEST_ASSERT_TRUE(core_state_read(&g_store, DOMAIN_NET, &out, sizeof(out)));
    TEST_ASSERT_EQUAL(LINK_STATUS_RECONNECTING, out.meta.status);
}

static void test_version_bumps_only_the_written_domain(void)
{
    net_state_t net_in = { 0 };
    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_NET, LINK_STATUS_OK, &net_in, sizeof(net_in)));
    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_NET, LINK_STATUS_OK, &net_in, sizeof(net_in)));

    net_state_t net_out;
    TEST_ASSERT_TRUE(core_state_read(&g_store, DOMAIN_NET, &net_out, sizeof(net_out)));
    TEST_ASSERT_EQUAL_UINT32(2, net_out.meta.version);

    denon_state_t denon_out;
    TEST_ASSERT_TRUE(core_state_read(&g_store, DOMAIN_DENON, &denon_out, sizeof(denon_out)));
    TEST_ASSERT_EQUAL_UINT32(0, denon_out.meta.version);   /* untouched */
}

static void test_updated_us_tracks_clock_across_writes(void)
{
    ota_state_t in = { 0 };

    g_clock.now_us = 100;
    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_OTA, LINK_STATUS_OK, &in, sizeof(in)));

    g_clock.now_us = 200;
    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_OTA, LINK_STATUS_OK, &in, sizeof(in)));

    ota_state_t out;
    TEST_ASSERT_TRUE(core_state_read(&g_store, DOMAIN_OTA, &out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT64(200, out.meta.updated_us);
    TEST_ASSERT_EQUAL_UINT32(2, out.meta.version);
}

static void test_read_rejects_size_mismatch(void)
{
    net_state_t in = { .link_up = true };
    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_NET, LINK_STATUS_OK, &in, sizeof(in)));

    denon_state_t wrong_type_out;
    TEST_ASSERT_FALSE(core_state_read(&g_store, DOMAIN_NET, &wrong_type_out, sizeof(wrong_type_out)));
}

static void test_write_rejects_size_mismatch_and_leaves_domain_unchanged(void)
{
    net_state_t original = { .link_up = true, .ipv4 = 0xC0A80001u };
    TEST_ASSERT_TRUE(core_state_write(&g_store, DOMAIN_NET, LINK_STATUS_OK, &original, sizeof(original)));

    denon_state_t wrong_type_in = { 0 };
    TEST_ASSERT_FALSE(core_state_write(&g_store, DOMAIN_NET, LINK_STATUS_OK, &wrong_type_in, sizeof(wrong_type_in)));

    net_state_t out;
    TEST_ASSERT_TRUE(core_state_read(&g_store, DOMAIN_NET, &out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0xC0A80001u, out.ipv4);
    TEST_ASSERT_EQUAL_UINT32(1, out.meta.version);   /* still just the one good write */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read_roundtrip_exact);
    RUN_TEST(test_write_sets_status_version_and_updated_us);
    RUN_TEST(test_writer_supplied_status_is_honored);
    RUN_TEST(test_version_bumps_only_the_written_domain);
    RUN_TEST(test_updated_us_tracks_clock_across_writes);
    RUN_TEST(test_read_rejects_size_mismatch);
    RUN_TEST(test_write_rejects_size_mismatch_and_leaves_domain_unchanged);
    return UNITY_END();
}
