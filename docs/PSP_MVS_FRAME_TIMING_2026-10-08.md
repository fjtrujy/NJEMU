# PSP MVS frame-timing investigation (2026-10-08)

## Scope

Real PSP, launched via PSPLink (`host0:`), running Metal Slug 3 in attract/demo mode. The diagnostic build used `PSP_ME_AUDIO=ON`, `PSP_MVS_FRAME_TRACE=1`, and `MVS_FRAME_TRACE_SAMPLE_COUNT=6000`, with the audio profile switches disabled. The test configuration was:

- `VideoSync=yes`, `60FPSLimit=no`, `ShowFPS=yes`.
- `AutoFrameSkip=no`, `FrameSkipLevel=0`.
- `EnableSound=yes`, `SampleRate=1`, `SoundVolume=10`.
- `AudioProcessor=1` (Main CPU) or `2` (Media Engine).
- Explicit `CacheReadSize=1`, `2`, or `3` for 16, 32, or 64 KiB reads, respectively.

The v8 binary trace stores 6,000 in-RAM frame records and writes them once at the end. Temporary profiling instrumentation was removed from tracked source code before production validation. Local capture files are retained in `build_mslug3_frame_trace/` and are not part of the Git commit.

## Sound-enabled hardware captures

| Configuration | Total frames >=20 ms | Work over 16.667 ms | Frames with C-ROM read time | Longest C-ROM read | Full-length VBlank waits |
| --- | ---: | ---: | ---: | ---: | ---: |
| Main CPU, 16 KiB | 376 | 408 | 954 | 1,303.6 ms | 0 |
| Main CPU, 32 KiB | 11 | 18 | 70 | 28.6 ms | 2 |
| Main CPU, 64 KiB | 38 | 59 | 35 | 31.3 ms | 0 |
| Media Engine, 16 KiB | 2 | 3 | 82 | 9.9 ms | 0 |
| Media Engine, 64 KiB | 7 | 12 | 17 | 29.7 ms | 0 |

`Work` here is total frame time minus measured VBlank wait and warm-up VSync wait. It is a useful budget estimate, not a standalone CPU utilization measurement. A total frame time around 33 ms is not automatically a VBlank defect: some such frames legitimately missed the current refresh because rendering or storage work exceeded the deadline.

**Controlled comparison:** the Main CPU 16 KiB and 64 KiB captures have identical per-frame sprite atlas miss counts, eviction counts, and atlas occupancy for all 6,000 frames. The dramatically different C-ROM latency is therefore not explained by different sprites or attract-mode scenes in these two captures. The 32 KiB capture diverges in sprite activity from frame 569 and must not be ranked directly against the other runs solely by its frame-time counts. The Media Engine 16 KiB and 64 KiB runs also diverge from frame 569; both remain within budget for most frames, but their counts are not a strict A/B comparison. Media Engine and Main CPU follow differing sprite sequences in their captured windows, so their aggregate timing gap must not be attributed exclusively to offloading audio.

The 16 KiB behavior is consistent with excessive partial C-ROM demand reads and their storage latency under the tested workload. The trace does not establish whether PSPLink/`host0:` transport specifically amplifies this behavior compared with Memory Stick storage. The runtime choice of 16/32/64 KiB remains available for user tests.

## Production decisions

1. Keep MVS sprite-atlas eviction biased toward entries at least two frames old, falling back to one-frame-old entries when necessary to make space. This reduces churn without changing the atlas format or removing the software renderer.
2. Keep the PSP aligned 16-pixel-wide indexed-texture upload path that copies each 8-row swizzled block contiguously. The generic row-copy path remains for other rectangles.
3. Keep the PSP `sceDisplayIsVblank()` guard before `sceDisplayWaitVblankStart()`. Previously, a VBlank already in progress could trigger an unnecessary wait until the following one. The longer guarded captures show no systematic extra full-refresh wait; an occasional near-full wait can still be normal when arriving just after VBlank.
4. Keep **64 KiB** as the automatic PSP MVS cache-read size. Do not change the PS2 MVS 16 KiB default, whose storage path and measurements are different. The runtime menu can still override either default.

## Validation and remaining work

- Built PSP MVS with `PSP_ME_AUDIO=OFF` and `PSP_ME_AUDIO=ON`.
- Built Desktop MVS and PS2 MVS after the cache-policy change.
- Desktop MVS CTest: 38/38 passed, after regenerating stale host translation tools and language packs; no translation source or schema change was required.
- Confirmed both Main CPU and Media Engine modes launch using a production PSP ME-enabled PRX with sound enabled and automatic cache reads. PSPLink confirms the module remains loaded. This does not substitute for a visual or audio quality review.
- Hardware `wjammers` retesting and a Memory Stick versus `host0:` cache-latency comparison remain useful follow-ups.

The profiler-free production changes are intentionally small. Do not restore a per-frame `host0:` logging path, which would itself perturb timing.
