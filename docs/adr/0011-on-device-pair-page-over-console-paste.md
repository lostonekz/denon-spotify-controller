# 0011 — An on-device pairing page, reached by QR from the panel

Status: Accepted
Date: 2026-08-05
Supersedes: [0009](0009-manual-code-paste-over-bounce-page.md)

## Context

[ADR-0009](0009-manual-code-paste-over-bounce-page.md) chose a UART-console paste for the
Spotify OAuth code and rejected everything else. Two of its three reasons have expired.

**Its own stated precondition is gone.** 0009 rejected "waiting for Phase 5's on-screen UI to
do a proper QR handoff" explicitly and only because Phase 4 had no screen to render one
against. Phase 5 shipped a 480×480 panel, and LVGL 9.5 already vendors `lv_qrcode` plus
Nayuki's `qrcodegen` — enabling it is a Kconfig flip, not a new dependency.

**The console flow does not work reliably.** Live console interaction over this board's native
USB-CDC port proved unreliable from Windows — the same finding that already forced
`DSC_SPOTIFY_AUTOPAIR` into existence as a two-boot workaround (`main/CMakeLists.txt`
documents it). "Physical access is a stronger boundary" is only an advantage if the resulting
flow is usable, and for the person who owns this panel it frequently was not.

**And there is now a visible dead end.** The System screen reports "Spotify · Reauth needed"
and, until this change, offered no way to act on it — a problem stated in words on a
touchscreen with nothing to touch.

## Decision

The panel shows a QR encoding `http://<device-lan-ip>:8080/pair`. The phone opens that page,
which is served **by the device itself**, and which offers the real Spotify authorize URL as a
link plus a box to paste the failed redirect URL back into. The device does the PKCE exchange.

The listener's lifetime is **exactly the pair overlay's**: the overlay's `mount()` submits
`INTENT_DOMAIN_PAIRING`/START and its `unmount()` submits STOP. It does not run at boot, does
not run while the panel is idle, and cannot outlive the screen that started it. There is no
always-on state and therefore nothing to opt out of.

**What 0009 decided that this does NOT change:** the `redirect_uri` is still
`http://127.0.0.1:<port>/callback`, and nothing listens on it, on any device. 0009's central
finding — that a loopback redirect cannot be received by the device, and does not need to be —
still stands and is still what makes the flow work. The listener introduced here serves a
*different* URL on a *different* port and is never a redirect target. No third-party bounce
page, no domain we operate, nothing resolving in the authorization code's path. That part of
0009 is reaffirmed, not reversed.

**What is reversed:** 0009's "no on-device HTTP listener", and the matching claim in
[SECURITY.md](../SECURITY.md) that no listener exists outside Wi-Fi provisioning.

## Consequences

**Good.** Pairing and re-pairing are reachable from the panel by anyone standing at it, with
no serial console, no host toolchain, and no reflash. The System screen's reauth warning now
leads somewhere.

**Good, and new.** The PKCE `state` nonce is now cleared on both terminal transitions
(`spotify_service_session.c`). It previously lived forever — no expiry, not cleared on success,
not cleared on unpair — so a replayed `code`+`state` stayed acceptable indefinitely. That was
tolerable when only a USB console could submit one. It is not now, and fixing it also makes
SECURITY.md's existing claim that the nonce is single-use actually true, which it was not.

**Cost — a real new exposure, accepted deliberately.** While the overlay is open, any host on
the LAN can `GET /pair` and read the authorize URL, which contains the `client_id`, the
`redirect_uri`, the `code_challenge` and the **`state` nonce**. Such a host could complete
consent with *their own* Spotify account and `POST` a valid `code`+`state` back before the
legitimate user does, pairing this panel to the attacker's account. PKCE still protects the
other direction — nobody can steal *our* pairing or replay our code — and the window is
bounded to the seconds an overlay is open with a person standing in front of the panel. A
4-digit PIN rendered on the panel and required as a second form field would close this
outright and was specifically considered; the owner of this device declined it in favour of a
shorter flow, with the exposure understood. `esp_http_server` binds `INADDR_ANY` with no
per-netif option, so restricting the listener to the STA interface was not available either.
**This is the paragraph to revisit if this project ever ships to anyone else.**

**Cost.** The phone must be on the same LAN. 0009's flow worked from "any browser, any device";
this one does not. A phone on cellular, or on a guest/IoT VLAN with client isolation, will scan
the code fine and then fail with a browser timeout that says nothing useful — which is why the
overlay says so in words before you scan.

**Cost.** Consenting on the phone and then closing the overlay before pasting leaves the
session stuck in `AWAITING_CODE` until a reboot or a fresh pair. The console flow had the same
hole, but it is far easier to hit now. Logged loudly; a proper `PAIR_CANCELLED` FSM event is
recorded in [PRODUCTION_DEFERRED.md](../PRODUCTION_DEFERRED.md).

## Two architectural consequences worth stating explicitly

**A UI tap now synchronously starts an HTTP server on the `intent` task.** The overlay's
`mount()` submits `PAIRING_INTENT_START`; `app_wiring_ticks.c`'s dispatcher calls
`pair_http_start()`, which does `httpd_start()` plus `svc_spotify_begin_pair()` (mbedTLS
SHA-256 and a 512-byte URL buffer). That is a heavier thing than "hand an intent to a
service queue", which is what [ARCHITECTURE.md](../ARCHITECTURE.md) §1.1's intents-flow-down
diagram otherwise describes, and it is why that task's stack went 3 KB → 4 KB. Nothing about
the layering rules is violated — `main` may depend on everything — but a reader of §1 alone
would not expect the intent dispatcher to own a server's lifetime.

**Two HTTP servers can now exist at once.** `net_wifi`'s captive portal takes port 80 and
control port 32768; this one takes `CONFIG_DSC_PAIR_HTTP_PORT` / `..._CTRL_PORT` (8080 /
32769) for exactly that reason. They are *usually* mutually exclusive — the portal runs in
SoftAP mode during provisioning, this one needs a STA address — but a Wi-Fi drop during
pairing can start provisioning while this server is still up, against a total
`LWIP_MAX_SOCKETS` of 10. Hence `max_open_sockets = 3` and `lru_purge_enable` here, rather
than the stock config.

## Alternatives rejected

**Adding an `AUTHORIZED --PAIR_REQUESTED--> AWAITING_CODE` edge** to `spotify_session_fsm`, so
re-pairing a healthy session would keep the old token until a new one landed. Rejected:
nothing returns `AWAITING_CODE → AUTHORIZED`, so abandoning that re-pair would strand a
working device in "Pairing…" with polling dead until reboot. Fixing that properly needs a
`PAIR_CANCELLED` event and a `RESUMING` re-entry — more new FSM surface than this feature
warrants. Instead, `pair_http_start(force=1)` unpairs first and the panel says so on the
button, so the cost is visible before it is paid.

**A static HTTPS bounce page on a domain we control.** Still rejected, for exactly the reason
0009 gave: it puts a real, resolving third party in the authorization code's path. Nothing
about having a screen changes that.

**HTTPS on the device** (self-signed certificate plus mDNS `.local`). Rejected: it trains the
user to click through a browser certificate warning, which is a worse habit to build than the
LAN exposure above is a risk.

**A permanent listener.** Rejected: it is the broadest possible reading of the reversal above,
and unnecessary — pairing is a deliberate act, so the listener can be too.
