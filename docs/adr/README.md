# Architecture Decision Records

One file per load-bearing decision. Each records the context, the decision, what we gave up,
and — most importantly — the alternatives we rejected and why.

A decision belongs here if reversing it later would be expensive, or if a reasonable
engineer joining the project would otherwise be tempted to "fix" it.

| # | Decision | Status |
|---|---|---|
| [0001](0001-intents-down-state-up.md) | Intents flow down, state flows up; UI and services never touch | Accepted |
| [0002](0002-c-with-handle-based-di.md) | C11 with opaque handles and injected dependencies, no C++ | Accepted |
| [0003](0003-denon-dual-transport.md) | Dual Denon transport: telnet primary, HTTP fallback | Accepted |
| [0004](0004-local-progress-interpolation.md) | Interpolate playback progress locally from a slow poll | Accepted |
| [0005](0005-assets-in-app-image.md) | Compile fonts and icons into the app image | Accepted |
| [0006](0006-staged-security.md) | Staged security: build it now, enable it in stages | Accepted |
| [0007](0007-denon-volume-is-primary.md) | Denon master volume is the primary volume control | Accepted |
| [0008](0008-own-the-lvgl-glue.md) | Own the LVGL/task glue; no `esp_lvgl_port` | Accepted |

## Template

```markdown
# NNNN — Title

Status: Proposed | Accepted | Superseded by ADR-NNNN
Date: YYYY-MM-DD

## Context
What forces are at play? What did we learn that made this a decision rather than a default?

## Decision
What we are doing, stated plainly.

## Consequences
What this costs us, and what it makes easy.

## Alternatives rejected
Each with the reason. This section is the one future maintainers actually need.
```
