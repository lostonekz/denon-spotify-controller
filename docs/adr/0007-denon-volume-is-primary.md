# 0007 — Denon master volume is the primary volume control

Status: Accepted
Date: 2026-07-28

## Context

The requirements list both "Volume (Spotify if available)" under Spotify controls and
"Volume" under Denon controls. Implemented literally, that is two volume controls on one
screen.

The complication is that the AVR-X3800H is **itself a Spotify Connect endpoint**. In the
expected setup — Spotify playing through the Denon — "Spotify device volume" and "Denon
master volume" are two controls on the same physical signal path, in series. Adjusting one
changes the same loudness the other reports differently. Exposing both is how you build a
product where the user has to think about which volume they mean.

There is also a real asymmetry in quality:

| | Denon MV | Spotify device volume |
|---|---|---|
| Feedback | ~20 ms over telnet | 300–600 ms, poll-confirmed |
| Scope | every input, not just Spotify | Spotify only |
| Matches the physical remote | yes | no |
| Is the real analog attenuator | yes | no |

## Decision

**One volume control in the UI.** It binds to the Denon master volume by default.

Spotify device volume is used **only** when `GET /v1/me/player` reports an active device
that is not the AVR — i.e. the user is casting to a different speaker, where the Denon MV
would do nothing. In that case the same control silently retargets and the UI labels the
output device ("Kitchen Speaker").

The user never chooses a mode. The control is always bound to the thing that actually
affects what they are hearing.

The safety clamp (`volume_limit_max`) is enforced in `svc_denon` at the protocol boundary,
not in the UI widget, so it holds regardless of which screen or gesture issued the intent.

## Consequences

**Good.** One control, always correct, no concept for the user to learn. This is the
difference between a consumer product and an engineering dashboard, and it is exactly the
kind of decision the brief's design goals are asking for.

**Good.** The common case gets the good transport: instant telnet feedback and a dial that
tracks the finger without lag.

**Good.** Turning the physical Denon remote's volume knob updates our display within ~20 ms,
because telnet pushes it. The panel stays in sync with the room.

**Cost.** A user who deliberately wants to attenuate at the Spotify layer while leaving the
AVR alone cannot. This is an unusual thing to want and a bad thing to do — double
attenuation costs dynamic range — so we are comfortable not supporting it.

**Cost.** The retarget logic needs the active-device ID from `/v1/me/player`, and device
identification must be reliable. If we cannot confidently tell whether the active device is
the AVR, we default to Denon MV, which is the safe answer.

## Alternatives rejected

**Two separate volume controls.** The literal reading of the requirement. Rejected: it
exposes an implementation detail as a user-facing concept, and the two controls fight each
other in the default setup.

**Spotify volume as primary.** Rejected: slower feedback, works only for Spotify, does
nothing when the user is watching TV through the same AVR, and it desynchronises from the
physical remote.

**A user-selectable preference for which volume the dial controls.** Rejected as the wrong
shape of solution — it makes the user resolve an ambiguity we are better placed to resolve
automatically, every time they use the device. If field experience shows the auto-detection
is unreliable, this becomes the fallback, not the default.

**A single control that changes both proportionally.** Rejected: unpredictable combined
gain, and it makes the state impossible to reason about when another controller changes one
of them.
