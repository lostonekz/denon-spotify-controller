#include "net_wifi_backoff.h"

uint32_t net_wifi_backoff_max_delay_ms(uint32_t attempt)
{
    uint32_t exponent = (attempt < NET_WIFI_BACKOFF_MAX_EXPONENT)
                            ? attempt : NET_WIFI_BACKOFF_MAX_EXPONENT;
    uint64_t max_delay = (uint64_t)NET_WIFI_BACKOFF_BASE_MS << exponent;
    if (max_delay > NET_WIFI_BACKOFF_CAP_MS) {
        max_delay = NET_WIFI_BACKOFF_CAP_MS;
    }
    return (uint32_t)max_delay;
}

void net_wifi_backoff_init(net_wifi_backoff_t *b, net_wifi_rand_fn rand_u32, void *rand_ctx)
{
    b->attempt   = 0;
    b->rand_u32  = rand_u32;
    b->rand_ctx  = rand_ctx;
}

void net_wifi_backoff_reset(net_wifi_backoff_t *b)
{
    b->attempt = 0;
}

uint32_t net_wifi_backoff_next_delay_ms(net_wifi_backoff_t *b)
{
    uint32_t max_delay = net_wifi_backoff_max_delay_ms(b->attempt);
    b->attempt++;

    uint32_t r = b->rand_u32(b->rand_ctx);
    return r % (max_delay + 1);
}
