# Reactive Memory Policy Plan

Status: investigation/design, 2026-09-21

## Goal

Replace the remaining compile-time low/high-memory behaviour with one runtime
memory policy that adapts to the memory actually available on the device and to
the requirements of the selected game.

There is currently **no `LOW_MEMORY` preprocessor flag** in NJEMU. The legacy
switch is `LARGE_MEMORY`, which still exists in the PSP Makefile and in a number
of `#ifdef LARGE_MEMORY` blocks. The repository already contains the beginning
of the desired replacement (`platform_driver->availableRam()` plus
`memory_profile_t`), but the migration is incomplete and the current
`availableRam()` implementations do not measure the same thing on each
platform.

This document is the authoritative plan for the remaining work. It supersedes
the unfinished parts of the older root-level `MEMORY_TIER_PLAN.md`.

---

## 1. Current-state audit

### 1.1 What is already runtime-driven

The following infrastructure already exists:

- `platform_driver_t::availableRam()`;
- `memory_profile_select()` called immediately after platform initialization;
- `MEMORY_TIER_TINY/SMALL/MEDIUM/LARGE`;
- runtime profile fields for:
  - sound preload eligibility;
  - crypto preload/scratch eligibility;
  - CPS2 GFX preload eligibility;
  - PSP2K-region eligibility;
  - cache minimum/maximum sizing;
  - safety reserve;
- runtime PCM-cache enable/disable support;
- a runtime CPS2 `cps2_use_preload` switch;
- runtime profile checks in parts of MVS loading and neocrypt;
- a runtime cache-size clamp and malloc probe.

So this is **not** a green-field refactor. Much of the structural work needed to
have both paths compiled into one binary has already been started.

### 1.2 What is still compile-time

`LARGE_MEMORY` still controls behaviour in:

- `src/common/cache.c`;
- `src/common/state.c`;
- `src/common/filer.c`;
- `src/common/ui.c` / `ui.h`;
- `src/cps2/memintrf.c`, `memintrf.h`, `vidhrdw.h`;
- `src/mvs/memintrf.c`, `memintrf.h`, `neocrypt.c`;
- `src/psp/psp.h`, `psp_platform.c`;
- the legacy PSP `Makefile` and PSP CI matrix.

The important remaining cases are not cosmetic. Several of them still compile
out complete allocation/free/preload branches, which means a CMake PSP build
cannot simply promote itself to the old large-memory behaviour at runtime.

### 1.3 `availableRam()` is currently semantically inconsistent

#### Desktop

`desktop_availableRam()` returns physical RAM, capped to 256 MiB. It does not
measure current available memory or the largest allocatable block.

This is safe as a crude policy cap, but it is not actually "available RAM".

#### PS2

`ps2_availableRam()` returns:

```text
GetMemorySize() - 4 MiB
```

PS2 main RAM is fixed, so this acts as a static budget estimate. It does not
reflect allocations already made, heap fragmentation, or the largest
contiguous block.

#### PSP

`psp_availableRam()` currently uses `sceKernelTotalFreeMemSize()` and, only in a
legacy `LARGE_MEMORY` build, adds `psp2k_mem_left`.

This is also the wrong PSPSDK API for the metric we want: current PSPSDK
documents `pspSdkTotalFreeUserMemSize()` as the amount of memory available in
the user partition(s), explicitly distinguishing it from
`sceKernelTotalFreeMemSize()`.

For the new policy, PSP needs two distinct values:

1. total free user memory;
2. largest contiguous free user block.

Current PSPSDK provides a user-memory helper (`pspSdkTotalFreeUserMemSize()`) and
`sceKernelMaxFreeMemSize()` for the largest block. The largest-block metric is
important because a nominal 20 MiB free total does not imply that a 15 MiB GFX
preload can be allocated contiguously.

### 1.4 Tiers should own distribution policy, not compile-time behaviour

The existing tier idea is useful, but the current thresholds/toggles are too
coarse. The desired end state is a **runtime tier table that describes how to
distribute the cacheable RAM budget among the regions competing for memory**.

The tier must be selected from the **cacheable budget remaining after mandatory
allocations and safety reserve**, not simply from physical RAM. This is
important: a 32 MiB PS2 and a 32 MiB hypothetical device do not necessarily
have the same amount of memory left when the cache is created.

Within a tier, allocation remains continuous rather than becoming another hard
binary switch. Each cacheable region has:

- a minimum/floor;
- a relative weight;
- an optional cap;
- its actual region size as the absolute maximum.

The solver first satisfies floors, then distributes the remaining bytes by
weight, caps any region that is already fully resident, and redistributes the
unused remainder. Therefore crossing a tier boundary changes the *policy*, not
the validity of a code path.

### 1.5 Cache sizing is still artificially capped by compile-time metadata

`cache.c` still has compile-time `MIN_CACHE_SIZE` / `MAX_CACHE_SIZE`, and
`cache_data[MAX_CACHE_SIZE]` is statically sized from those values.

That means a high-memory device cannot naturally donate all safe remaining RAM
to cache even if the policy says it can. To become truly reactive, cache
metadata must either:

- be dynamically allocated for the chosen block count; or
- use a platform-independent maximum derived from the core's actual
  `MAX_CACHE_BLOCKS` if the BSS impact is acceptable.

The preferred solution is dynamic metadata sized to the selected cache.

---

## 2. Design decision: capability + pressure, not LOW/HIGH flags

Do **not** introduce a new `LOW_MEMORY` flag.

A device has two separate properties:

1. **capability** — what memory arenas/features the platform can expose;
2. **current pressure** — how many bytes are currently free/contiguous.

Those must be represented separately.

Proposed platform snapshot:

```c
typedef struct platform_memory_info {
    uint64_t physical_total_bytes;
    uint64_t budget_cap_bytes;

    uint64_t free_bytes;
    uint64_t largest_free_block_bytes;

    uint64_t extended_total_bytes;
    uint64_t extended_free_bytes;
    uint64_t extended_largest_block_bytes;

    uint32_t capabilities;
    uint32_t reliability_flags;
} platform_memory_info_t;
```

Example capability flags:

```c
PLATFORM_MEMORY_CAP_EXTENDED_ARENA
PLATFORM_MEMORY_CAP_QUERY_FREE
PLATFORM_MEMORY_CAP_QUERY_LARGEST_BLOCK
```

Example reliability flags:

```c
PLATFORM_MEMORY_FREE_IS_ESTIMATE
PLATFORM_MEMORY_LARGEST_IS_PROBED
```

The platform driver should expose:

```c
bool (*queryMemoryInfo)(void *data, platform_memory_info_t *out);
```

`availableRam()` can remain temporarily as a compatibility wrapper and then be
removed.

---

## 3. When to react

"Reactive" should **not** mean continuously resizing memory during every frame.
That would introduce fragmentation, pointer invalidation and hard-to-reproduce
runtime state changes.

Use three deterministic checkpoints instead.

### Checkpoint A - after platform initialization

Capture platform capability and a first memory snapshot.

Purpose:

- diagnostics;
- broad policy limits;
- detect PSP Fat/Slim-style capacity without selecting a separate executable.

### Checkpoint B - after game metadata is known, before large ROM/cache allocations

Capture memory again and build the actual game-specific plan.

At this point NJEMU knows the important ROM region sizes, so it can calculate
whether full GFX, sound preload, crypto scratch and cache actually fit.

This should be the **primary decision point**.

### Checkpoint C - allocation failure fallback

Every optional large allocation must have a deterministic lower-memory fallback.
If the allocator disagrees with the snapshot because of fragmentation or an
intervening allocation:

1. abandon the optional preload;
2. refresh the snapshot;
3. recalculate the remaining cache target;
4. retry with a smaller plan.

Do not promote or resize again during gameplay.

---

## 4. Runtime tier table + game-specific memory plan

Introduce a per-load immutable plan:

```c
typedef struct memory_plan {
    memory_tier_t tier;
    uint64_t measured_free_bytes;
    uint64_t largest_free_block_bytes;
    uint64_t safety_reserve_bytes;

    bool use_extended_arena;
    bool use_crypto_extended_scratch;

    uint64_t gfx_cache_bytes;
    uint64_t pcm_cache_bytes;

    /* A cache target equal to the whole region is effectively a preload. */
    bool gfx_fully_resident;
    bool pcm_fully_resident;
} memory_plan_t;
```

The plan is computed once per game and then treated as immutable until
`memory_shutdown()`.

`memory_tier_t` becomes the runtime **distribution policy selector**. It still
must never decide whether code is compiled: all allocation/cache modes required
by a core are present in the same executable.

### 4.1 Tier selection uses cacheable budget

Define:

```text
cacheable_budget = measured_free
                 - mandatory_late_allocations
                 - safety_reserve
```

Proposed initial tiers (values are intentionally benchmarkable policy defaults,
not ABI):

| Tier | Cacheable budget | Intended device/state |
| --- | ---: | --- |
| `CRITICAL` | `< 6 MiB` | very constrained / heavily fragmented |
| `LOW` | `6-12 MiB` | PSP Fat-like constrained budget |
| `MEDIUM` | `12-24 MiB` | normal constrained console budget |
| `HIGH` | `24-40 MiB` | enough RAM for aggressive caching |
| `VERY_HIGH` | `>= 40 MiB` | PSP Slim/desktop-like spare budget |

These thresholds are applied to the budget **after** mandatory allocations, so
they are portable across devices. We should tune them from real PS2/PSP cache
miss measurements rather than from model names.

### 4.2 Generic region policy descriptor

Represent the table in data rather than scattered `if` statements:

```c
typedef struct cache_region_policy {
    uint32_t floor_kb;
    uint32_t weight;
    uint32_t cap_kb;      /* 0 = only limited by region size/budget */
    bool allow_full_resident;
} cache_region_policy_t;

typedef struct memory_tier_policy {
    const char *name;
    uint32_t min_cacheable_mb;
    uint32_t safety_reserve_kb;

    cache_region_policy_t gfx_or_crom;
    cache_region_policy_t pcm_or_vrom;

    /* Where every otherwise-unassigned cache block goes first. */
    cache_region_id_t primary_spill_target;
    cache_region_id_t secondary_spill_target;
} memory_tier_policy_t;
```

The same table shape can serve both cores; unsupported regions simply have
weight/floor zero.

There is one additional rule that is more important than the weights:

> **After the safety reserve has been removed, NJEMU should consume all useful
> cacheable RAM.**

The tier's floors/weights establish the preferred balance between regions. They
must not leave an arbitrary unused remainder. Once the weighted distribution is
done, every remaining cache block is assigned to the tier's spill targets until
either the budget is exhausted or every cacheable source region is fully
resident.

For the current CPS2/MVS design, the primary spill target should be **GFX/C-ROM**.
This is the most useful default sink because:

- graphics are substantially larger than the small working-set caches;
- every extra GFX/C-ROM cache block directly increases the probability of
  avoiding storage I/O;
- the region usually has enough addressable source data to absorb all remaining
  constrained-console RAM;
- MVS PCM already has a known small working-set policy, while C-ROM is the main
  I/O bottleneck measured on PS2.

Therefore a tier table is not a set of fixed cache sizes. It is a set of
**minimums + relative priorities + caps + final spill order**.

### 4.3 Initial MVS distribution table

MVS has two genuinely competing cacheable regions today:

- **C-ROM** (`memory_length_gfx3`) — sprite data, currently the main dynamic
  cache and the most expensive PS2 storage path;
- **V-ROM / PCM** (`memory_length_sound1`) — currently a fixed 3 MiB PCM cache
  (`MAX_PCM_SIZE = 0x30` x 64 KiB) when sound cannot be fully resident.

The initial policy should preserve the proven ~3 MiB PCM working set for normal
tiers while giving most additional RAM to C-ROM:

| Tier | C-ROM floor | C-ROM weight | PCM floor | PCM weight | PCM cap | Primary spill | Full-resident promotion |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| `CRITICAL` | 2 MiB | 4 | 0 | 1 | 1 MiB | C-ROM | PCM may remain unavailable; consume every remaining safe block in C-ROM |
| `LOW` | 4 MiB | 3 | 1 MiB | 1 | 3 MiB | C-ROM | after PCM reaches target/cap, all remainder goes to C-ROM |
| `MEDIUM` | 8 MiB | 4 | 2 MiB | 1 | 3 MiB | C-ROM | after PCM reaches target/cap, all remainder goes to C-ROM |
| `HIGH` | 12 MiB | 5 | 3 MiB | 1 | 3 MiB | C-ROM | C-ROM consumes all remaining safe memory; PCM may become resident only if explicitly uncapped later |
| `VERY_HIGH` | 12 MiB | 5 | 3 MiB | 1 | region size | C-ROM, then PCM | fill PCM when useful, but C-ROM remains first sink for otherwise-unused RAM; both may become resident |

The weight is applied **only after floors**. The cap prevents PCM from consuming
RAM indefinitely at tiers where the legacy 3 MiB cache is already known to be a
reasonable working set. Once a cap/region size is reached, its unused share is
automatically transferred to C-ROM. After the weighted pass finishes, C-ROM
also receives **all remaining unassigned blocks**, not merely its weighted
share.

This table is a starting point for measurement. `CACHE_IO_PROFILE` already gives
the hit/miss/read timing data needed to tune the C-ROM/PCM ratio on PS2.

### 4.4 Initial CPS2 distribution table

CPS2 currently has one main cacheable region: **GFX1**. QSound data is loaded as
a normal ROM region rather than using the MVS-style PCM cache, so inventing a
second percentage bucket would waste RAM.

| Tier | GFX floor | GFX share | Full-resident promotion |
| --- | ---: | ---: | --- |
| `CRITICAL` | 2 MiB | 100% of cacheable budget | no |
| `LOW` | 4 MiB | 100% | no |
| `MEDIUM` | 8 MiB | 100% | if entire GFX region fits safely |
| `HIGH` | 8 MiB | 100% | yes |
| `VERY_HIGH` | 8 MiB | 100% | yes |

Thus CPS2 naturally transitions from a small streaming cache to a 100%-resident
GFX cache without requiring a separate `preload_gfx` mode in the policy. If the
target cache equals `memory_length_gfx1`, the implementation may choose the
most efficient full-resident loading path internally.

Because GFX is the only current CPS2 cache bucket, CPS2 has an especially simple
invariant:

```text
gfx_cache_bytes = min(cacheable_budget, memory_length_gfx1)
```

In other words, after the safety reserve, **all usable memory goes to GFX** until
the complete GFX region is resident.

### 4.5 CPS1 and NCDZ

The tier framework remains common, but CPS1/NCDZ should not be forced to create
cache buckets they do not currently need. Their region-policy weights remain
zero until a real cacheable subsystem is identified. This avoids turning a
memory-management refactor into speculative caching work.

### 4.6 Distribution algorithm

For a core with N cacheable regions:

1. compute `cacheable_budget`;
2. select the tier from the table above;
3. align all allocations to the cache block size (currently 64 KiB where
   applicable);
4. satisfy each enabled region's floor;
5. if all floors cannot fit, enter the documented fallback ladder rather than
   overcommitting;
6. distribute remaining memory proportional to region weights;
7. clamp each target to `min(policy_cap, actual_region_size)`;
8. redistribute every clamped region's excess among regions that can still
   grow;
9. run a **spill pass**: while unassigned budget remains, give every available
   cache block to `primary_spill_target` (GFX/C-ROM), then to the secondary
   spill target if the primary source region is fully resident/capped;
10. stop with unused cacheable RAM only when every cacheable source region is
    fully resident or no remaining allocation shape can be represented safely;
11. check every contiguous target against `largest_free_block_bytes`;
12. allocation failure demotes/replans using the actual new snapshot.

Pseudocode for MVS:

```text
budget = cacheable_budget
tier = select_tier(budget)

crom = min(crom_floor[tier], crom_size)
pcm  = min(pcm_floor[tier],  pcm_size)
budget -= crom + pcm

while budget >= BLOCK_SIZE and some_region_can_grow:
    distribute BLOCK_SIZE chunks using tier weights
    clamp each region to its tier cap and real region size
    spill capped shares to the other region

while budget >= BLOCK_SIZE and crom < crom_size:
    crom += BLOCK_SIZE
    budget -= BLOCK_SIZE

while budget >= BLOCK_SIZE and pcm < pcm_size and pcm_policy_allows_growth:
    pcm += BLOCK_SIZE
    budget -= BLOCK_SIZE

if pcm == pcm_size:
    pcm_fully_resident = true
if crom == crom_size:
    gfx_fully_resident = true
```

This is deliberately block-based so the planner produces values the cache can
actually consume without later rounding surprises.

The expected invariant is therefore:

```text
gfx_cache_bytes + pcm_cache_bytes
    == min(cacheable_budget, total_cacheable_source_bytes)
```

modulo block alignment and a real contiguous-allocation/fragmentation limit.
The safety reserve is outside `cacheable_budget`, so satisfying this invariant
does **not** consume the emergency margin.

---

## 5. Budget algorithm

### 5.1 Inputs

At game-load planning time:

- current free bytes;
- largest contiguous block;
- any separate extended-memory arena;
- mandatory core allocations;
- selected game's ROM region sizes;
- expected temporary decrypt scratch size;
- safety reserve;
- core-specific cache floor.

### 5.2 Reserve first

Before optional preloads:

```text
usable = free_now - safety_reserve - known_mandatory_late_allocations
```

Use both total and contiguous constraints:

```text
allocation_size <= usable
allocation_size <= largest_free_block
```

for every single contiguous allocation.

### 5.3 Apply the tier distribution table

After reserve/mandatory deductions, the selected tier determines floors,
weights and caps. This is the only normal source of cache targets.

For CPS2 the table has one active bucket, so the entire cacheable budget flows
to GFX until `memory_length_gfx1` is reached.

For MVS the same budget is split between C-ROM and PCM/V-ROM according to the
tier table in section 4.3. If PCM reaches its cap or complete region size, its
share spills into C-ROM. If C-ROM becomes fully resident first, the inverse
spill applies.

Temporary crypto scratch is **not** a cache bucket: its lifetime is temporary
and it must be handled as a transient contiguous-allocation constraint when the
planner determines the reserve/arena needed during decryption.

### 5.4 Promotion to fully resident

There is no independent "preload mode" policy toggle.

Instead:

```text
region_cache_target == region_total_size
    => region is fully resident
```

The implementation may then use the fastest loading path for a fully resident
region rather than filling it through the streaming cache machinery.

This makes the transition monotonic:

```text
2 MiB cache -> 8 MiB cache -> 20 MiB cache -> complete region resident
```

rather than switching between unrelated low/high-memory implementations.

### 5.5 Allocation fallback ladder

A planned optional allocation must never make startup fail if a lower-memory
path exists.

Example MVS fallback:

```text
planned C-ROM + PCM targets
    -> reduce the region whose allocation failed to its floor
    -> redistribute recovered budget to the other cacheable region
    -> reduce both toward their floors
    -> fail only if mandatory allocations + cache floor cannot fit
```

Example CPS2 fallback:

```text
full-resident GFX target fails
    -> reduce GFX target to largest proven allocatable block
    -> shrink cache to floor
    -> fail cleanly
```

---

## 6. Platform-specific implementation

### 6.1 Desktop

Purpose: deterministic development target, not "consume as much host RAM as
possible".

Use:

- physical total only as capability information;
- OS available-memory information where practical;
- a hard NJEMU budget cap (initially retain 256 MiB) so a desktop with 64 GiB
  does not create a giant cache;
- largest-block may be reported as the effective budget cap because modern
  desktop virtual memory makes the PSP/PS2-style fragmentation constraint much
  less useful.

Support explicit test overrides:

```text
NJEMU_MEMORY_BUDGET_MB=...
NJEMU_MEMORY_LARGEST_BLOCK_MB=...
```

These are more useful for policy testing than relying only on
`NJEMU_MEM_TIER`.

### 6.2 PS2

PS2 has fixed 32 MiB main RAM and no useful OS-level "available user heap"
query in the current code.

Do not pretend that `GetMemorySize() - 4 MiB` is a live free-memory reading.
Instead:

- keep physical total from `GetMemorySize()`;
- keep a conservative policy cap/reserve;
- at Checkpoint B, measure allocatability with a non-retained malloc probe in
  64 KiB cache-block increments;
- record the largest successful contiguous block;
- immediately free the probe;
- final cache allocation still retries downward if necessary.

This extends the cache's existing malloc-probe technique into a normalized
platform memory snapshot rather than duplicating sizing logic inside
`cache_start()`.

### 6.3 PSP

PSP is the platform that benefits most from a single adaptive binary.

Preferred direction:

1. package one executable that requests access to the maximum user memory the
   firmware/device permits;
2. measure what the process actually received at runtime;
3. choose the memory plan from that measurement;
4. stop using `LARGE_MEMORY` to choose code layout or emulator behaviour.

For telemetry use:

- PSPSDK user-free-memory API for total free user memory;
- `sceKernelMaxFreeMemSize()` for the largest contiguous block;
- hardware/model information only as capability metadata, never as the policy
  itself.

Important: **RAM measurement should decide behaviour, not PSP model number.**
Model detection is only useful if an extended-memory API/arena requires it.

### 6.4 PSP expanded memory / PSP2K region

PSP needs an explicit arena model because NJEMU currently has **two different
ways of seeing the extra RAM**, and they must never be counted at the same time.

The runtime tier still depends on total *usable cacheable budget*, but allocation
placement is a separate second step.

Proposed PSP arena description:

```c
typedef enum psp_memory_mode {
    PSP_MEMORY_USER_ONLY = 0,
    PSP_MEMORY_USER_EXPANDED,   /* MEMSIZE exposed extra RAM to normal allocator */
    PSP_MEMORY_LEGACY_EXTENDED  /* raw 0x0a000000 linear arena */
} psp_memory_mode_t;

typedef struct psp_memory_arenas {
    psp_memory_mode_t mode;

    uint64_t user_free_bytes;
    uint64_t user_largest_block_bytes;

    uint64_t extended_free_bytes;
    uint64_t extended_largest_block_bytes;

    bool extended_suspend_tail_needs_preservation;
} psp_memory_arenas_t;
```

Critical invariant:

```text
MEMSIZE-expanded user heap OR legacy raw PSP2K arena
                      never both
```

If the CFW has merged the extra RAM into the user partition, directly treating
`0x0a000000-0x0bffffff` as a second private arena risks double-counting/aliasing
memory already owned by the normal allocator.

The final PSP integration is therefore a two-stage process:

1. discover which memory exposure mode is active;
2. feed its aggregate usable budget into the tier table;
3. place the resulting cache targets into the available arena(s).

#### A. Preferred: normal allocator sees all permitted user RAM

Modern PSPSDK packaging supports a `MEMSIZE` field in `PARAM.SFO`. NJEMU should
explicitly request `MEMSIZE=1` instead of depending on the SDK default. On
firmware/CFW that exposes the additional RAM this makes it part of the normal
user allocation space.

Do this in the PSP CMake packaging call:

```cmake
create_pbp_file(
    TARGET ${TARGET}
    ...
    MEMSIZE 1
)
```

Then query the memory actually exposed to the process:

```c
user_free    = pspSdkTotalFreeUserMemSize();
user_largest = sceKernelMaxFreeMemSize();
```

Do not infer success from `MEMSIZE=1`, PSP model, or CFW version. The measured
user partition is the truth.

If the supported firmware matrix allows this reliably:

- request expanded user RAM for the single PSP package (`MEMSIZE=1`);
- allocate through normal `malloc`;
- remove raw `PSP2K_MEM_TOP` / `psp2k_mem_offset` management;
- remove the `kubridge` dependency used solely for old large-memory handling;
- Fat devices simply report less free memory and naturally choose a smaller
  plan.

This is substantially safer than maintaining a manually managed hard-coded
32 MiB arena.

##### How this maps to tiers

Nothing PSP-specific is added to the tier table.

Example, after mandatory allocations/reserve:

```text
PSP-1000-like process exposes 9 MiB cacheable
    -> LOW tier
    -> MVS gets floors/weights for LOW
    -> all safe remainder spills into C-ROM

PSP-2000/3000-like process exposes 38 MiB cacheable via MEMSIZE
    -> HIGH tier
    -> same exact executable/code paths
    -> PCM gets its policy target
    -> all remaining safe RAM spills into C-ROM/GFX
```

The extra PSP RAM therefore changes the **measured budget**, which naturally
changes the tier. There is no "PSP Slim tier".

#### B. Compatibility fallback: keep an explicit extended arena

If old CFW support still requires the historical PSP2K mapping:

- compile the arena support into every PSP binary;
- detect capability at runtime;
- represent it in `platform_memory_info_t`;
- encapsulate it behind an allocator API such as:

```c
void *platform_extended_alloc(size_t size, size_t alignment);
void platform_extended_free(void *ptr);
```

Core/cache code must never directly reference `PSP2K_MEM_TOP`.

##### Legacy arena properties that must be preserved

The current NJEMU implementation reveals several non-generic properties of this
arena:

- fixed 32 MiB range at `0x0a000000-0x0bffffff`;
- bump/linear allocation through `psp2k_mem_offset`;
- allocations are not individually freed; the arena is effectively reset per
  game;
- MVS can move already-loaded immutable regions into it to free normal heap;
- neocrypt uses it as temporary scratch without advancing the permanent bump
  pointer;
- the last 4 MiB currently need explicit save/restore handling across PSP
  sleep/resume once they are in use.

Do not expose any of those details to CPS2/MVS core code. Model them inside a
PSP arena allocator.

Suggested API:

```c
typedef enum memory_arena_id {
    MEMORY_ARENA_USER = 0,
    MEMORY_ARENA_PSP_EXTENDED
} memory_arena_id_t;

void *platform_memory_alloc(memory_arena_id_t arena,
                            size_t size,
                            size_t alignment);

bool platform_memory_can_reset(memory_arena_id_t arena);
void platform_memory_reset(memory_arena_id_t arena);

bool platform_memory_contains(memory_arena_id_t arena, const void *ptr);
```

For the linear PSP arena `free(ptr)` is deliberately not part of the interface;
ownership is reset as a whole at game shutdown.

##### How the raw 32 MiB arena participates in tier selection

Compute two independent safe budgets:

```text
user_cacheable = user_free
               - user_mandatory_late
               - user_safety_reserve

ext_cacheable = extended_free
              - extended_required_reserve

total_cacheable = user_cacheable + ext_cacheable
```

`total_cacheable` selects `LOW/MEDIUM/HIGH/...`, but the planner keeps the two
arena capacities separate when placing buffers.

This is crucial: 10 MiB user + 20 MiB extended is **not equivalent to one
contiguous 30 MiB allocation**.

##### PSP arena placement order

For `PSP_MEMORY_LEGACY_EXTENDED`, use this initial placement policy:

1. keep mandatory small/runtime allocations in the normal user heap;
2. preserve the user-heap safety reserve;
3. place the primary GFX/C-ROM cache spill in the **extended arena first**;
4. place PCM/V-ROM according to the selected tier, using remaining extended
   memory when appropriate;
5. only consume normal user heap for cache after the extended arena target is
   exhausted or unsuitable;
6. use migration of immutable CPU/FIX/pen-usage regions only as a
   fragmentation/contiguity optimization, not as the primary source of more
   total budget.

This formalizes the useful part of the current PSP trick: large, long-lived
GFX cache data goes into the arena that is ideal for a linear allocation, while
the regular heap keeps room for late small allocations.

Example:

```text
after mandatory allocations:
    user heap safe cacheable = 7 MiB
    PSP2K extended usable    = 28 MiB
    total                    = 35 MiB

=> HIGH tier

MVS target:
    PCM = 3 MiB
    C-ROM = 32 MiB

ideal placement with segmented cache backing:
    extended segment: C-ROM 28 MiB
    user segment:     C-ROM 4 MiB
    user:             PCM 3 MiB

    user emergency reserve remains untouched
```

The current cache implementation cannot yet realize that ideal placement because
`GFX_MEMORY` is one contiguous pointer. Therefore **segmented cache backing is a
required part of the legacy dual-arena PSP path** if we want to honor the global
"consume all safe RAM" invariant.

##### Segmented GFX/C-ROM cache backing

Today cache slots are addressed as:

```c
&GFX_MEMORY[slot << BLOCK_SHIFT]
```

That forces the complete cache to be one contiguous allocation. Replace that
physical-storage assumption with a small segment table while keeping logical
cache slots unchanged:

```c
typedef struct cache_segment {
    uint8_t *base;
    uint32_t first_slot;
    uint32_t slot_count;
    memory_arena_id_t arena;
} cache_segment_t;

typedef struct cache_backing {
    cache_segment_t segments[3];
    uint32_t segment_count;
    uint32_t total_slots;
} cache_backing_t;

uint8_t *cache_slot_ptr(uint32_t slot);
```

On PSP legacy mode this initially needs at most two permanent cache segments:

1. PSP2K extended arena;
2. normal user heap.

The abstraction can remain platform-independent so future ports are not forced
back into a single giant allocation.

Every direct `&GFX_MEMORY[idx << BLOCK_SHIFT]` access in `cache.c` must go
through `cache_slot_ptr(idx)`. The LRU/cache metadata continues to use one
logical slot index, so the I/O/cache algorithms do not need to know which arena
owns a slot.

This produces the desired PSP behaviour:

```text
tier target C-ROM = 32 MiB

extended largest block = 28 MiB
user largest safe block = 4 MiB

segment 0 = 28 MiB extended
segment 1 =  4 MiB user

logical C-ROM cache = 32 MiB
unused safe RAM = 0
```

Without segmentation the planner would have to cap the C-ROM cache at 28 MiB
and either waste the remaining 4 MiB or give it to a less valuable region. That
would violate the intended GFX-first spill policy.

##### Contiguous full-resident path vs logically fully cached

Keep two concepts separate:

```text
logical_cache_covers_entire_region
single_contiguous_buffer_covers_entire_region
```

If the complete GFX/C-ROM source is represented across several cache segments,
runtime I/O can still reach zero once every source block has a resident slot,
but code that expects one contiguous decoded ROM pointer cannot automatically
use the direct preload/decode path.

Therefore `memory_plan_t` should eventually distinguish:

```c
bool gfx_fully_cached;          /* all source blocks fit across cache segments */
bool gfx_contiguous_resident;   /* complete region fits in one suitable arena */
```

Use the direct CPS2/MVS preload path only for
`gfx_contiguous_resident == true`. Otherwise retain the cache access machinery,
even if its logical cache is large enough to hold 100% of the source data.

##### State-save interaction

`cache_alloc_state_buffer()` currently borrows the contiguous `GFX_MEMORY`
buffer and may save/restore it as one range. Segmented cache backing invalidates
that assumption.

As part of R6, state-save must either:

- allocate its temporary state buffer independently from cache backing; or
- iterate/cache-backup each physical segment explicitly.

The preferred design is independent state-save scratch. Cache storage should not
double as unrelated temporary contiguous memory once multiple arenas are
supported.

##### Suspend/resume tail handling

The current code preserves the last 4 MiB of the PSP2K area through
`resume.bin` when that portion has been used. The new allocator must make this
an arena property, not a cache/core special case.

Two valid implementation stages:

1. **conservative first version:** reserve the top 4 MiB, so the raw arena
   exposes 28 MiB usable and needs no special cache knowledge;
2. **full-capacity version:** expose all 32 MiB and have the platform memory
   driver transparently save/restore the suspend-sensitive 4 MiB when committed.

The second option matches the goal of exhausting useful RAM. It should be used
after suspend/resume has been validated on real PSP hardware.

The tier table itself does not change between 28 MiB and 32 MiB; only the
measured `ext_cacheable` input changes.

##### Neocrypt scratch

Neocrypt is transient and must not permanently steal the spill budget.

The planner should record the maximum scratch requirement and choose one of:

```text
extended scratch window
    -> use raw extended arena temporarily, without committing it permanently

user scratch
    -> temporarily require largest_user_block >= scratch_size
```

After decrypt finishes, the scratch bytes become available to the final
GFX/C-ROM spill plan. In other words, the tier should be computed from the
**steady-state cache budget**, while startup validation additionally verifies
that the transient decrypt peak can be satisfied.

##### Existing MVS migration trick

The current MVS large-memory path moves CPU1/CPU2/FIX/BIOS/pen-usage allocations
to the extended region when SOUND1 pressure forces extended-memory use. Preserve
the capability, but turn it into an explicit optional **repack** step:

```text
if planned cache target cannot be allocated because user heap is fragmented:
    move eligible immutable allocations to extended arena
    refresh user_free + user_largest
    retry the same tier target
```

Do not perform migration merely because the device is a Slim. It should happen
only when it improves the largest usable cache allocation or protects the user
heap reserve.

### 6.5 PSP mode detection order

Recommended boot-time logic:

```text
1. Query pspSdkTotalFreeUserMemSize() + sceKernelMaxFreeMemSize().

2. If measured user memory already reflects expanded capacity:
       mode = PSP_MEMORY_USER_EXPANDED
       raw PSP2K arena = disabled (avoid double-counting)

3. Otherwise, if runtime capability checks prove legacy extended access safe:
       mode = PSP_MEMORY_LEGACY_EXTENDED
       expose PSP2K arena through platform memory driver

4. Otherwise:
       mode = PSP_MEMORY_USER_ONLY

5. Build the tier from the resulting safe cacheable budget.
```

The exact threshold used to classify an expanded user partition should be
derived from measured PSP-1000 vs PSP-2000/3000 baselines, not a magic model
check. `kuKernelGetModel()` can remain as supporting capability evidence for the
legacy raw-arena path, but measured memory remains authoritative.

### 6.6 What happens to the old PSP tricks

| Existing trick | New model |
| --- | --- |
| `LARGE_MEMORY=1` | deleted; same binary adapts at runtime |
| `kuKernelGetModel()` | capability hint only for legacy raw arena |
| `PSP2K_MEM_TOP` | hidden inside platform arena implementation, then ideally removed |
| direct 32 MiB cache at `0x0a000000` | GFX/C-ROM primary spill into extended arena |
| `psp2k_mem_alloc()` | platform linear-arena allocator |
| `psp2k_mem_move()` | optional repack operation to improve user-heap contiguity |
| `psp2k_mem_free()` no-op | replaced by arena ownership + reset-at-game-exit |
| neocrypt raw scratch | transient arena reservation, released before final cache commitment |
| top 4 MiB `resume.bin` backup | platform suspend/resume property of extended arena |
| separate PSP/Slim build | one `MEMSIZE=1` package + runtime measurement |

The key point is that the PSP tricks are **not another set of tiers**. They are
ways of increasing or reshaping the RAM that the tier allocator is allowed to
spend.

---

## 7. Remaining `LARGE_MEMORY` migration map

### Group A - easy policy/UI cleanup

Files:

- `common/filer.c`;
- `common/ui.c` / `ui.h`;
- PSP title/build naming in `Makefile`;
- CI matrix labels.

Actions:

- replace PSP-Slim warning gates with runtime capability checks where still
  needed;
- remove UI enum entries that only exist because of compile-time large mode, or
  make the entry unconditional and gate display/runtime use;
- stop producing separate "PSP" vs "PSP Slim" emulator behaviour builds.

### Group B - PSP symbols and memory arena

Files:

- `psp/psp.h`;
- `psp/psp_platform.c`;
- CPS2/MVS `psp2k_mem_*` declarations.

Actions:

- decouple symbol existence from `LARGE_MEMORY`;
- preferably remove direct arena use entirely;
- otherwise expose it through platform allocator/capability APIs.

### Group C - cache metadata and state-save path

Files:

- `common/cache.c`;
- `common/state.c`.

Actions:

- make cache metadata size follow runtime `cache_bytes`;
- remove compile-time min/max sizes;
- move the malloc probe into the common memory-query/budget layer;
- make state-save temporary-memory strategy depend on actual available arena,
  not `defined(LARGE_MEMORY)`.

### Group D - CPS2 GFX preload

Files:

- `cps2/memintrf.c` / `.h`;
- `cps2/vidhrdw.h` and matching implementation.

Actions:

- compile preload/decode functions unconditionally for CPS2;
- remove `#ifdef LARGE_MEMORY` around preload path;
- set `cps2_use_preload` from `memory_plan_t`;
- if preload allocation fails, recompute to cache mode and retry cleanly.

### Group E - MVS sound/extended allocations

File:

- `mvs/memintrf.c` / `.h`.

Actions:

- compile both normal/extended allocation paths;
- remove large-mode-specific free logic;
- track allocation ownership/arena explicitly per buffer;
- size PCM/V-ROM caching from `memory_plan_t::pcm_cache_bytes`;
- treat `pcm_cache_bytes == memory_length_sound1` as fully resident rather than
  as a separate compile-time sound-preload mode.

### Group F - neocrypt scratch

File:

- `mvs/neocrypt.c`.

Actions:

- replace `#ifdef LARGE_MEMORY` scratch selection with a runtime scratch
  allocator;
- use largest-contiguous-block information;
- fall back to ordinary heap scratch when extended scratch is unavailable;
- keep decrypt hot paths free from repeated policy decisions.

---

## 8. Implementation phases

### R0 - Freeze current behaviour and measurements

Before changing allocation policy:

- record PSP Fat/Slim legacy behaviour from current `LARGE_MEMORY=0/1` builds;
- record PS2 MVS/CPS2 cache sizes and startup memory logs;
- identify representative large games:
  - MVS: `mslug3` plus at least one title with large encrypted regions;
  - CPS2: one large GFX title such as `ssf2t`/`xmcota`/another existing corpus title;
- add logging for memory snapshot + chosen plan.

No behavioural changes in R0.

### R1 - Normalize platform memory telemetry

Implement `platform_memory_info_t` and `queryMemoryInfo()`.

- Desktop: available/budget-capped view;
- PS2: physical total + controlled largest-block probe/estimate;
- PSP: `pspSdkTotalFreeUserMemSize()` + `sceKernelMaxFreeMemSize()` + detected
  memory exposure mode (`USER_ONLY`, `USER_EXPANDED`, `LEGACY_EXTENDED`).

Add pure tests for snapshot normalization and overrides.

Acceptance:

- every platform logs the same fields/units;
- no core has to know how the memory number was obtained.

### R2 - Introduce `memory_plan_t` and a pure budget solver

Make a side-effect-free function:

```c
bool memory_plan_build(
    const platform_memory_info_t *memory,
    const game_memory_requirements_t *game,
    memory_plan_t *out);
```

Unit-test synthetic cases including:

- cacheable budgets immediately below/at/above every tier boundary;
- 4/6/8/12/16/24/32/40/48/64 MiB cacheable budgets;
- fragmented case: large total free, small largest block;
- CPS2 GFX just below/equal/above the cacheable budget;
- MVS C-ROM/PCM targets at every floor/cap transition;
- PCM cap spill correctly increases C-ROM target;
- complete PCM/C-ROM region spill correctly redistributes to the other region;
- after safety/mandatory deductions, no allocatable cache block remains
  unassigned while GFX/C-ROM can still grow;
- GFX/C-ROM is always the primary spill target for CPS2/MVS;
- unused budget is accepted only when all cacheable source regions are fully
  resident or a real contiguous-allocation constraint prevents using it;
- PSP dual-arena cases such as `28 MiB extended + 4 MiB user` produce one
  32 MiB logical GFX/C-ROM cache through two physical segments;
- that same dual-arena case does not mark `gfx_contiguous_resident` unless one
  arena alone can hold the complete source region;
- full-region target sets the corresponding `*_fully_resident` flag;
- every target is a multiple of the cache block size;
- safety reserve never violated;
- sum of all cache targets never exceeds the selected cacheable budget;
- deterministic results for the same inputs.

At this stage runtime still uses old paths; compare/log new plan vs old behaviour.

### R3 - Make cache size fully runtime-driven

- dynamically size `cache_data` metadata;
- dynamically size the MVS PCM cache metadata/data target instead of hard-coding
  `MAX_PCM_SIZE = 0x30` as the active cache size;
- remove compile-time `MIN_CACHE_SIZE` / `MAX_CACHE_SIZE` policy;
- feed `memory_plan.gfx_cache_bytes` / `pcm_cache_bytes` into `cache_start()`;
- introduce `cache_backing_t` + `cache_slot_ptr()` so one logical GFX/C-ROM
  cache can span multiple physical arenas/segments;
- migrate direct `GFX_MEMORY + slot*BLOCK_SIZE` accesses to the backing
  abstraction;
- keep allocation retry-down as fragmentation safety;
- retain core `MAX_CACHE_BLOCKS` only as a format/addressability limit, not a
  device-memory tier.

Validate MVS cache correctness and existing cache-I/O profiling.

### R4 - CPS2 runtime cache/full-resident selection

- unconditionally compile GFX preload/decode support;
- remove `LARGE_MEMORY` from CPS2 headers/source;
- use `memory_plan.gfx_cache_bytes` as the single target;
- select the direct full-resident load path only when
  `gfx_cache_bytes == memory_length_gfx1`;
- implement full-resident-allocation-failure -> smaller streaming-cache replan;
- test same ROM in forced low/high budgets and compare decoded output/screenshots.

### R5 - MVS C-ROM/PCM distribution + crypto + ownership cleanup

- apply the tier table to C-ROM + PCM/V-ROM simultaneously;
- make the legacy 3 MiB PCM size a tier policy cap rather than a compile-time
  active cache size;
- allow PCM/V-ROM to become fully resident when its target reaches the region
  size and the selected tier permits it;
- replace raw PSP2K assumptions with arena-aware allocation ownership;
- migrate neocrypt scratch allocation;
- ensure every allocation has one unambiguous matching free path;
- compare decrypted results byte-for-byte between memory plans.

### R6 - State-save and UI cleanup

- make state-save temporary buffer allocation runtime/capability-driven;
- eliminate compile-time PSP-version warning UI;
- remove remaining `LARGE_MEMORY` gates from common code.

### R7 - PSP single-binary memory exposure

Preferred path:

- configure PSP packaging with explicit `MEMSIZE=1` to request maximum supported
  user memory for every build (do not rely on the SDK default);
- verify PSP-1000 still boots and reports its smaller memory;
- verify PSP-2000/3000 reports expanded memory;
- verify the raw PSP2K arena is disabled whenever extra memory is already merged
  into the user partition;
- verify legacy raw-arena fallback separately on firmware/CFW where it is still
  required;
- verify the same tier target can span extended + user cache segments with
  `unused=0` when both arenas have usable capacity;
- validate suspend/resume with the extended arena using less than 28 MiB, exactly
  28 MiB and into the final 4 MiB;
- remove separate large-memory title/build mode;
- remove `kubridge` if no longer needed elsewhere.

If legacy CFW requires the explicit arena, implement the compatibility allocator
from section 6.4B instead, still using one runtime policy.

### R8 - Delete `LARGE_MEMORY`

Only after R3-R7 are validated:

- remove `LARGE_MEMORY` from `Makefile`;
- remove all remaining `#ifdef LARGE_MEMORY`;
- remove old `memory_profile_select()` flag bridge;
- update README and CI;
- keep memory-budget/tier overrides solely as testing/debug controls.

Acceptance command:

```sh
rg '\bLARGE_MEMORY\b' src Makefile CMakeLists.txt .github README.md
```

should return only historical documentation if intentionally retained.

### R9 - Validation matrix

#### Functional

For CPS1/CPS2/MVS/NCDZ:

- GUI/no-GUI where relevant;
- SAVE_STATE on/off;
- COMMAND_LIST on/off;
- PSP ADHOC configurations where buildable.

#### Memory-policy synthetic matrix

Force budgets independent of physical machine:

```text
12 MiB
16 MiB
20 MiB
24 MiB
32 MiB
48 MiB
64 MiB
96 MiB
128 MiB
256 MiB
```

For each, assert:

- no allocation exceeds largest contiguous block;
- safety reserve is preserved;
- selected preloads fit mathematically;
- cache target is deterministic;
- optional-allocation failure selects the documented fallback.

#### Hardware/emulator

- PSP-1000;
- PSP-2000/3000;
- PSP-2000/3000 with `MEMSIZE=1` expanded user heap;
- PSP legacy raw extended-arena mode, if retained;
- PSP suspend/resume while GFX spill occupies the final 4 MiB of extended RAM;
- PPSSPP configured with representative memory models;
- native PS2;
- PCSX2;
- Desktop forced-budget tests.

Record startup memory snapshot, final plan, actual cache allocation and any
fallback.

---

## 9. Logging required during migration

Use one compact diagnostic line at plan creation, for example:

```text
[memory] free=27.8MiB largest=18.4MiB reserve=2MiB tier=MEDIUM budget=20.0MiB crom=17.0MiB pcm=3.0MiB unused=0
```

And after allocations:

```text
[memory] committed crom=16.5MiB pcm=3.0MiB reserve=2.0MiB unused=0.5MiB reason=fragmentation
```

This should be behind a normal diagnostic/log level once the migration is
stable, not noisy unconditional per-frame output.

---

## 10. Important invariants

1. One binary per platform/core must support both constrained and spacious
   devices.
2. Compile-time flags must never decide cache/preload ownership.
3. No optional preload may make a game fail if the streaming path could run.
4. Total free bytes and largest contiguous block are different constraints and
   both must be respected.
5. Safety reserve is deducted before optional allocations.
6. After reserve/mandatory deductions, all safely usable cacheable RAM should
   be assigned. GFX/C-ROM is the primary sink for otherwise-unassigned memory.
7. Unused cacheable RAM is valid only if every cacheable source region is fully
   resident or fragmentation/allocation-shape constraints make it unusable.
8. Memory policy is decided at deterministic lifecycle checkpoints, never every
   frame.
9. Allocation failure is a supported input to the planner, not an exceptional
   impossible state.
10. PSP model is capability metadata only; actual measured memory drives policy.
11. Core code must not know hard-coded PSP memory addresses after the migration.
12. `resources/` is unrelated to this refactor and must not be touched.

---

## 11. Recommended execution order

Recommended implementation sequence:

1. **R1** normalized telemetry;
2. **R2** pure budget solver + exhaustive synthetic tests;
3. **R3** runtime cache sizing;
4. **R4** CPS2 preload/cache;
5. **R5** MVS sound/crypto/allocation ownership;
6. **R6** state/UI cleanup;
7. **R7** PSP single-binary memory exposure;
8. **R8** delete `LARGE_MEMORY` completely;
9. **R9** full forced-budget + hardware validation.

The most important architectural rule is to complete R1/R2 **before** deleting
`LARGE_MEMORY`. That gives every later conversion a single tested decision
source instead of replacing one scattered set of conditionals with another.
