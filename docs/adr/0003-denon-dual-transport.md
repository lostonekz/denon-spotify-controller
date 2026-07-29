# 0003 — Dual Denon transport: telnet primary, HTTP fallback

Status: Accepted
Date: 2026-07-28

## Context

The AVR-X3800H exposes two control surfaces:

| | Telnet :23 | HTTP :8080 |
|---|---|---|
| Push events | yes, ~20 ms | none — polling only |
| Concurrency | **one client, device-wide** | unlimited |
| Format | raw ASCII, `\r`-terminated | `/goform/formiPhoneAppDirect.xml?CMD`, `AppCommand.xml` |
| Documented | semi — the IP command chart | no, and it has changed across firmware |

The decisive fact is the telnet exclusivity. The receiver accepts exactly **one** telnet
connection at a time. Home Assistant, the official Denon app, or any other controller on the
network will take it, and we then get a refused connection or a silent EOF with no
explanation. This is not an edge case — it is the normal state of a house that already has
home automation.

Telnet is also the only transport with push events, and push events are what make the volume
control feel like hardware rather than a web page.

## Decision

Both transports, behind one injected interface (`denon_transport_if`), with automatic
selection. Default mode `auto`:

1. Attempt telnet. On success use it for commands and events.
2. On refusal or EOF, fall back to HTTP immediately: commands via `formiPhoneAppDirect.xml`,
   state via `AppCommand.xml` polled at 2 s.
3. Retry telnet every 60 s with jitter; promote on success.
4. **Release the telnet socket whenever the panel is asleep and the AVR is off**, so other
   controllers in the house can use it.

Fallback is surfaced in diagnostics as `HTTP (fallback)` but **not** as a user-facing error,
because in fallback the product still works — it is merely less immediate.

Settings expose `denon_transport_pref` (`auto` / `telnet` / `http`) for users who want to
guarantee they never contend for the socket.

## Consequences

**Good.** The device works in a house that already runs Home Assistant, which is the likely
deployment. It degrades on a dimension the user can feel (latency) rather than a dimension
they cannot (it stops working). Step 4 makes us a good citizen on a shared resource, which
matters because the alternative is our panel breaking someone's existing automation.

**Cost.** Two transport implementations to write, test, and keep in sync, plus a promotion
state machine. Roughly a week of the Denon phase.

**Cost.** State semantics differ between transports: telnet reports changes made by the
physical remote instantly, HTTP finds them up to 2 s later. The `domain_meta_t.status`
field exists partly to let the UI be honest about this.

**Mitigated.** The undocumented HTTP interface is a drift risk (see RISKS R5). Confined to
`denon_http.c` and probed at startup via `Deviceinfo.xml`.

## Alternatives rejected

**Telnet only.** Simplest, best feel. Rejected because it fails completely and
inexplicably whenever another controller holds the socket, which we should assume is often.
A premium device cannot have a failure mode of "stops working when the user installs
something else".

**HTTP only.** Robust, no contention, no socket lifecycle. Rejected because the 300 ms–1 s
latency and absence of push events is exactly the difference between a Sonos-grade control
and a web dashboard. Volume feedback in particular has to be immediate or the control feels
broken.

**Telnet with an aggressive reconnect loop to reclaim the socket.** Rejected on principle:
it turns the device into a bad actor that fights the user's other software for a shared
resource, and the observable result is that both controllers become unreliable.

**HEOS on port 1255 as the primary transport.** It is multi-client and would sidestep the
exclusivity problem entirely. Rejected *for now* because it covers HEOS/streaming control
rather than the AVR functions we need (master volume, input select, listening mode, power).
Listed in the roadmap as a complementary addition, not a replacement.
