# Networking Strategy

Deliverable §10. Split out of [PLATFORM.md](PLATFORM.md), which now covers flash layout,
OTA, and configuration (§8-9).

Two external systems, with opposite constraints:

- **Spotify Web API** — cloud, TLS, OAuth, rate-limited, **no push channel**. Polling is the
  only option, so the design is built around hiding that rather than pretending otherwise.
- **Denon AVR-X3800H** — local, unauthenticated, instant, but the good transport accepts
  **one client device-wide**. See [adr/0003](adr/0003-denon-dual-transport.md).

---

## 1. Connectivity

### 1.1 Connectivity supervisor

```
        ┌──────┐
        │ BOOT │
        └──┬───┘
           │ no credentials
           ├──────────────────► PROVISIONING ──┐
           │ credentials                        │ saved
           ▼                                    │
      CONNECTING ◄──────────────────────────────┘
           │ got IP
           ▼
      ┌────────┐  SNTP ok   ┌────────┐
      │ ONLINE │◄───────────┤ SYNCING│
      └───┬────┘            └────────┘
          │ link lost / DNS fail / N consecutive API errors
          ▼
      DEGRADED ──► RECONNECTING ──► (back to CONNECTING)
```

- Backoff is exponential **with jitter**, capped at 60 s. Never a busy retry loop.
- `DEGRADED` is a first-class state, not an error: the UI stays fully interactive, renders
  the last known state marked `STALE`, and keeps local controls (brightness, screens) live.
  A controller that goes blank because WiFi hiccuped feels broken; one that says
  "Reconnecting…" over dimmed-but-present content feels solid.
- **SNTP gates everything TLS.** Without a valid clock, certificate validation fails with a
  confusing error. `svc_spotify` waits for `TOPIC_TIME` before its first request.

### 1.2 Provisioning — two strictly separate stages

**These cannot be combined, and an earlier draft of this document got it wrong.** While the
phone is joined to the device's SoftAP it has **no route to the internet**, so it cannot
reach `accounts.spotify.com` to authorise anything. Any design that reuses the captive
portal session for OAuth fails on every normal home network.

**Stage 1 — WiFi, over SoftAP (no internet needed).**
Device shows an on-screen QR code and AP name → phone joins the device AP → captive portal
lists scanned networks → credentials saved → **SoftAP torn down** → device joins the home
LAN and reaches `ONLINE`.

**Stage 2 — Spotify, over the home LAN (both parties online).**
Only starts once the device is on the LAN with SNTP synced.

#### The redirect problem, and the decision

Spotify requires `redirect_uri` to be **registered in advance and matched exactly**, and it
must be `https://` — the only `http://` exception is loopback (`127.0.0.1`, `[::1]`). Our
device has a dynamic LAN IP and no public DNS name, so the obvious approaches all fail:

| Approach | Why it fails |
|---|---|
| `http://<device-lan-ip>/callback` | not registrable — IP changes; plain http rejected |
| `http://denon-controller.local/cb` | mDNS name is fine on the LAN, but Spotify rejects non-loopback http |
| A bounce page on a domain we control | a real, resolving domain is something a third party could genuinely receive the code through in transit |
| RFC 8628 device authorization flow | Spotify does not implement it |

**Decision: register a loopback redirect_uri and paste the callback URL manually, over the
UART debug console — revised from an earlier draft of this document, which described a
bounce-page design instead.** `http://127.0.0.1:<port>/callback` is registered once with
Spotify; nothing needs to actually listen on that port — the redirect only ever needs to
reach the browser's own address bar, never a live server.

```
device (console)         any browser                Spotify
  │  `spotify pair` prints    │                          │
  │  the authorize URL        │                          │
  │  (state = nonce)          │                          │
  ├───────────────────────────►                          │
  │                           ├────────authorize────────►│
  │                           │◄──302 to http://127.0.0.1:PORT/cb?code&state
  │                           │  (connection fails -- nothing is listening,
  │                           │   but the address bar keeps the full URL)
  │  user pastes that URL into `spotify code <url>`       │
  │  device parses code/state, validates state, PKCE exchange over TLS
```

- `spotify pair` generates the PKCE `code_verifier`/`code_challenge` and a `state` nonce
  (RAM only), and prints the authorize URL for the user to open in **any** browser, on any
  device — it does not need to be on the device's LAN at all, since no bounce page or
  on-device listener is in the loop.
- The user copies the full address-bar URL after Spotify's redirect (even though the page
  itself fails to load) and pastes it into `spotify code <url>`, which parses `code`/`state`
  as query parameters and validates `state` against the nonce `spotify pair` generated —
  rejecting a mismatch outright, no exchange attempted.
- **The code is safe by construction, same as the withdrawn bounce-page design.** This is
  PKCE: the authorization code is useless without the `code_verifier`, which never leaves
  the device. Unlike a bounce page, there is no third party in the loop at all — the code
  never transits anything but Spotify and the user's own browser.

**This is not a straight downgrade.** Removing the on-device HTTP listener a bounce-page
design would have needed removes an entire threat class (another host on the LAN racing the
redirect), and the console requires physical/USB access — a strictly stronger boundary than
"same WiFi network." See
[adr/0009](adr/0009-manual-code-paste-over-bounce-page.md) for the full reasoning, including
why a loopback address was chosen over registering an arbitrary HTTPS domain.

**Consequence:** setup is still two passes, not one — WiFi first (Stage 1), Spotify paired
afterwards from the console (Stage 2), inherent to the constraint in §1.2's own split above,
not a design choice. A device that works as a Denon controller immediately, with Spotify
paired afterwards, is a better first-run experience than one that blocks on a cloud account.
The manual-paste step is cruder than a QR handoff would have been — a deliberate trade
against building a bounce page or an on-device listener for a phase that has no on-screen UI
to render a QR code on anyway (no `hal_display`/LVGL until Phase 5).

### 1.3 Spotify

**Transport.** HTTPS to `api.spotify.com`, plus `i.scdn.co` for artwork. No push channel
exists in the Web API — polling is the only option and the architecture is built around
that constraint rather than pretending otherwise.

**Auth — OAuth 2.0 Authorization Code with PKCE.** The Implicit Grant is deprecated and
Client Credentials cannot access user playback, so PKCE is the only viable flow for a
device with no secret storage guarantee.

- Setup: `spotify pair` (the UART debug console, §1.2 above) generates `code_verifier`/
  `code_challenge`/`state` and prints the authorize URL for the user to open in any browser.
  Spotify redirects the browser to a registered loopback address
  (`http://127.0.0.1:<port>/callback`) that nothing listens on — the connection fails, but
  the address bar keeps the full URL. The user pastes that URL into `spotify code <url>`,
  which validates `state` before attempting the PKCE exchange. No on-device HTTP listener,
  no bounce page.
- **Refresh tokens rotate.** Spotify's PKCE flow returns a *new* refresh token on many
  refreshes. Failing to persist it is the single most common way these integrations die
  after a few weeks. The token write is committed to NVS **before** the new access token is
  used, so a power cut mid-refresh cannot strand us.
- Refresh proactively at 80 % of `expires_in`, not on 401.
- Unrecoverable auth failure (revoked, password changed) → `spotify_session` FSM enters
  `REAUTH_REQUIRED` → reported via `spotify status` on the UART console until Phase 6 gives
  it an on-screen home; re-pairing is `spotify pair`/`spotify code` again, same as first
  setup. Recoverable without a factory reset.

**Polling policy** (`spotify_poll_policy.c`, pure and host-tested):

| Condition | Interval |
|---|---|
| Playing, screen awake | 5 s |
| Playing, screen asleep | 30 s (keeps wake-on-playback working) |
| Paused, awake | 10 s |
| Paused, asleep | 60 s |
| After a user intent | force poll at +250 ms, +1 s |
| HTTP 429 | honour `Retry-After` exactly |
| Consecutive errors | exponential backoff to 120 s |

**Progress is interpolated locally, not polled.** On each poll we store
`(progress_ms, position_at_us)` from the monotonic clock. The UI computes
`progress_ms + (now_us - position_at_us)/1000` every frame while `is_playing`. That gives a
perfectly smooth 60 fps progress bar from a 5 s poll — the difference between "premium
device" and "web dashboard". Correction on each poll is applied as a short ease, never a
visible jump. See [adr/0004](adr/0004-local-progress-interpolation.md).

**Rate limiting.** Spotify's limits are a rolling window and are not published precisely.
A client-side token bucket sized well under any plausible limit, plus strict `Retry-After`
compliance, plus `ETag`/`If-None-Match` where the endpoint supports it. Artwork is fetched
**only when the track ID changes**, never per poll.

**Endpoints used:** `GET /v1/me/player` · `PUT /v1/me/player/play|pause` ·
`POST /v1/me/player/next|previous` · `PUT /v1/me/player/shuffle|repeat|volume` ·
`GET /v1/me/player/devices`.

**API drift is a multi-year risk.** All JSON parsing is confined to `spotify_parse.c`,
which maps responses onto our own `core_types` structs. No Spotify field name appears
anywhere else in the codebase. When Spotify changes something, one file changes, and its
host tests run against captured fixtures in `test/host/fixtures/`.

### 1.4 Denon AVR-X3800H

**Two transports behind one injected interface** (`denon_transport_if`):

| | Telnet :23 | HTTP :8080 |
|---|---|---|
| Push events | **Yes** — instant | No, polling only |
| Latency | ~20 ms | 300 ms–1 s |
| Concurrency | **One client, device-wide** | Unlimited |
| Endpoints | raw ASCII, `\r`-terminated | `/goform/formiPhoneAppDirect.xml?CMD`, `/goform/AppCommand.xml` |

**The exclusivity problem.** The AVR accepts exactly one telnet client at a time. Home
Assistant, the official Denon app, or another controller will take it — and then we are
locked out with no error beyond a refused connection or a silent EOF.

Strategy (`auto` mode, the default):

1. Try telnet. On success, use it for both commands and push events — this is the good path
   and it feels instantaneous.
2. On refusal or EOF, fall back to HTTP immediately, poll `AppCommand.xml` at 2 s, and
   surface link state as `HTTP (fallback)` in diagnostics — **not** as an error to the user,
   because everything still works.
3. Retry telnet every 60 s with jitter. Promote back on success.
4. Never hold the telnet socket when the panel is asleep and the AVR is off — release it so
   other controllers in the house can work. Being a good citizen on a shared resource is
   part of the product.

See [adr/0003](adr/0003-denon-dual-transport.md).

**Command coalescing.** Dragging a volume dial produces ~60 changes/second. Sending all of
them floods the AVR, which starts dropping commands and reporting stale values — the volume
then visibly fights the user's finger. So `svc_denon`'s outbound queue coalesces by command
class: for absolute-value commands (`MV`, brightness-like), only the newest pending value
survives, and transmission is rate-limited to one every **100 ms**. Toggle and discrete
commands (`PW`, `MU`, `SI`) are never coalesced and never reordered.

**Protocol encoding** is isolated in `denon_codec.c` — pure functions, fully host-tested:

```
"MV525"  <->  52.5 dB      (the half-step 3-digit form)
"MV52"   <->  52.0 dB
"PWON" / "PWSTANDBY"       power
"MUON" / "MUOFF"           mute
"SIMPLAY" / "SIBD" / …     input select
"MSDIRECT" / "MSSTEREO" …  listening mode
```

The 2-vs-3-digit `MV` ambiguity is exactly the kind of thing that produces a 10 dB volume
error at 2 a.m. It gets a test table with every boundary case.

**Discovery.** SSDP (`urn:schemas-denon-com:device`) then mDNS, then manual IP entry in
Settings. The resolved host is cached in NVS; re-discovery happens only when the cached
host stops responding, so a DHCP lease change self-heals without user action.

**Implemented as SSDP + manual only, no mDNS rung** — a deliberate scope trim, not an
oversight: no Denon-specific technical reason for mDNS was found anywhere (no "some models
don't respond to SSDP" caveat), a broad `ssdp:all` search filtered by a case-insensitive
"denon" substring match found a real AVR-X3800H reliably in testing, and manual entry (the
debug console's `denon host <ip>`) is the ultimate fallback regardless.

**Capability probe at startup.** `GET /goform/Deviceinfo.xml` records model, firmware, and
available zones/inputs. Denon firmware updates have historically changed these endpoints;
probing means we detect a mismatch and degrade cleanly rather than misbehaving.

### 1.5 The volume question — a real design decision

The AVR-X3800H is *itself* a Spotify Connect endpoint. So "Spotify volume" and "Denon
master volume" are frequently two controls on the same physical signal path, and exposing
both is how you build a confusing product.

**Decision:** the primary volume control is bound to the **Denon master volume**. It is the
real analog attenuator, it has instant feedback over telnet, it works for every input (not
just Spotify), and it is what the physical remote controls.

Spotify device volume is exposed **only** when `GET /v1/me/player` reports an active device
that is *not* the AVR — i.e. the user is casting to a different speaker. In that case the
volume control silently retargets and the UI labels the output device. One control, always
the correct one, no mode switch for the user to understand.

### 1.6 HTTP client policy

`net_http` takes an injected policy struct rather than hardcoding behaviour:

```c
typedef struct {
    uint16_t connect_timeout_ms;   /* 5000  */
    uint16_t request_timeout_ms;   /* 8000  */
    uint8_t  max_retries;          /* 2     */
    uint16_t backoff_base_ms;      /* 500   */
    bool     retry_on_5xx;         /* true  */
    bool     honour_retry_after;   /* true  */
} http_policy_t;
```

Spotify, OTA, and artwork each get different policies. Timeouts are **always** set — an
un-timed socket read on a network service is how a wall panel freezes for twenty minutes.
