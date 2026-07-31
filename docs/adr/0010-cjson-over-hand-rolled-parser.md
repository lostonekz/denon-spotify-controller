# 0010 — cJSON over a hand-rolled parser for Spotify responses

Status: Accepted
Date: 2026-07-31

## Context

`svc_ota` already parses JSON in this codebase (`ota_manifest_json.c`) with a hand-rolled,
closed-schema parser: the OTA manifest is small, security-critical, and has exactly 11 known
fields, so strict rejection of anything unexpected is the right behaviour — an earlier draft
of [MEMORY.md](../MEMORY.md) generalised that same approach into the plan for Spotify's
responses too: a streaming pull parser reading directly into `core_types` structs, no DOM, no
`cJSON`, no per-node allocation.

Spotify's `/v1/me/player` and `/v1/me/player/devices` responses are a different shape of
problem. They are large (several KB), deeply nested (track → artists array → album → images
array → device object), third-party, and evolving — Spotify has changed and added fields to
this API over time and will again. [RISKS.md](../RISKS.md) R5 explicitly wants this parsing
layer to be *tolerant* of unknown or additive fields, not strict about rejecting them: a
closed-schema parser that errors on an unrecognised field would break the integration outright
the next time Spotify ships a routine, backward-compatible API addition — the opposite of
what a closed, security-critical 11-field manifest schema wants.

## Decision

Use `cJSON` (ESP-IDF's built-in `json` component, already present in this install with no
extra dependency to add) for all Spotify response parsing, confined entirely to
`spotify_parse.c` — the one file, per [RISKS.md](../RISKS.md) R5's drift mitigation, that
knows any Spotify vendor field name at all. `spotify_parse.c` builds a `cJSON` tree from one
response, extracts the handful of fields this project actually uses by key (ignoring
everything else in the tree), copies them into `core_types` structs, and frees the tree
before returning. The tree's lifetime never extends beyond a single parse call.

`svc_ota`'s hand-rolled manifest parser is unchanged — this decision does not generalise to
it. The two parsers are solving opposite problems on purpose: a small, closed, strictly
validated schema (OTA) versus a large, open, drift-tolerant one (Spotify).

## Consequences

**Good.** Unknown/additive fields in Spotify's responses are ignored for free — `cJSON`
navigates by key, so a field this project doesn't read is simply never looked at, satisfying
R5 without any bespoke tolerance logic to write or maintain.

**Good.** Substantially less code than a hand-rolled recursive-descent parser would need for
a schema this size and this nested, and no separate case-by-case tolerance logic to keep in
sync as Spotify's API evolves.

**Cost.** `MEMORY.md` §5's original claim of zero-allocation, bounded-scratch JSON parsing no
longer holds for Spotify traffic — `cJSON` allocates per node from the general heap. Peak
heap usage for a real `/v1/me/player` response is not yet measured (pending Tier 6 hardware
verification, which needs a live account) and is not bounded by a fixed scratch buffer the
way the withdrawn streaming-parser design would have been; `net_http_get()`'s own
truncate-not-error response-buffer cap is what bounds the *input* size instead.

**Cost.** `cJSON` is not host-tested elsewhere in this codebase before Phase 4, so
`test/host/CMakeLists.txt` gained a new source dependency
(`$ENV{IDF_PATH}/components/json/cJSON/cJSON.c`) purely for `test_spotify_parse` — confirmed
present in this ESP-IDF install, so it costs no network fetch, but it is one more file host
tests now compile that production code elsewhere doesn't touch.

## Alternatives rejected

**A hand-rolled streaming pull parser**, generalising `svc_ota`'s own approach (the original
`MEMORY.md` §5 plan). Rejected: writing a drift-tolerant, unknown-field-skipping recursive
descent parser by hand for a response this large and this nested would be substantial,
fragile code, built to solve a problem `cJSON` already solves — and unlike the OTA manifest,
there is no security reason here to want strict rejection instead of tolerance.

**Reusing `ota_manifest_json.c`'s exact closed-schema approach for Spotify.** Rejected for the
same reason from the other direction: a closed schema is the wrong tool for an open, evolving,
third-party API, and would break the integration on Spotify's next backward-compatible field
addition — precisely the failure mode R5 exists to prevent.
