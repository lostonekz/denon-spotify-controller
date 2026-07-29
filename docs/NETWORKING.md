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
| `http://127.0.0.1:PORT/callback` | loopback resolves to **the phone**, not the device |
| RFC 8628 device authorization flow | Spotify does not implement it |

**Decision: a static HTTPS bounce page on the same host that already serves OTA manifests.**

```
device                phone browser              Spotify            bounce page
  │  QR: authorize URL     │                        │                     │
  │  state = nonce|lan_ip  │                        │                     │
  ├───────────────────────►│                        │                     │
  │                        ├───────authorize───────►│                     │
  │                        │◄──302 code+state───────┤                     │
  │                        ├────────────────────────┼────────────────────►│
  │                        │◄──302 to http://<lan_ip>:PORT/cb?code&state──┤
  │◄──GET /cb?code&state───┤                        │                     │
  │  verify nonce, PKCE exchange over TLS           │                     │
```

- `https://ota.example.com/spotify-callback` is registered once with Spotify. It is a
  **static page** — no server logic, no state, just a few lines of JS that read `code` and
  `state` from the query string and redirect the browser onward.
- The device embeds its own LAN address in the `state` parameter it generates. `state`
  already exists for CSRF (SECURITY.md §5), so this adds no new mechanism — and the device
  validates the nonce on return, so a tampered `state` is rejected.
- **The code transiting the bounce page is safe by construction.** This is PKCE: the
  authorization code is useless without the `code_verifier`, which never leaves the device.
  The bounce page never sees a token and never needs to be trusted.
- Reuses infrastructure we already need for OTA. No server to operate, no per-device
  registration, no dynamic DNS.

Fallback if the bounce host is unreachable: Settings offers manual entry of a code the user
copies from the browser. Ugly, but it means a hosting outage cannot permanently block setup.

The phone must be on the same LAN as the device for the final hop. This is checked before
showing the QR, and the failure is explained on screen rather than silently timing out.

**Consequence:** setup is two passes, not one. That is inherent to the constraint, not a
design choice — and the honest version is a QR code and a "Connect Spotify" step the user
can also skip and do later from Settings. A device that works as a Denon controller
immediately, with Spotify added afterwards, is a better first-run experience than one that
blocks on a cloud account.

### 1.3 Spotify

**Transport.** HTTPS to `api.spotify.com`, plus `i.scdn.co` for artwork. No push channel
exists in the Web API — polling is the only option and the architecture is built around
that constraint rather than pretending otherwise.

**Auth — OAuth 2.0 Authorization Code with PKCE.** The Implicit Grant is deprecated and
Client Credentials cannot access user playback, so PKCE is the only viable flow for a
device with no secret storage guarantee.

- Setup: device generates `code_verifier`, displays a **QR code** encoding the authorize
  URL with `redirect_uri` pointing at the device's own LAN address. The user authorises on
  their phone; the phone's browser redirects to the device; a tiny local HTTP listener
  captures the code and exchanges it.
- **Refresh tokens rotate.** Spotify's PKCE flow returns a *new* refresh token on many
  refreshes. Failing to persist it is the single most common way these integrations die
  after a few weeks. The token write is committed to NVS **before** the new access token is
  used, so a power cut mid-refresh cannot strand us.
- Refresh proactively at 80 % of `expires_in`, not on 401.
- Unrecoverable auth failure (revoked, password changed) → `spotify_session` FSM enters
  `REAUTH_REQUIRED` → the UI shows a re-pair QR code on the Now Playing screen. Recoverable
  without a factory reset.

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
