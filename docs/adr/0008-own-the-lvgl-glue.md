# 0008 — Own the LVGL/task glue; do not use `esp_lvgl_port`

Status: Accepted
Date: 2026-07-28

## Context

Something must connect LVGL to the hardware: create the `lv_display`, allocate and register
draw buffers, provide the 2 ms tick, implement the flush callback against
`esp_lcd_rgb_panel`, feed touch input, and run `lv_timer_handler()` on a task with a lock
that keeps other tasks out of the LVGL API.

Espressif ships `esp_lvgl_port`, which does exactly this and is the path of least
resistance. It is a well-maintained component and for most projects it is the right answer.

Two things make this project different:

1. **The top project risk lives in this code.** The ST7701 is on the parallel RGB bus, so
   framebuffer strategy, bounce-buffer sizing, and VSYNC handling are the direct levers on
   RISKS R1 (PSRAM bandwidth contention). Phase 0 exists specifically to measure and tune
   them.
2. **The threading contract is architectural here, not incidental.** ADR-0001 requires that
   only the UI task touches LVGL, that it never blocks, and that state arrives by copied
   events. A general-purpose port layer offers a lock so *any* task can call LVGL safely —
   which is a solution to a problem we have deliberately designed away.

## Decision

We own the glue. Roughly 200 lines across `hal_display` and `ui_app`:

- `hal_display` — ST7701 init sequence, `esp_lcd_rgb_panel` configuration, framebuffer and
  bounce-buffer allocation, VSYNC callback.
- `ui_app` — `lv_display` creation, flush callback, the `esp_timer` tick, and the UI task
  loop.

There is **no LVGL lock**. Instead the invariant is enforced structurally: no task other
than `ui` can reach an `lv_*` symbol, because no other component lists `lvgl` in its CMake
`REQUIRES` (ARCHITECTURE §1.4). The rule is checked by the linker rather than honoured at
runtime.

`esp_lvgl_port` remains a perfectly good fallback if this turns out to be a poor use of
effort — the interface between `hal_display` and `ui_app` is small enough to swap.

## Consequences

**Good.** Phase 0 can tune framebuffer count, refresh rate, bounce-buffer size, and
partial-vs-direct refresh mode by editing our own code, with no wrapper to work around and
no upstream configuration surface to discover. Given that Phase 0 exists to answer R1, this
is the deciding factor.

**Good.** No runtime lock means no lock to forget, no priority inversion between the UI task
and a network task, and no possibility of a service blocking on the display mutex. The
locking discipline that a port layer provides is replaced by a stronger structural guarantee.

**Good.** One less dependency whose version must be tracked against LVGL 9.x and ESP-IDF.

**Cost.** ~200 lines of hardware glue we write, test, and maintain — including the RGB
panel's genuinely fiddly parts (VSYNC-synchronised buffer swap, cache invalidation for
bounce buffers). This is exactly the code where subtle bugs produce intermittent visual
artifacts, so it gets written during Phase 0 when it has our full attention, not squeezed in
alongside UI work.

**Cost.** Upstream improvements and bug fixes to `esp_lvgl_port` do not reach us. Accepted:
the component's value is mostly in the generality we are not using.

## Alternatives rejected

**Use `esp_lvgl_port` as-is.** Faster to start and the sensible default for most projects.
Rejected because it abstracts precisely the layer we most need to measure and tune, and it
provides a threading model (any task may call LVGL under a lock) that is weaker than the one
ADR-0001 already gives us for free.

**Use it in Phase 0 to get pixels quickly, then replace it.** Superficially the best of both
— but Phase 0's entire purpose is to produce trustworthy measurements of the display path,
and measuring a layer we intend to discard produces numbers that do not transfer. If we are
going to own it, own it before the numbers matter.

**Own the task loop but use the component's panel setup.** Rejected: the panel configuration
*is* the part that matters for R1. That split keeps the cost and gives away the benefit.
