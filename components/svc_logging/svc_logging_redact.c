#include "svc_logging_redact.h"

#include <string.h>

static int is_boundary(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0';
}

static size_t matched_prefix_len(const char *text, size_t remaining,
                                    const redact_rule_t *rules, size_t rules_count)
{
    for (size_t r = 0; r < rules_count; r++) {
        size_t plen = strlen(rules[r].prefix);
        if (plen <= remaining && memcmp(text, rules[r].prefix, plen) == 0) {
            return plen;
        }
    }
    return 0;
}

size_t svc_logging_redact_apply(char *text, size_t text_len,
                                  const redact_rule_t *rules, size_t rules_count)
{
    size_t redactions = 0;
    size_t i = 0;

    while (i < text_len) {
        size_t plen = matched_prefix_len(&text[i], text_len - i, rules, rules_count);
        if (plen == 0) {
            i++;
            continue;
        }

        size_t j = i + plen;
        while (j < text_len && !is_boundary(text[j])) {
            text[j] = '*';
            j++;
        }

        redactions++;
        i = j;
    }

    return redactions;
}
