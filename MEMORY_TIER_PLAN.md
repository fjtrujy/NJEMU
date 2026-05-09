# Memory Tier System — Replacing `LARGE_MEMORY`

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

4. **Cache sizing** (`cache.c`, ~20 refs) → at game-load time, compute `cache_size = available_ram - baseline - game_required_ram - preload_costs - safety_threshold`. If the game's full GFX fits within that budget without streaming, allocate it as a single load and skip the cache (`cache_size = 0`). Otherwise, allocate the cache; abort only if below `cache_floor_mb` *and* the GFX won't fit fully either. Keep malloc-probe as final fallback for fragmentation.

5. **Boolean feature flags** (`mvs/memintrf.c`, `cps2/memintrf.c`, `ym2610.c`, `emucfg.h` — ~25 refs) → convert `#ifdef LARGE_MEMORY` to `if (g_profile.preload_sound)` / `if (g_profile.cache_enabled)`.

6. **PSP2K region** (`psp.h`, `cache.c`) → guard with `if (g_profile.use_psp2k_region)`, keep inside PSP-only files.

### Phase 3 — Neocrypt refactor (separate, focused PR)

7. **Refactor `mvs/neocrypt.c`** into two entry points dispatched once at init:
   - `neocrypt_preload_all()` — current `LARGE_MEMORY` path (preload all decrypt buffers)
   - `neocrypt_on_demand()` — current default (decrypt during access)
   - Function pointer set from `g_profile.preload_crypto`
   - Hot path stays branch-free (single indirect call instead of 67 inline branches)

### Phase 4 — Cleanup

8. Remove `LARGE_MEMORY` CMake option (`CMakeLists.txt:51`)
9. Delete dead `#ifdef LARGE_MEMORY` blocks
10. Update `CLAUDE.md` build commands (drop `-DLARGE_MEMORY=ON` line)

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

1. **PR1** — Phase 1 + 2 (foundation + sizing/flag migration). No behavior change on PSP if tier values map cleanly to old `LARGE_MEMORY` values.
2. **PR2** — Phase 3 (neocrypt refactor). Isolated, easier to review.
3. **PR3** — Phase 4 (remove `LARGE_MEMORY` flag, cleanup). Trivial once 1 + 2 are in.

## Starting Point

Begin with **PR1, Phase 1** (`available_ram()` + `memory_profile_t` skeleton) so the scaffolding is in place before touching any call sites.
