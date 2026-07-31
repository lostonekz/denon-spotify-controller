# Event and State Architecture

Deliverables §5 and §6. Split out of [ARCHITECTURE.md](ARCHITECTURE.md) because this is the
contract every module is written against, and it deserves to be findable on its own.

The governing rule lives in ARCHITECTURE §1.1 and [adr/0001](adr/0001-intents-down-state-up.md):
**intents flow down, state flows up, and the UI never touches a service directly.** This
document specifies the two mechanisms that make that rule workable.

---

## 5. Event architecture

### 5.1 Shape

Two channels, deliberately asymmetric:

- **Intents** (UI → services): a single `intent_t` POD posted to one intent queue, consumed
  by a dispatcher that routes to the owning service. One queue, so ordering of user actions
  is globally preserved — this matters when a user hits *next* then *pause* quickly.
- **Events** (services → UI, and service → service): topic-based pub/sub. Each subscriber
  owns a queue; publishing copies the event into every matching subscriber queue.

**The single intent queue preserves dispatch order, not completion order.** This is a real
limitation and it must not be over-claimed. Two intents routed to different services execute
concurrently on different tasks, with wildly different latencies — a Denon command over
telnet completes in ~20 ms, a Spotify call in 200–600 ms. Within one service, execution is
serial and ordered. Across services, it is not.

Where that actually bites is **cross-domain intents**, of which volume retargeting
([adr/0007](adr/0007-denon-volume-is-primary.md)) is the live example: the target of a volume
change depends on Spotify's reported active device, so a volume intent racing a
device-changed event can be applied to the wrong output.

Three mechanisms, in increasing order of cost — use the cheapest that suffices:

1. **Causal sequence IDs.** Every intent carries `seq` and the `state_version` of each domain
   it read to make its decision. A service that finds the domain has moved on since then
   re-evaluates instead of acting on a stale premise.
2. **Acknowledgements.** Services publish `INTENT_ACCEPTED` / `INTENT_REJECTED` / 
   `INTENT_SUPERSEDED` carrying the originating `seq`. This is what the view model's pending
   state (§6.3) reconciles against, and it is why pending intents can be abandoned
   deterministically rather than on a timeout alone.
3. **A mandatory coordinator for every cross-domain action.** Volume retargeting gets a small
   dedicated coordinator that resolves the target *once* against a consistent snapshot, then
   issues a single-domain intent. Sequencing logic lives in one named place rather than
   being implied by queue ordering.

Mechanisms 1 and 2 apply to every intent. **Mechanism 3 is mandatory, not optional, for any
intent whose target or effect depends on more than one domain's state** — an earlier draft
left it discretionary, which is exactly how a race gets shipped: the discretion is exercised
under deadline by whoever adds the second cross-domain action.

The enforcement is structural. A cross-domain action has no single owning service, so there
is no service the dispatcher can route it to — it *must* go to a coordinator, or it has
nowhere to go. `intent_t` carries a `domain` field; `INTENT_DOMAIN_MULTI` routes to the
coordinator table and a missing entry is a startup assertion, not a runtime surprise.

The list is deliberately short — today it is exactly one, volume retargeting — and every
addition to it is a design review.

### 5.2 No allocation, no lifetime bugs

Every event is a **fixed-size POD, copied by value**:

```c
typedef struct {
    event_topic_t topic;
    uint32_t      seq;
    int64_t       ts_us;
    union {
        playback_evt_t   playback;      /* <= 48 B */
        denon_evt_t      denon;
        artwork_evt_t    artwork;       /* carries a HANDLE, not pixels */
        net_evt_t        net;
        setting_evt_t    setting;
    } u;
} event_t;                              /* static_assert(sizeof(event_t) <= 64) */
```

Large payloads are never copied. Album art lives in a preallocated slot pool and the event
carries a generation-tagged handle:

```c
typedef struct { uint32_t slot; uint32_t generation; } artwork_handle_t;
```

The generation counter is the important part: if the UI holds a handle to a slot that has
since been evicted and reused, `artwork_cache_lock()` sees the generation mismatch and
returns `NULL` instead of rendering someone else's album. Use-after-free becomes a
returned error rather than a corrupted frame.

**Generation is 32-bit, deliberately.** With 3 slots and a track change every few minutes,
a 16-bit counter wraps in roughly a year of continuous playback — well inside the life of a
device that is never power-cycled. On wrap, a stale handle validates against the wrong image
and the failure is a silently wrong album cover that no test would catch. 32 bits pushes
wrap past any plausible device lifetime. The extra 4 bytes are free; the bug would not have
been.

### 5.2b Schema versioning

Events are the contract between every producer and consumer in the system. Without a
versioning rule, every feature addition risks breaking a consumer silently, and the failure
shows up as a subtly wrong screen rather than a compile error.

Two mechanisms, both cheap:

- **`static_assert` on every payload size.** `sizeof(playback_evt_t)` is asserted at compile
  time. Changing a payload without thinking about it is a build failure, not a runtime
  surprise.
- **Additive-only within a major version.** New fields append to a payload struct and must
  have a zero value that means "absent". Consumers ignore fields they do not know. A field
  may be deprecated (left in place, stop writing it) but never removed or retyped in place —
  removal or retype requires a new topic.

```c
#define EVENT_SCHEMA_VERSION 1   /* bump only on a breaking change */
```

The version is not transmitted — every producer and consumer is in the same image, so a
mismatch is impossible at runtime. It exists as a **review tripwire**: bumping it is the
signal that a change is breaking, and the release checklist requires every subscriber to be
re-examined when it moves. Cheap discipline that costs nothing until the day it matters.

### 5.3 Backpressure policy — decided up front, per topic

Queue overflow is not an exception, it's a design parameter. Each topic declares its policy:

| Topic | Depth | On full | Release hook |
|---|---|---|---|
| `TOPIC_PLAYBACK` | 4 | **Replace oldest** — only the newest state matters | none |
| `TOPIC_DENON` | 8 | Replace oldest | none |
| `TOPIC_ARTWORK` | 2 | Replace oldest | none — see the note below the table |
| intent queue* | 16 | **Drop newest + count it** — never lose an older user action | none |
| `TOPIC_NET` | 8 | Replace oldest | none |
| `TOPIC_TOAST` | 4 | Drop newest | none |

Every drop increments a counter visible in diagnostics. A rising drop count is the earliest
signal that a task is falling behind.

*`intent_t` does not travel over `core_eventbus` and there is no `TOPIC_INTENT` member of
`event_topic_t` — it doesn't fit `event_t`'s fixed-size union budget (§5.2), and intents are
consumed by one dispatcher rather than fanned out to subscribers, so bus semantics buy
nothing here. Phase 3 (`svc_denon`) implemented it as a dedicated `QueueHandle_t` owned
directly in `app_ctx_t` (same shape as `svc_ota_t.result_queue`: a plain FreeRTOS queue
inside a struct, no new abstraction component), drained by `intent_dispatch_tick_trampoline`
in `app_wiring`. It adopts this table's depth-16/drop-newest-and-count policy values
verbatim — only the transport differs from what this row's name implies.

**The bus does not know what an artwork handle is.** An earlier draft specified "replace
oldest, release the dropped handle" for `TOPIC_ARTWORK`, which quietly made a generic
transport aware of one domain's ownership rules — a dependency-inversion violation that
would have grown a second and third special case within a year.

Instead, the release hook is **injected as part of the topic policy** at registration,
available to any future topic whose payload is a claim on caller-owned storage:

```c
typedef void (*event_release_fn)(const event_t *dropped, void *ctx);

typedef struct {
    event_topic_t     topic;
    uint8_t           depth;
    overflow_policy_t on_full;      /* REPLACE_OLDEST | DROP_NEWEST */
    event_release_fn  on_release;   /* NULL for PODs that own nothing */
    void             *release_ctx;
} topic_policy_t;
```

**`TOPIC_ARTWORK` itself registers `on_release = NULL`, not `artwork_cache_release` — reversed
from an earlier draft of this section, found by review during Phase 4 (`svc_artwork`).**
`core_pool_slot_t`/`artwork_cache_t` is generation-tagged, single-owner storage
(`artwork_cache.h`'s own ownership contract — not refcounted; see LIFECYCLE.md/MODULES.md),
and `svc_artwork` already releases the *previous* handle itself, on its own schedule, every
time a new one is published: the new handle is written to `core_state` and
`ARTWORK_EVT_READY` published for it *before* the previous handle is released, so there is
never a window where `core_state` points at an already-released slot. Wiring the bus's own
drop-triggered `on_release` hook up *as well* would double-release the same handle the first
time the bus ever discarded an undelivered `ARTWORK_EVT_READY` event (overflow, unsubscribe,
or shutdown) — the hook would release a handle `svc_artwork` is also going to release later
when superseded. Only `svc_artwork` ever calls `core_pool_slot_release()`; a subscriber that
receives an `ARTWORK_EVT_READY` event must treat its handle as a convenience copy, not a
grant of ownership — lock it (`core_pool_slot_lock()`) immediately before use, every time,
and treat a failed lock (stale generation) as "no artwork right now," never as a corrupted
frame.

The mechanism above — an injected `on_release` hook, generic and domain-blind — remains real,
available machinery for a future topic whose payload genuinely *is* a releasable claim with
no other release path of its own. `TOPIC_ARTWORK` simply isn't that case, once `svc_artwork`
already owns a complete release schedule. It would still have closed two leaks a purely
prose-based rule could miss (a subscriber that unsubscribes with events still queued, and
shutdown with a non-empty queue) for any topic that does end up using it.

### 5.4 Topics

```
TOPIC_NET        link up/down, IP acquired, RSSI, provisioning state
TOPIC_TIME       SNTP synced (gates TLS)
TOPIC_PLAYBACK   track changed, position, play state, shuffle, repeat, device
TOPIC_SPOTIFY    session/auth state, API error, rate-limited
TOPIC_DENON      power, volume, mute, input, listening mode, link state
TOPIC_ARTWORK    artwork ready (handle + palette), artwork failed
TOPIC_SETTINGS   setting changed
TOPIC_POWER      active / dim / asleep, backlight level
TOPIC_OTA        available, progress, applied, failed
TOPIC_TOAST      transient user-facing message
TOPIC_DIAG       fault recorded
```

---

## 6. State management

### 6.1 Single writer per domain

```c
typedef struct {
    net_state_t       net;
    spotify_state_t   spotify;
    playback_state_t  playback;
    artwork_state_t   artwork;
    denon_state_t     denon;
    power_state_t     power;
    ota_state_t       ota;
} app_state_t;
```

Each domain has exactly **one** writer — the service that owns it. Nothing else may mutate
it, ever. Readers call `state_store_read(h, DOMAIN_PLAYBACK, &out)` which takes a mutex,
`memcpy`s, and releases. The lock is held for the duration of a struct copy and nothing
else — no I/O, no allocation, no callbacks under the lock.

The UI therefore always renders from a private, internally consistent snapshot. It can
never observe a half-updated struct, and it never holds a pointer into shared memory.

### 6.2 Every domain carries its own honesty

```c
typedef struct {
    link_status_t status;      /* OK | STALE | RECONNECTING | UNAVAILABLE */
    int64_t       updated_us;  /* monotonic */
    uint32_t      version;     /* bumped on every write */
} domain_meta_t;
```

This is what lets the UI tell the truth. A premium device does not show a confident volume
number that is thirty seconds out of date — it shows the number, dimmed, because
`status == STALE`. Getting this into the type system from day one costs nothing and is
nearly impossible to retrofit.

### 6.3 Optimistic intents

The gap between tapping *pause* and Spotify's API confirming it is 200–600 ms. Waiting for
confirmation feels broken. So:

1. UI posts `INTENT_PAUSE` and immediately renders the paused state, flagged `pending`.
2. `svc_spotify` executes, then force-polls at +250 ms and +1 s to converge quickly.
3. If the server agrees, `pending` clears silently.
4. If the server disagrees or the call fails, state snaps back **with a short animation**
   and a toast. Never a silent revert — a control that lies is worse than a slow one.
5. A pending intent that goes unconfirmed for 3 s is abandoned and reconciled.

Pending state lives in the *view model*, not the state store. The store only ever holds
what we believe the server actually reports. That separation is what keeps reconciliation
comprehensible.

### 6.4 FSMs

Anything with more than three states gets a table-driven FSM in `core_fsm`, in its own
file, testable on the host without an ESP32:

`net_wifi` · `spotify_session` · `denon_link` · `ota` · `power` · `provisioning`

Every transition is logged as `[fsm:denon_link] CONNECTED --(EVT_SOCKET_EOF)--> BACKOFF`.
When the panel misbehaves in six months, this log is the entire debugging story.

---
