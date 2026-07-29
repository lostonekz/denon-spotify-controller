# 0001 — Intents flow down, state flows up

Status: Accepted
Date: 2026-07-28

## Context

The stated requirements are explicit that UI logic and networking must never mix, and that
everything should be event-driven. Those are easy to agree with and hard to hold onto:
the pressure to add "just one" direct call from a button handler into the Spotify client is
constant, and each one is individually reasonable. Ten of them later, the UI cannot be
tested, the network layer cannot be changed, and every screen has its own retry logic.

A wall-mounted device also has a specific hazard: the UI task drives the display. Any
blocking call that leaks into it freezes the entire product, and with no console attached
that is indistinguishable from dead hardware.

## Decision

Exactly two channels between the UI and the rest of the system, both mediated by `core/`:

- **Intents flow down.** The UI posts `intent_t` values to a single intent queue. It never
  calls a service function.
- **State flows up.** Services publish immutable snapshots on the event bus. The UI reads
  its own private copy and renders it.

The UI includes no service header. Services include no LVGL header. Both include only
`core/`. This is enforced by CMake `REQUIRES` lists and by a CI include linter, not by
discipline alone.

Intents use a *single* queue so that the global ordering of user actions is preserved —
"next" then "pause" must not arrive reordered. State uses topic-based pub/sub with
per-topic backpressure policies, because for state only the newest value matters.

## Consequences

**Good.** The UI is testable without a network. Services are testable without a display.
A blocking call cannot migrate into the UI task because the UI has nothing blocking to call.
Adding a second control surface later (MQTT, Home Assistant, a physical encoder) is a new
intent producer, not a refactor — this is why the Home Assistant bridge in the roadmap is a
small job rather than a large one.

**Cost.** More indirection than a direct call. A user action traverses queue → dispatcher →
service → event → UI, which is more code and more places to look when debugging. We accept
that, and we mitigate it by logging every intent and every FSM transition, so the trace is
readable end to end.

**Cost.** Optimistic UI becomes mandatory rather than optional. A round trip through the bus
plus a Spotify API call is 200–600 ms, which feels broken if rendered honestly. Pending
state therefore lives in the view model, with explicit reconciliation. That is real
complexity we are choosing to take on — see ARCHITECTURE.md §6.3.

## Alternatives rejected

**Direct calls with a mutex.** Simplest and most common. Rejected because it puts networking
latency on the UI task by construction, and because it makes the "never block the UI" rule
unenforceable — every new call site is a fresh opportunity to violate it.

**Callbacks from services into UI code.** Rejected because the callback runs on the
service's task, so every callback body needs its own thread-safety reasoning and its own
hand-off to the LVGL task. That reasoning gets done correctly the first three times.

**A shared global state struct with a mutex, read directly by the UI.** Rejected because
readers then hold pointers into memory that a writer can change mid-render, and because
"who is allowed to write this field" degrades into "everyone" within a year. The snapshot
copy costs a `memcpy` and removes the entire class of problem.

**One event queue for everything, including intents.** Rejected because state events must be
droppable (newest wins) while intents must not be. Merging them forces one policy on two
kinds of message with opposite requirements.
