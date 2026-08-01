/*
 * UTF-8-safe truncation for Now Playing metadata -- RISKS.md R17's
 * "safety-critical part": truncating a multi-byte sequence at a buffer
 * boundary must never produce a partial codepoint. Unrepresentable byte
 * sequences render as U+FFFD (never `.notdef`/blank/garbled fragments).
 *
 * Pure, host-tested, zero LVGL/ESP-IDF -- Tier 1. Deliberately does not
 * decide font glyph coverage (Latin-1 + Latin Extended-A, UI_ARCHITECTURE.md
 * §5.3b) -- that's tools/gen_assets.py's job at build time; this module
 * only defends the byte-level UTF-8 boundary, which is a correctness
 * property independent of which glyphs a given font happens to embed.
 */
#ifndef VIEW_MODEL_TEXT_H
#define VIEW_MODEL_TEXT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Copies as much of src (NUL-terminated UTF-8, or NULL) as fits in
 * dst_cap-1 bytes without ever splitting a multi-byte codepoint --
 * truncation stops before a codepoint that wouldn't fit whole, it never
 * emits half of one. Any ill-formed byte sequence encountered is replaced
 * with U+FFFD (3 bytes: EF BF BD), advancing past exactly the one bad
 * leading byte before resuming -- the "maximal subpart" replacement rule.
 * Always NUL-terminates dst (dst_cap must be >= 1, else this is a no-op).
 * Returns the number of bytes written, excluding the NUL. */
size_t view_model_text_copy_safe(char *dst, size_t dst_cap, const char *src);

/* True if s (NUL-terminated) contains at least one representable
 * character -- i.e. is not empty and not made up entirely of U+FFFD
 * replacement sequences. Used to decide whether a copy_safe() result is
 * usable or whether a fallback (RISKS.md R17: album name, or "Unknown
 * Track") is needed instead. */
bool view_model_text_has_content(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_TEXT_H */
