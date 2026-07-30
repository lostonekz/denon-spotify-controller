/*
 * Controllable fake clock for host tests: set now_us directly, no wall-clock
 * dependency.
 */
#ifndef FAKE_TIME_SOURCE_H
#define FAKE_TIME_SOURCE_H

#include <stdint.h>

#include "os_if_types.h"

typedef struct {
    int64_t now_us;
} fake_time_source_t;

time_source_t fake_time_source_get(fake_time_source_t *fts);

#endif /* FAKE_TIME_SOURCE_H */
