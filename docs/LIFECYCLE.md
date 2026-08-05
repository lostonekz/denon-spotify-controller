# System Lifecycle and Fault Handling

Boot ordering, readiness, restart policy, degraded operation, and what happens after a
crash. Split out of [ARCHITECTURE.md](ARCHITECTURE.md) §3.

These concerns are grouped deliberately. Left ungrouped they scatter into whichever module
noticed them first, and within a year one service retries forever, another gives up
silently, and a third blocks boot — with no single place to read the policy.

---

## 1. `svc_supervisor` — runtime only, policy lives elsewhere

The supervisor is a **mechanism, not a policy owner**. It executes a declarative table; it
does not contain per-service knowledge. This split is deliberate: a supervisor that accretes
"what should happen when X fails" for a dozen services becomes the god-object that every
change has to touch.

**`svc_supervisor` (runtime) owns:**

- Executing the boot graph in topological order.
- Tracking each subsystem's declared state and publishing transitions.
- Applying whatever restart policy the table specifies.
- Reporting aggregate health, which is what the OTA gate consumes.

**It does not own** — these are declared per service, in the service's own header, and
merely *read* by the supervisor:

```c
/* svc_spotify/include/spotify_service.h — the service declares its own contract */
static const service_desc_t SPOTIFY_SERVICE_DESC = {
    .name          = "spotify",
    .requires      = SVC_BIT(NET_WIFI) | SVC_BIT(NET_TIME),
    .restart       = RESTART_BACKOFF_THEN_DEGRADE,
    .max_restarts  = 3,
    .degraded_ok   = true,      /* the product works without it */
    .teardown      = spotify_service_teardown,
};
```

Adding a service adds a descriptor next to that service. The supervisor never changes.

### 1.1 Readiness, not just liveness

Each subsystem reports `INIT → READY → DEGRADED → FAILED`. `DEGRADED` is a normal operating
state, not an error — no AVR on the network, Spotify not yet paired — and it never blocks
another subsystem. `degraded_ok = false` (only the display path and storage) means a failure
there is fatal rather than degradable.

### 1.2 Restart is the hard part, and mostly we do not do it

Supervised restart is easy in Erlang because processes own nothing shared. In C on FreeRTOS
it is the opposite: a service holds sockets, queue handles, timers, pool blocks, TLS
contexts, and possibly LVGL objects. Restarting one that half-releases its resources leaks
them, and the leak is invisible until the third restart exhausts something.

So the rule is deliberately conservative:

> **A service may only be restarted if its teardown is provably complete and idempotent.
> Where containment cannot be proven, reboot instead.**

Rebooting a wall panel costs ~3 seconds of black screen. Leaking a socket every 20 minutes
costs a field failure nobody can reproduce. The trade is not close.

| Service | Policy | Why |
|---|---|---|
| `svc_spotify` | **restartable** | owns one TLS session + one timer; teardown closes both, no shared state |
| `svc_denon` | **restartable** | owns one socket + one queue; teardown drains and closes |
| `svc_artwork` | **restartable** | already has `suspend`/`resume` for OTA; pool slots are generation-tagged, single-owner (not refcounted — see `artwork_cache.h`'s ownership contract) |
| `svc_ota` | task is transient | created and destroyed per update by design |
| `net_wifi` | **reboot** | netif/LWIP teardown is not reliably complete in ESP-IDF |
| `svc_storage` | **reboot** | NVS handle loss is unrecoverable in place |
| `ui` / `hal_display` | **reboot** | RGB panel DMA and LVGL state cannot be safely torn down live |
| `svc_supervisor` | **reboot** | supervising the supervisor is a fiction |

Every `teardown` is required to be **idempotent** and to release in reverse acquisition
order. Each restartable service gets a host test that runs create → teardown → create in a
loop and asserts pool and handle counts return to baseline. **A service without that test
is not restartable**, regardless of what its descriptor claims. **Aspirational, not
enforced:** no such per-service loop test exists today (only `test_supervisor_restart.c`,
which exercises the policy against fakes), and no gate checks the pairing.

### 1.3 Fatal-state policy

What is worth rebooting for versus what should display a fault code. A wall panel that
reboot-loops is worse than one showing "Display fault — E04". Reboot is capped at 3 attempts
in 10 minutes; beyond that the device holds the fault screen and waits for a human, because
a loop hides the evidence needed to fix it.

### 1.4 The OTA health-gate verdict

Owned here, because this is the module that already knows what "healthy" means. Keeps
`svc_ota` from reaching into subsystems it should not know about — the coupling flagged in
review. `svc_ota` asks one question and gets one answer.

---

## 2. Coredump and fault retention

A field failure that cannot be reproduced is a failure that never gets fixed.

### 2.1 Two artifacts with very different sensitivity

An earlier draft said dumps are "redacted for registered secret values." **That is not
achievable and the claim has been removed.** A coredump is a binary RAM image: secrets appear
in it as stack temporaries, TLS session keys, partially-parsed JSON, and heap fragments, in
encodings no string matcher will find. Claiming redaction on a binary dump is worse than
admitting it is sensitive, because it invites people to treat it as safe to email.

So there are two separate artifacts:

| | **Fault summary** | **Full coredump** |
|---|---|---|
| Contents | task, PC, reason, uptime, heap/stack watermarks, last 8 fault records | raw RAM image |
| Sensitivity | low — structured, no free-form memory | **must be treated as containing credentials** |
| Default export | yes, plain | no |
| Transport | diagnostics screen, USB, or local fetch | USB only, or encrypted to a support public key |
| Retention | rolling last 8 in NVS | most recent only, in the 256 KiB partition |

The summary is what a user can safely share. It is deliberately structured rather than a
memory excerpt, which is what makes it safe.

### 2.2 Handling

- The `svc_logging` RAM ring is captured into the dump, so the last ~8 KB of log survives
  the crash. The ring itself applies secret redaction *at write time* — which does work,
  because it operates on strings we are formatting.
- On next boot, `svc_diagnostics` detects an unretrieved dump, records the summary to NVS,
  and surfaces a marker on the diagnostics screen.
- **Nothing is ever uploaded automatically.** Full dumps leave the device only via USB, or
  encrypted to a support public key baked into the image, and only on explicit user action.
- The one-shot local HTTP export (deferred — [ROADMAP.md](ROADMAP.md), prerequisites
  a wired fault screen doesn't exist yet; `net_http` has since Phase 2) is planned as an inbound service, hardened
  accordingly: pairing PIN shown on screen, 5-minute timeout, closes on first fetch, never
  listening otherwise. `SECURITY.md` §5 documents exactly this bounded-listener pattern now, for the pairing page
  ([adr/0011](adr/0011-on-device-pair-page-over-console-paste.md)) — this note used to say
  it didn't, which was true only while §5 was written
  for the OAuth flow specifically (adr/0009, no listener at all) — so this mechanism's own
  real security review is still owed once it's actually built, not covered by that section.
