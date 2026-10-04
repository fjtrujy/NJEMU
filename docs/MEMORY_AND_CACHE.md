# Memory and Cache Notes

This document collects the detailed memory and cache background previously embedded in the README. Runtime allocation policy is implemented by the common memory planner; platform-specific values here should be treated as explanatory rather than a substitute for runtime diagnostics.

## Memory Requirements

Understanding memory allocation is crucial for PSP and PS2 platforms where RAM is limited.

### Platform Memory Constraints

| Platform | Available RAM | Notes |
|----------|--------------|-------|
| PSP (Fat) | ~24 MB | User memory only |
| PSP (Slim/2000+) | ~64 MB | Same EBOOT requests the expanded user-memory partition |
| PS2 | ~32 MB | Main RAM |
| Desktop | Unlimited | System dependent |

### Total Memory Requirements by System

| System | CPU/ROM | GFX ROM | Sound ROM | Cache | Static RAM | Estimated Total |
|--------|---------|---------|-----------|-------|------------|-----------------|
| CPS1   | 1-4 MB  | 2-8 MB  | 0.5-2 MB  | -     | ~128 KB    | 4-15 MB         |
| CPS2   | 2-8 MB  | 4-16 MB | 1-4 MB    | 0-20 MB | ~128 KB  | 7-48 MB         |
| MVS    | 1-4 MB  | 8-64 MB | 1-8 MB    | 0-32 MB + 3 MB PCM | ~98 KB | 13-111 MB |
| NCDZ   | 1-2 MB  | 16-128 MB | 0-2 MB  | -     | ~512 B     | 17-132 MB       |

### Why Cache is Required

Many arcade games have graphics data larger than available RAM:
- **MVS games** can have 64+ MB of sprite data
- **CPS2 games** can have 16+ MB of sprite data
- **PSP/PS2** only have 24-64 MB available

The cache system streams graphics from storage in 64 KB blocks, allowing large games to run on memory-constrained platforms.

### PSP Runtime Memory Policy

NJEMU ships a single PSP binary. Its PARAM.SFO explicitly requests the largest
user-memory partition with `MEMSIZE=1`; the same EBOOT therefore runs on
PSP-1000 and PSP-2000/3000-class hardware without a model-specific build.

At startup NJEMU measures the memory actually available to the process with
`pspSdkTotalFreeUserMemSize()` and the largest contiguous allocation with
`sceKernelMaxFreeMemSize()`. The game-specific memory planner then chooses the
cache/residency targets from those runtime measurements.

The important consequences are:

- PSP-1000 naturally receives a smaller cache budget;
- PSP-2000/3000 can use the expanded user heap exposed by the same EBOOT;
- GFX/C-ROM gets allocation priority, with MVS PCM using the remaining planned
  share;
- cache targets are dynamic and aligned to the 64 KB streaming block size;
- CPS2 uses full GFX residency only when the complete region fits the selected
  plan, otherwise it uses the streaming cache;
- MVS applies the same runtime policy to C-ROM and PCM/V-ROM;
- allocation retry-down handles fragmentation without a second build mode;
- the loading log reports the effective allocation, for example
  `C-ROM cache: 15360KB / 65536KB`;
- all PSP allocations use normal heap ownership; there is no raw model-specific
  memory allocator or suspend/resume memory-copy workaround.

### Static RAM Allocations (per system)

**CPS1/CPS2:**
- Main RAM: 64 KB
- Graphics RAM: 48 KB
- Object RAM (CPS2): 8 KB

**MVS:**
- Main RAM: 64 KB
- SRAM: 32 KB
- Memory Card: 2 KB

### GPU Command List Size

| System | GULIST_SIZE |
|--------|-------------|
| CPS1   | 48 KB |
| CPS2   | 48 KB |
| MVS    | 300 KB |
| NCDZ   | 300 KB |

### Sound Buffer Sizes

| System | Buffer Size | Total (stereo) |
|--------|-------------|----------------|
| CPS2 | 2,944 samples | ~12 KB |
| MVS/Others | 1,600 samples | ~6 KB |

---
