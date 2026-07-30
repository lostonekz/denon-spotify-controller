#include "svc_supervisor_graph.h"

#include <stdbool.h>
#include <stdint.h>

graph_result_t svc_supervisor_graph_order(const service_desc_t *descs, size_t count,
                                            size_t *out_order)
{
    uint32_t present_mask = 0;
    for (size_t i = 0; i < count; i++) {
        present_mask |= SVC_BIT(descs[i].id);
    }
    for (size_t i = 0; i < count; i++) {
        if ((descs[i].requires & ~present_mask) != 0) {
            return GRAPH_MISSING_DEPENDENCY;
        }
    }

    uint32_t in_degree[SVC_ID_COUNT];
    bool     done[SVC_ID_COUNT];
    for (size_t i = 0; i < count; i++) {
        uint32_t bits = descs[i].requires;
        uint32_t n = 0;
        while (bits != 0) {
            n += bits & 1u;
            bits >>= 1;
        }
        in_degree[i] = n;
        done[i] = false;
    }

    size_t placed = 0;
    while (placed < count) {
        bool progressed = false;

        for (size_t i = 0; i < count; i++) {
            if (done[i] || in_degree[i] != 0) {
                continue;
            }

            out_order[placed++] = i;
            done[i] = true;
            progressed = true;

            for (size_t j = 0; j < count; j++) {
                if (!done[j] && (descs[j].requires & SVC_BIT(descs[i].id)) != 0) {
                    in_degree[j]--;
                }
            }
        }

        if (!progressed) {
            return GRAPH_CYCLE;
        }
    }

    return GRAPH_OK;
}
