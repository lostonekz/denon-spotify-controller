#include "fake_time_source.h"

static int64_t fake_now_us(void *ctx)
{
    const fake_time_source_t *fts = (const fake_time_source_t *)ctx;
    return fts->now_us;
}

time_source_t fake_time_source_get(fake_time_source_t *fts)
{
    time_source_t t = { .now_us = fake_now_us, .ctx = fts };
    return t;
}
