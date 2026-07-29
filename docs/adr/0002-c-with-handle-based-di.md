# 0002 — C11 with opaque handles and injected dependencies

Status: Accepted
Date: 2026-07-28

## Context

The requirements ask for C, no global variables, dependency injection where possible, a
public interface per module, and no dynamic allocation after startup. Those four constraints
interact, and satisfying them accidentally is unlikely — the idiom needs choosing up front.

(The fourth was later refined into **bounded allocation domains** — see
[MEMORY.md](../MEMORY.md) §2 — because the absolute form is not achievable on ESP-IDF, whose
network stacks allocate continuously. The handle-based idiom below is what makes the refined
version enforceable, so this ADR stands unchanged.)

The default C embedded idiom is a file-static struct plus `module_init()` / `module_do_x()`.
It is compact and it is what most ESP-IDF examples do. It also produces exactly the codebase
this project is trying to avoid: one instance, no seams for testing, and initialisation
order encoded implicitly in the order of calls in `app_main`.

## Decision

Every module is an **opaque handle** constructed from an explicit config struct that carries
its dependencies:

```c
typedef struct spotify_service_s *spotify_service_h;
esp_err_t spotify_service_create(const spotify_service_cfg_t *cfg, spotify_service_h *out);
```

- The struct definition lives in the `.c` file. Callers cannot reach inside.
- Dependencies (`event_bus_h`, `http_client_h`, `settings_h`, `time_source_t`) arrive
  through `cfg`. A module never reaches out for a global to find its collaborators.
- Anything with more than one plausible implementation is injected as a **vtable struct**
  (`denon_transport_if`, `http_client_if`, `kv_store_if`, `time_source_t`), so tests
  substitute fakes and the runtime can swap implementations.
- All handles are created in `app_wiring.c` in dependency order. That file is the single
  place composite ownership exists, and reading it top to bottom describes the whole system.
- Handle storage is a single static `app_ctx_t` allocated once — satisfying "no dynamic
  allocation after startup" without scattering statics through every module.

**No C++.** The lifecycle vtable pattern in `screen_if_t` and the transport interfaces give
us the polymorphism we actually need in about thirty lines of plain C.

## Consequences

**Good.** Every module can be instantiated twice, which is what makes host tests possible at
all. Initialisation order is explicit and compiler-checked rather than implied. Injecting a
fake `time_source_t` makes the poll policy and progress interpolation testable
deterministically, without waiting in real time — that alone justifies the pattern.

**Cost.** More boilerplate: a config struct, a create, a destroy, and a handle threaded
through call sites. Roughly 20–30 extra lines per module. This is the main thing a
contributor will be tempted to skip; the review checklist calls it out.

**Cost.** `app_wiring.c` becomes a chokepoint that every new module touches. Acceptable —
it is also the file that best documents the system, and merge conflicts there are trivial.

## Alternatives rejected

**File-static singletons (`static my_state_t s_state;`).** The ESP-IDF default. Rejected: no
seams for testing, implicit initialisation order, and it directly violates the no-globals
requirement. It is genuinely fine for a hobby build and genuinely wrong for a codebase with
a multi-year maintenance horizon.

**C++ with classes and RAII.** Tempting for UI composition specifically, where object trees
and destructors map well. Rejected because the cost is not local: exceptions and RTTI must
be disabled anyway on this target, `std::` containers conflict with the no-allocation rule,
and mixing the two ABIs across a component boundary creates linkage subtleties that cost
more debugging time than the syntax saves. The requirement said "avoid C++ unless strongly
justified" and the justification is not there.

**A service-locator / registry (`registry_get("spotify")`).** Rejected because it is a global
wearing a disguise. Dependencies become invisible at the call site, and the compiler stops
being able to tell you what a module needs.

**Passing individual dependencies as separate parameters instead of a config struct.**
Rejected because every added dependency then breaks every call site, which in practice
pushes people toward globals to avoid the churn.
