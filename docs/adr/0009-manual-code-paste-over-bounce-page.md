# 0009 — Manual code paste over a bounce-page for Spotify OAuth

Status: Accepted
Date: 2026-07-31

## Context

Spotify's OAuth Authorization Code + PKCE flow requires `redirect_uri` to be registered in
advance and matched exactly, and it must be `https://` — the only `http://` exception is a
literal loopback address (`127.0.0.1`, `[::1]`). The device has a dynamic LAN IP and no
public DNS name of its own, so there is no straightforward `https://` endpoint it can
register and actually receive traffic on. An earlier draft of [NETWORKING.md](../NETWORKING.md)
and [SECURITY.md](../SECURITY.md) worked around this with a static HTTPS bounce page hosted
on the same domain that already serves OTA manifests: the phone's browser would be redirected
there, and a few lines of client-side JS would forward `code`/`state` onward to the device
over the LAN via an on-device HTTP listener.

Two things changed since that draft. First, no `hal_display`/LVGL bring-up exists until
Phase 5, so there is nothing to render a QR code (or any other on-screen handoff) against —
the same situation Phase 3 hit with the Denon control UI, resolved there by extending the
UART debug console instead of waiting for a screen. Second, and more load-bearing: a bounce
page on a domain we control is still a real, resolving third party in the code's path — if
that domain, its DNS, or anything logging its requests is ever compromised or misconfigured,
it is a place the authorization code could genuinely be observed in transit, even though PKCE
makes the code alone useless without the `code_verifier`. Loopback addresses are exempted
from Spotify's `https://` requirement precisely because they never leave the requesting
machine — a fact that also means running a listener at `127.0.0.1` on the *device* would not
work: from the phone's browser, `127.0.0.1` resolves to the phone itself, not the device. This
project cannot get a working on-device HTTP listener via loopback and does not want a
real, resolving bounce-page domain in the loop.

## Decision

Register `http://127.0.0.1:<port>/callback` as the app's `redirect_uri` with Spotify.
Nothing listens on that port, on any device — the redirect only ever needs to reach the
browser's own address bar, never a live server.

The UART debug console drives the whole exchange:

1. `spotify pair` generates the PKCE `code_verifier`/`code_challenge` and a `state` nonce (RAM
   only), and prints the authorize URL for the user to open in any browser, on any device —
   it does not need to be on the device's LAN, since nothing beyond the browser itself is
   part of this flow.
2. The user logs in and approves on Spotify's own consent screen. Spotify redirects the
   browser to the registered loopback address with `?code=...&state=...`. The connection
   fails (nothing is listening), but the address bar still shows the full attempted URL with
   both query parameters intact.
3. The user copies that URL and pastes it into `spotify code <url>`, which parses `code` and
   `state` out of the query string and validates `state` against the nonce generated in step
   1 — rejecting a mismatch outright, with no exchange attempted — before performing the PKCE
   token exchange over TLS.

No bounce page, no on-device HTTP listener, and no third-party domain anywhere in the loop.

## Consequences

**Good.** Removes an entire threat class a bounce-page-plus-listener design would have
carried: another host on the LAN racing the redirect to the on-device listener. There is no
listener to race. The console additionally requires physical/USB access to the device — a
strictly stronger boundary than "same WiFi network," which is all the withdrawn design would
have required of an attacker. No hosting to operate, no per-device DNS, no dynamic-DNS
dependency for this flow.

**Cost.** Cruder than a QR handoff: the user has to notice their browser "failed to load" a
page is actually the expected outcome, then copy a URL by hand. This is a deliberate,
documented trade against building either a bounce page or an on-device listener for a phase
that has no screen to render anything nicer on anyway.

**Cost.** The flow depends on the user correctly copying the *entire* address-bar URL,
including the query string — a truncated paste fails cleanly (missing `code`/`state`) rather
than silently, but it is one more manual step than an automated redirect would need.

## Alternatives rejected

**A static HTTPS bounce page on a domain we control**, forwarding `code`/`state` to an
on-device HTTP listener over the LAN (the original design). Rejected: the domain and
everything in its request path is a real third party the code transits, which PKCE's
`code_verifier` protection was never meant to be the *only* safeguard against; removing that
party removes the threat class entirely rather than merely mitigating it, and this project has
no ongoing need to operate that page beyond OAuth setup.

**A loopback listener on the device itself.** Does not work: `127.0.0.1` from the phone
browser's perspective resolves to the phone, not the device. This is precisely why Spotify's
loopback exemption is safe to rely on here — nothing needs to listen for it to work.

**RFC 8628 device authorization flow** (the "enter this code on another device" pattern).
Rejected because Spotify does not implement it.

**Waiting for Phase 5's on-screen UI to do a proper QR handoff.** Rejected because nothing in
Phase 4's own exit criteria needs an on-screen UI, and blocking Spotify pairing on Phase 5
would delay a phase that is otherwise fully buildable and testable now — the same reasoning
Phase 3 used for its own UART debug console instead of waiting on `hal_display`.
