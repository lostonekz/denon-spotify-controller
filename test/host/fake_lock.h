/*
 * No-op os_lock_if_t for host tests: correct, not a cheat -- Unity tests are
 * single-threaded, so there is nothing to serialize against.
 */
#ifndef FAKE_LOCK_H
#define FAKE_LOCK_H

#include "os_if_types.h"

os_lock_if_t fake_lock_get(void);

#endif /* FAKE_LOCK_H */
