# Memory Tier System — Replacing `LARGE_MEMORY`

> **Status note (2026-09-21):** the foundation and part of this migration are
> already implemented. For the remaining work, use
> `docs/REACTIVE_MEMORY_POLICY_PLAN.md` as the authoritative plan. It updates
> this design from coarse tier-driven behaviour to a continuous runtime budget
> based on free memory, largest contiguous allocation, game requirements and
> deterministic fallback.
>
> PSP legacy-CFW compatibility is intentionally out of scope. The authoritative
> plan uses a single `MEMSIZE=1` PSP package and removes the raw PSP2K/kubridge
> memory path rather than preserving or abstracting it.

## Goal

Remove the binary `LARGE_MEMORY` CMake flag and replace it with a **runtime tier-based memory profile** selected from the actual available RAM at startup. The profile drives cache sizing, preload strategy, and platform-specific allocation choices.

### Why — minimize disk I/O cost

Every cache miss = a disk read, and disk reads are slow (PS2 USB ~1 MB/s; PSP UMD seek penalties; mechanical fileio overhead). The real metric we're optimizing is **disk reads per frame**, not "amount of RAM used".

This reframes how we spend free RAM:

1. **Preloads eliminate misses entirely** for the regions they cover — the highest-value use of RAM. If GFX fits, preloading it means *zero* GFX disk reads after boot.
2. **Cache only helps with regions that didn't fit a preload** — it reduces miss rate but never reaches zero.

So the allocation priority is:

```
RAM budget = baseline + safety_threshold (mandatory)
          + preloads in priority order (each one zeroes out a class of misses)
          + cache (absorbs what's left, handles whatever wasn't preloaded)
```

A tier with `preload_gfx = true` and a tiny cache is **strictly better** than a tier with no preloads and a huge cache, as long as the GFX region actually fits — because the first scenario has zero GFX misses, the second has many. This is the design intent behind the tier table below.

## Investigation Summary

`LARGE_MEMORY` is a single CMake flag (`CMakeLists.txt:51`, OFF default) with **149 references across 15 files**, but in practice only meaningful for **MVS and CPS2 on PSP**. PS2 and Desktop never set it.

It bundles **three orthogonal decisions** into one binary switch:

| Decision | What changes | Where |
|---|---|---|
| **Allocation source** | Use PSP2K kernel region (32 MB at `0xa000000`) for cache vs. malloc from main heap | `cache.c:955-1015`, `psp.h:25,54` |
| **Preload strategy** | Preload full ADPCM (~MB) and crypto buffers (~9 MB) vs. on-demand decode | `mvs/memintrf.c` (8 refs), `cps2/memintrf.c` (3 refs), `sound/ym2610.c` (14 refs), `mvs/neocrypt.c` (**67 refs**) |
| **Cache enable/disable** | CPS2 disables cache entirely with `LARGE_MEMORY` (full preload) | `emucfg.h:71`, `cache.c` constants |

Cache size is currently decided by **malloc-probing** (try largest multiple of 64 KB, decrement until alloc succeeds) — there is **no syscall** anywhere querying actual free RAM.

Current cache constants:
- `MIN_CACHE_SIZE`: 2 MB (small) / 4 MB (large)
- `MAX_CACHE_SIZE`: 20 MB (small) / 32 MB (large)

## Design

### Sizing principle: **preloads first, cache absorbs the remainder**

Preloads eliminate misses entirely for the regions they cover; the cache only reduces miss rate. So tiers decide which **preload features** are on (sound, crypto, full GFX) — each feature has a known RAM cost and a known I/O-saved benefit. After deducting the emulator baseline, the game's mandatory ROM regions, enabled preload costs, and a safety threshold, **all remaining RAM goes to the cache** so it can absorb whatever wasn't preloaded.

```
cache_size = available_ram
           - emulator_baseline           (per-target: CPU regs, palettes, framebuffers, sound buffers, …)
           - game_required_ram           (CPU ROM, sound ROM if not preloaded, work RAM — known after game load)
           - sum(enabled preload costs)  (preload_sound, preload_crypto, preload_gfx)
           - safety_threshold            (e.g. 2 MB for OS / fragmentation / late mallocs)
```

The existing malloc-probe in `cache_start()` stays as a final safety net (covers fragmentation surprises) but is no longer the primary sizing mechanism — it just trims the computed value down if reality disagrees.

### Timing — cache budget is computed **after** game selection

The two things happen at different times in the boot sequence:

1. **At platform init (game-agnostic).** Query `available_ram()`, pick the **tier** (which features are even *eligible*). This is the static profile — it doesn't yet commit to allocating anything for the cache.
2. **At game load (game-specific).** Now we know the game's CPU ROM size, sound ROM size, GFX ROM size, etc. Subtract those from the budget, then commit to the preloads the tier allows *if they actually fit for this game*. Whatever's left goes to the cache via `cache_start()`.

This naturally handles the three cases:

| Case | Outcome |
|---|---|
| **Tiny game** — total ROM ≤ available RAM | All ROMs loaded directly, cache disabled (`cache_size = 0`). Zero misses ever. |
| **GFX ≤ cache buffer** — game's full GFX fits the cache region | Cache buffer ends up holding 100% of GFX, never evicts. Effectively a preload via the cache mechanism. |
| **GFX > cache buffer** — standard streaming case | Normal LRU streaming with misses. This is the case where the cache size *really* matters and the I/O optimizations from earlier (async prefetch, larger blocks) pay off. |

In other words, "cache enabled" is not a tier decision — it's a runtime outcome based on whether the game has more GFX than fits in RAM after preloads. The tier just decides whether to *make room* for a cache by skipping some preloads.

### `memory_profile_t`

```c
typedef struct {
    const char *name;              // "tiny", "small", "medium", "large"
    uint32_t   min_ram_mb;         // selection threshold

    // Feature toggles
    bool       preload_sound;      // MVS ADPCM, CPS2 QSound
    bool       preload_crypto;     // MVS neocrypt buffers
    bool       preload_gfx;        // CPS2 full GFX preload (replaces "cache_enabled = false")
    bool       use_psp2k_region;   // PSP Slim 32 MB kernel region

    // Sizing knobs (cache is whatever's left)
    uint32_t   safety_threshold_mb; // hold-back for OS / fragmentation
    uint32_t   cache_floor_mb;      // minimum cache; abort if we can't reach this
} memory_profile_t;
```

Per-feature RAM costs come from the target (not the profile) — e.g. `mvs_target.preload_sound_cost_mb`, `cps2_target.preload_gfx_cost_mb` — since they depend on which game/system is loaded.

### Initial tier table (features only — cache is derived)

| Tier   | RAM       | preload_sound | preload_crypto | preload_gfx (CPS2) | psp2k     | safety | cache_floor |
|--------|-----------|---------------|----------------|--------------------|-----------|--------|-------------|
| tiny   | < 24 MB   | no            | no             | no                 | no        | 1 MB   | 2 MB        |
| small  | 24–48 MB  | no            | no             | no                 | no        | 2 MB   | 4 MB        |
| medium | 48–96 MB  | yes           | no             | no                 | yes (PSP) | 2 MB   | 8 MB        |
| large  | ≥ 96 MB   | yes           | yes            | yes (if it fits)   | yes (PSP) | 4 MB   | 8 MB        |

Cache itself is **uncapped** — on a 256 MB host it can grow to ~240 MB if that's what's left. The floor is the abort threshold: if we can't even reach `cache_floor_mb` after deductions, log an error and refuse to start (or fall back to disabling a feature).

### Selection

- Auto-detected at startup from `platform_driver->available_ram()`
- Override via env var `NJEMU_MEM_TIER=tiny|small|medium|large` for testing
- Selected tier + detected RAM logged at boot

## Decisions (locked)

| Question | Decision |
|---|---|
| `mvs/neocrypt.c` (67 refs) | **Refactor to two entry points** dispatched once at init — keeps hot path branch-free |
| Tier override mechanism | **Auto-detect + env var override** (`NJEMU_MEM_TIER=...`) |
| Where does `available_ram()` live | **On `platform_driver_t`** — consistent with other drivers |

## Execution Plan

### Phase 1 — Foundation (no behavior change yet)

1. **Add `available_ram()` to `platform_driver_t`** (`src/common/platform_driver.h`)
   - PSP: `sceKernelTotalFreeMemSize()` + PSP2K probe (read `0xa000000` for Slim detection)
   - PS2: `GetMemorySize()` − reservation baseline
   - Desktop: `sysconf(_SC_AVPHYS_PAGES) * _SC_PAGESIZE`, capped at 256 MB

2. **Define tier table in `src/common/memory_profile.{h,c}`** with the 4 tiers and the `memory_profile_t` struct.

3. **Add `memory_profile_select()`** called from `emumain.c` after platform init:
   - Auto-detect from `available_ram()`
   - Honor `NJEMU_MEM_TIER` env var override (parse string → tier enum, log warning if invalid)
   - Log selected tier + detected RAM at boot
   - **Note:** this only picks the profile — actual cache/preload sizing happens later, after game selection, when game ROM sizes are known.

### Phase 2 — Migrate call sites

The 36 `LARGE_MEMORY` refs (excluding `mvs/neocrypt.c`, deferred to Phase 3) split into three categories. Phase 2 is split into 2a (clean swaps) and 2b (structural refactor), executed as separate PRs.

#### Category A — Pure sizing constants (Phase 2a)
- `cache.c:15-28` — `MIN_CACHE_SIZE` / `MAX_CACHE_SIZE` differ by `LARGE_MEMORY`. Trivially convertible to profile reads.

#### Category B — PSP2K kernel region (Phase 2a)
- `cache.c:953-961` (cache_start) — selects PSP2K region for the cache. Outer `#ifdef LARGE_MEMORY` stays (symbols `PSP2K_MEM_TOP`, `psp2k_mem_left` only declared then), inner runtime check `if (profile->use_psp2k_region)` added.
- `cache.c:1145-1213` (state-save buffers) and `mvs/memintrf.c:116-119`, `cps2/memintrf.c:75-78` — also PSP2K, but tangled with mode-bifurcated static state. **Deferred to 2b.**

#### Category C — Structural code (Phase 2b — separate PR)
- `emucfg.h:71-76` — `USE_CACHE` is *derived* from `LARGE_MEMORY` for CPS2, then used in `#if USE_CACHE` to add or remove entire variables, struct fields, and functions across the codebase.
- `ym2610.c:672-675, 703-706` — `ADPCMA` / `ADPCMB` structs have fields (`block`, `buf`) only when `!LARGE_MEMORY`.
- `cache.c:79, 97, 302, 645, 759, 1011, 1050, 1098, 1119` — gate the entire **PCM cache** infrastructure (separate from the GFX cache; only present when not preloading).
- `mvs/memintrf.c:460, 863, 967, 1643, 1800, 2057` — variable definitions and code paths that exist only in one mode.

Converting Category C requires always compiling in both code paths and always allocating both struct layouts, with runtime dispatch on the active mode. Several thousand lines, real risk of subtle bugs — too large for a single session, so split off.

#### Phase 2a steps (sized for one PR)

1. **Bridge old flag to new system**: when `LARGE_MEMORY` is defined at compile time, force `memory_profile_select()` to choose the `large` tier regardless of detected RAM. Preserves existing PSP Slim behaviour exactly.

2. **Cache sizing migration** (`cache.c`): replace runtime uses of `MIN_CACHE_SIZE` / `MAX_CACHE_SIZE` in `cache_start()` with profile-derived values (`cache_min_mb` / `cache_max_mb`), clamped to compile-time bounds so the static `cache_data[MAX_CACHE_SIZE]` array stays valid. Keep malloc-probe as final fallback.

3. **PSP2K runtime guard** (`cache.c:953-961`): inside the existing `#ifdef LARGE_MEMORY` (needed for symbol existence), add inner runtime check `if (profile->use_psp2k_region)` so the profile drives the actual decision.

4. **No call-site changes outside `cache.c`** — Category C waits for 2b.

#### Phase 2b steps (later, separate PR)

5. **`USE_CACHE` runtime conversion**: redesign `cps2/memintrf.c` so the same code path handles both modes; the cache infrastructure becomes optionally-active rather than compiled-out. Likely requires data layout changes.

6. **PCM cache infrastructure** (`cache.c`, `ym2610.c`, `mvs/memintrf.c`): make ADPCMA/ADPCMB struct layouts identical regardless of mode, branch at runtime on `profile->preload_sound`.

7. **State-save PSP2K paths** (`cache.c:1145-1213`): finish migrating `cache_alloc_state_buffer` / `cache_free_state_buffer` to runtime check; needs the static `cache_alloc_type` to exist unconditionally.

### Phase 3 — Neocrypt refactor (separate, focused PR)

7. **Refactor `mvs/neocrypt.c`** into two entry points dispatched once at init:
   - `neocrypt_preload_all()` — current `LARGE_MEMORY` path (preload all decrypt buffers)
   - `neocrypt_on_demand()` — current default (decrypt during access)
   - Function pointer set from `g_profile.preload_crypto`
   - Hot path stays branch-free (single indirect call instead of 67 inline branches)

### Phase 4 — Cleanup (DONE, scope reduced)

While preparing Phase 4 we discovered the `LARGE_MEMORY` CMake option had
**never been wired through to the C preprocessor** -- only the legacy
`Makefile:217-218` actually adds `-DLARGE_MEMORY=1` to the compile line.
Every CMake build (every config in `build_*/CMakeFiles/*/flags.make`) was
running with `LARGE_MEMORY` undefined the entire time.

This means: in CMake builds, all `#ifdef LARGE_MEMORY` paths have been dead
code since the migration to CMake; the corresponding `#ifndef LARGE_MEMORY`
paths were the live ones. The Phase 1-3 runtime infrastructure
(`memory_profile_t`, `cps2_use_preload`, neocrypt scratch helpers) is the
correct replacement for that machinery and works identically to today's
behaviour in CMake builds.

The Phase 4 done in this branch:

8. **Remove the dead CMake option** from `CMakeLists.txt:51`. Replaced with
   a comment explaining the discovery for future contributors.
9. **Update `CLAUDE.md`**: drop the `-DLARGE_MEMORY=ON` line from the PSP
   build instructions and the bullet from "Useful CMake Options". Replaced
   with a note pointing at `NJEMU_MEM_TIER` env override.

The Phase 4 explicitly **NOT** done (deferred):

10. **Decoupling PSP2K symbols** (`#ifdef LARGE_MEMORY` → `#ifdef PSP`).
    Newly enables LARGE_MEMORY-equivalent preload paths in CMake PSP Slim
    builds. Cannot be tested without real PSP Slim hardware -- regression
    risk on Slim.
11. **Runtime PSP-Slim detection** via `kuKernelGetModel()`. Coupled with
    (10).
12. **Deleting dead `#ifdef LARGE_MEMORY` blocks** across the codebase.
    Would break the legacy Makefile builds that still use `LARGE_MEMORY=1`.

The Phase 2a `LARGE_MEMORY -> tier=large` bridge in `memory_profile.c`
remains in place. It still serves Makefile builds that define the macro,
even though it never fires in CMake builds.

## Risks & Test Plan

- **PSP regression risk** (highest — the only platform actually using it today)
  - Smoke-test 4 configs: PSP base / PSP Slim × MVS / CPS2
- **Crypto correctness**
  - Run a known game through both neocrypt paths, compare decrypted output byte-for-byte
- **PS2 / Desktop tier promotion**
  - Previously always effectively tier=small; new auto-detect should bump them up — verify cache sizes don't blow past available RAM
- **Cache absorbing all free RAM**
  - Verify the safety threshold is large enough on each platform: late mallocs (ZIP buffers, save state, GUI) must still succeed after cache allocation. Watch for fragmentation on PSP / PS2 where there's no virtual memory.

## Suggested PR Breakdown

1. **PR1** — Phase 1 (foundation: `available_ram()` + `memory_profile_t` + selector). Already merged.
2. **PR2** — Phase 2a (cache sizing + PSP2K runtime guard + LARGE_MEMORY→tier bridge). Behaviour-preserving.
3. **PR3** — Phase 2b (`USE_CACHE` runtime conversion + PCM cache infrastructure + state-save PSP2K). Largest single chunk — likely needs further splitting.
4. **PR4** — Phase 3 (neocrypt refactor). Isolated.
5. **PR5** — Phase 4 (remove `LARGE_MEMORY` CMake flag, cleanup). Trivial once everything above is in.

## Starting Point

Begin with **PR1, Phase 1** (`available_ram()` + `memory_profile_t` skeleton) so the scaffolding is in place before touching any call sites.
