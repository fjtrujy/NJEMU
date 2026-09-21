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

    uint32_t capabilities;
    uint32_t reliability_flags;
} platform_memory_info_t;
```

Example capability flags:

```c
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

### 6.4 PSP expanded user memory (`MEMSIZE=1`)

NJEMU does **not** need to retain compatibility with old CFW-specific raw
PSP2K memory tricks. The target design should therefore have one PSP allocator
model only: the normal user heap, with the EBOOT explicitly requesting all user
memory the firmware/device can expose.

Configure PSP packaging explicitly with:

```cmake
create_pbp_file(
    TARGET ${TARGET}
    ...
    MEMSIZE 1
)
```

Do not rely on the SDK default. Then query the memory actually visible to the
process:

```c
free_bytes = pspSdkTotalFreeUserMemSize();
largest_free_block_bytes = sceKernelMaxFreeMemSize();
```

The measured values are authoritative. NJEMU does not need to know whether the
unit is a PSP-1000, PSP-2000, PSP-3000 or an emulator when selecting a tier.

Example after mandatory allocations and safety reserve:

```text
PSP with 9 MiB cacheable
    -> LOW tier
    -> apply LOW floors/weights
    -> spill all remaining safe RAM into GFX/C-ROM

PSP with 38 MiB cacheable
    -> HIGH tier
    -> apply HIGH floors/weights
    -> spill all remaining safe RAM into GFX/C-ROM
```

There is no PSP-Slim-specific tier and no secondary memory arena. Extra PSP RAM
simply increases `free_bytes`, which may select a higher tier and/or enlarge the
GFX/C-ROM spill target.

### 6.5 Delete the legacy PSP2K memory path

Because old-CFW compatibility is explicitly out of scope, the migration should
remove rather than abstract the historical raw extended-memory implementation:

- remove `LARGE_MEMORY` from the PSP Makefile/build matrix;
- remove `kubridge` when no longer used elsewhere;
- remove `kuKernelGetModel()` from memory-policy decisions;
- remove `PSP2K_MEM_TOP`, `PSP2K_MEM_BOTTOM` and `PSP2K_MEM_SIZE`;
- remove `psp2k_mem_offset` / `psp2k_mem_left`;
- remove `psp2k_mem_alloc()`, `psp2k_mem_move()` and `psp2k_mem_free()`;
- remove direct PSP2K allocation branches in CPS2, MVS, cache and neocrypt;
- remove the `resume.bin` save/restore workaround for the final 4 MiB;
- remove the PSP-version warning/UI path that existed only for `LARGE_MEMORY`.

All PSP allocations then use normal ownership semantics (`malloc`/`free`) and
normal heap fragmentation rules. This is significantly easier to reason about
and test.

### 6.6 Consequence for cache backing

The segmented GFX/C-ROM cache backing previously considered for combining a
raw PSP2K arena with the normal heap is **not required for this PSP migration**.

Keep the cache representation contiguous unless another platform-independent
reason appears to segment it. The planner should instead respect:

```text
cache_target <= largest_free_block_bytes
```

while still trying to consume the full safe budget. If the total free memory is
larger than the largest block because of heap fragmentation, the allocation
fallback/probe may reduce the target. This is a genuine allocator constraint,
not a reason to preserve the old PSP arena machinery.

The main optimization goal remains:

```text
usable PSP RAM
- mandatory allocations
- safety reserve
= cacheable budget

cacheable budget -> tier distribution -> GFX/C-ROM final spill
```

### 6.7 Neocrypt and other temporary allocations

Neocrypt should use ordinary heap scratch. Its maximum temporary requirement is
a startup peak that the planner must account for before committing the final
cache allocation.

Recommended sequencing:

```text
measure memory
-> load mandatory regions
-> perform/deallocate transient decrypt scratch
-> measure again
-> build final steady-state tier/cache plan
-> allocate GFX/C-ROM + PCM caches
```

This is cleaner than reserving a permanent PSP-specific scratch arena and also
maximizes the memory available to the final GFX spill.

### 6.8 PSP acceptance criteria

The PSP memory part of the migration is complete when:

1. every PSP package explicitly uses `MEMSIZE=1`;
2. tier selection uses `pspSdkTotalFreeUserMemSize()` and
   `sceKernelMaxFreeMemSize()`;
3. PSP-1000-class memory naturally chooses a lower budget/tier;
4. PSP-2000/3000-class expanded user memory naturally produces a larger
   cacheable budget with the same executable;
5. GFX/C-ROM consumes all safe leftover memory up to the contiguous allocation
   and source-region limits;
6. no production source references `PSP2K_MEM_*`, `psp2k_mem_*` or
   PSP-memory-specific `LARGE_MEMORY` behaviour;
7. `kubridge` is absent unless independently required by another feature;
8. normal PSP suspend/resume works without `resume.bin` memory preservation.

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

### Group B - PSP legacy large-memory removal

Files:

- `psp/psp.h`;
- `psp/psp_platform.c`;
- CPS2/MVS `psp2k_mem_*` declarations.

Actions:

- delete raw PSP2K symbols and helpers entirely;
- remove `kubridge` if no unrelated feature still needs it;
- make PSP memory telemetry use only the normal expanded user heap;
- make PSP packaging explicitly request `MEMSIZE=1`.

### Group C - cache metadata and state-save path

Files:

- `common/cache.c`;
- `common/state.c`.

Actions:

- make cache metadata size follow runtime `cache_bytes`;
- remove compile-time min/max sizes;
- move the malloc probe into the common memory-query/budget layer;
- make state-save temporary-memory strategy depend on actual free heap, not
  `defined(LARGE_MEMORY)`.

### Group D - CPS2 GFX preload

Files:

- `cps2/memintrf.c` / `.h`;
- `cps2/vidhrdw.h` and matching implementation.

Actions:

- compile preload/decode functions unconditionally for CPS2;
- remove `#ifdef LARGE_MEMORY` around preload path;
- set `cps2_use_preload` from `memory_plan_t`;
- if preload allocation fails, recompute to cache mode and retry cleanly.

### Group E - MVS sound allocations

File:

- `mvs/memintrf.c` / `.h`.

Actions:

- delete the PSP2K allocation/move/free path;
- use normal heap ownership consistently;
- size PCM/V-ROM caching from `memory_plan_t::pcm_cache_bytes`;
- treat `pcm_cache_bytes == memory_length_sound1` as fully resident rather than
  as a separate compile-time sound-preload mode.

### Group F - neocrypt scratch

File:

- `mvs/neocrypt.c`.

Actions:

- replace `#ifdef LARGE_MEMORY` scratch selection with ordinary heap scratch;
- use largest-contiguous-block information;
- keep decrypt hot paths free from repeated policy decisions.

---

## 8. Implementation phases

### R0 - Freeze current behaviour and measurements

Before changing allocation policy:

- record current PSP user-memory measurements for representative PSP-1000 and
  PSP-2000/3000-class targets/PPSSPP configurations;
- record PS2 MVS/CPS2 cache sizes and startup memory logs;
- identify representative large games:
  - MVS: `mslug3` plus at least one title with large encrypted regions;
  - CPS2: one large GFX title such as `ssf2t`/`xmcota`/another existing corpus title;
- add logging for memory snapshot + chosen plan.

No behavioural changes in R0.

### R1 - Normalize platform memory telemetry

**Status: completed on 2026-09-21.**

Implemented:

- added `platform_memory_info_t` with physical total, budget cap, free bytes,
  largest free block, capability flags and reliability flags;
- added `platform_driver_t::queryMemoryInfo()` while retaining `availableRam()`
  as a temporary compatibility wrapper;
- Desktop now reports live available memory where supported, keeps the 256 MiB
  NJEMU cap and reports its largest-block value as an estimate;
- PS2 now probes the largest allocatable contiguous block in 64 KiB increments
  and uses that conservative result as its free-memory estimate;
- PSP now uses `pspSdkTotalFreeUserMemSize()` plus
  `sceKernelMaxFreeMemSize()` and no longer adds the raw PSP2K arena to the
  telemetry result;
- added `NJEMU_MEMORY_BUDGET_MB` and
  `NJEMU_MEMORY_LARGEST_BLOCK_MB` overrides at the normalized snapshot layer;
- added a common startup diagnostic line and unit tests for normalization,
  effective-budget clamping and override parsing.

Validation performed:

- Desktop MVS build + 7/7 CTest tests pass;
- PS2 MVS cross-build passes with the repository's PS2SDK toolchain;
- PSP MVS cross-build passes with the repository's PSPSDK toolchain and creates
  `EBOOT.PBP`.

R1 deliberately does **not** enable `MEMSIZE=1` or remove the old PSP2K path;
those remain R7/R8 work after the planner and runtime cache ownership have been
migrated.

Implement `platform_memory_info_t` and `queryMemoryInfo()`.

- Desktop: available/budget-capped view;
- PS2: physical total + controlled largest-block probe/estimate;
- PSP: `pspSdkTotalFreeUserMemSize()` + `sceKernelMaxFreeMemSize()`.

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
- delete raw PSP2K assumptions and use normal heap ownership;
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
- verify PSP suspend/resume without any `resume.bin` extended-memory workaround;
- remove separate large-memory title/build mode;
- remove `kubridge` if no longer needed elsewhere.

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
- PSP suspend/resume with a high-memory cache plan;
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
