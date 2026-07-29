# 0006 — Staged security: build it now, enable it in stages

Status: Accepted
Date: 2026-07-28
Supersedes: an earlier draft of this ADR that rejected code signing entirely

## Context

This device runs 24/7 on a wall, is reachable on the LAN, holds WiFi and Spotify
credentials, and updates itself over the network. It is a personal build today, with a
realistic possibility of becoming a product.

An earlier version of this ADR concluded "integrity yes, authenticity no" — TLS plus
SHA-256, no code signing, no eFuse operations. External review flagged unsigned OTA and
plaintext credentials as Critical, and it was right about the first one on grounds that hold
even for a personal device.

The reasoning that produced the wrong answer is worth recording, because it was a *shape* of
error rather than a fact error. It treated "do we need signing?" as a question about the
threat, when the binding constraint was actually the **key-management hazard**: on a
wall-mounted, OTA-only device, losing the signing key permanently ends updateability, and
that hazard lives in one person's backup discipline for years (RISKS R14). Rejecting signing
"solved" that by removing the key. But the real solution is **key rotation support**, which
makes a lost or compromised key recoverable — and once rotation exists, the argument against
signing evaporates.

The second driver: whichever way this goes, the decision has tentacles into partitions,
manufacturing, diagnostics, and support. Deferring it is the expensive option, because
retrofitting security into a shipped architecture is the case study everyone already knows.

## Decision

**Build every security mechanism now. Stage its enablement.**

Two profiles from one codebase:

- **Development (default).** Image signature verification, anti-rollback, redaction, and
  session hardening all **run and hard-fail**. The trust anchor is a dev key committed to
  the repo. No eFuse operations, ever. Boards stay reflashable forever.
- **Production.** Same code, release key held offline, plus encrypted NVS, plus Secure Boot
  v2 and flash encryption burned at manufacturing as the final step after factory test.

The governing rule, and the reason this works:

> **Every security mechanism runs in every build. Only the trust anchor and the enforcement
> severity change.**

Specifics in [SECURITY.md](../SECURITY.md): threat model, OTA trust chain, `security_version`
anti-rollback, two-anchor key rotation, setup-session hardening, manufacturing sequence.

## Consequences

**Good.** T1 (hostile firmware from a compromised host) and T2 (malicious downgrade) are
closed from day one, in the personal build, at the cost of a build flag and a committed dev
key.

**Good — the point of the whole design.** The signature path executes on every developer
build, so it cannot silently rot. The common failure of staged security is that the
production path is first exercised on the day it must work; this structure makes that
impossible.

**Good.** Going commercial becomes a key swap plus a manufacturing procedure, not a
redesign. The partition table already reserves `nvs_keys`, the manifest already carries
`sig`, `key_id`, and `security_version`, and the verification code is already load-bearing.

**Good.** ARCHITECTURE §0.1 is preserved intact: nothing burned, nothing foreclosed. The
production profile is *designed*, not *committed to*.

**Cost.** A signing step in the build, key material to manage, and manifest generation that
is no longer hand-editable. Roughly a day of tooling.

**Cost.** Anti-rollback needs discipline: `security_version` must be bumped deliberately and
never casually, because it is one-way in production. It gets its own release-checklist item.

**Cost.** A developer who breaks the dev key or the signing tool cannot flash over OTA until
it is fixed. Mitigated by USB flashing always working in the development profile — which is
exactly what the no-eFuse rule protects.

## Alternatives rejected

**No signing (the previous decision).** Rejected on re-examination: it optimised against key
loss, which rotation solves properly, at the cost of leaving remote firmware installation
open. Reversing this now costs a day; reversing it after a product decision costs a quarter.

**Signing in production only, stubbed in development.** The obvious way to stage this, and
the trap. An unexercised verification path is not a verification path. Rejected.

**Full production security now — Secure Boot and flash encryption immediately.** Rejected
under §0.1 and by the user's explicit constraint. The eFuse burn is irreversible, its
failure mode is a scrap board, and there is no manufacturing threat model yet to size it
against.

**Defer the whole question until a product decision exists.** Rejected — this is precisely
the deferral that makes the retrofit expensive, and it is what the review correctly
identified as the six-month pain point.
