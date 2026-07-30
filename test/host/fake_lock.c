#include <stddef.h>

#include "fake_lock.h"

static void noop_lock(void *ctx)   { (void)ctx; }
static void noop_unlock(void *ctx) { (void)ctx; }

os_lock_if_t fake_lock_get(void)
{
    os_lock_if_t l = { .lock = noop_lock, .unlock = noop_unlock, .ctx = NULL };
    return l;
}
