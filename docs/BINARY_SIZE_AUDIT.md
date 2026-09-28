# NJEMU Binary and Static-RAM Size Audit

Date: 2026-09-22

This audit was started while closing R10 of the reactive-memory migration and
is now updated through R16. R11 removed the largest immutable payload; R12-R16
continue with static/lifetime RAM savings that do not add work to emulation hot
paths. Selective `-Os` and PS2 IRX externalization are explicitly deferred.

## 1. PSP section footprint

Representative current PSPSDK builds:

| Core/configuration | `.text` | `.rodata` | `.data` | `.bss` | PRX |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPS2, GUI off | 559,504 B | 48,660 B | 5,152 B | 1,901,980 B | ~702 KiB |
| MVS, GUI off | 646,144 B | 47,732 B | 12,784 B | 2,023,804 B | ~812 KiB |
| CPS2, GUI on | 668,396 B | 2,736,732 B | 17,520 B | 1,989,100 B | ~3.39 MiB |
| MVS, GUI on | 756,464 B | 2,735,732 B | 21,536 B | 2,110,908 B | ~3.49 MiB |

The important distinction is that file size and runtime RAM pressure are not
the same problem. `.bss` does not materially inflate the PRX on disk, but it is
resident RAM that directly reduces the heap available to ROM/cache data.

## 2. R11 result: external CJK font data

The GUI build adds about 2.69 MiB of `.rodata`. Almost all of that increase is
explained by two translation units:

| Object | PSP contribution |
| --- | ---: |
| `src/common/font/gbk_s14.c` | 2,588,772 B |
| `src/common/font/gbk_tbl.c` | 64,408 B |

`gbk_s14.c` embeds the complete glyph bitmap plus position/width/height/pitch
tables. Together these two objects account for roughly 2.65 MiB of immutable
data in every GUI binary, independently of which language is actually selected.

R11 audited the data rather than subsetting it:

- `gbk_s14` contains 24,192 fixed 14x14 4-bpp glyphs, exactly 98 bytes each;
- all position entries equal `glyph * 98`;
- width, height and pitch are uniformly 14; skip values are uniformly zero;
- all runtime-valid entries in `gbk_tbl` equal the arithmetic mapping
  `(lead - 0x81) * 0xc0 + (trail - 0x40)`.

The complete 2,370,816-byte bitmap is therefore generated as
`font/gbk_s14.bin` and read through a 64-entry LRU glyph cache. No glyphs are
removed, so translated UI, legacy GBK names/metadata and command-list text keep
the original repertoire. The redundant metadata and GBK lookup arrays are no
longer linked into GUI binaries.

Measured PSP GUI result, using configurations comparable to the R10 rows:

| Core | `.text` | `.rodata` | `.data` | `.bss` | Total section delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPS2 R11 | 669,644 B | 83,900 B | 17,520 B | 1,996,012 B | -2,644,656 B |
| MVS R11 | 757,712 B | 82,884 B | 21,536 B | 2,117,820 B | -2,644,656 B |

For CPS2, the PRX falls from 3,553,466 B to 901,986 B. The runtime code costs
only +1,248 B of `.text` and the cache +6,912 B of `.bss` versus the comparable
R10 build, while `.rodata` falls by 2,652,832 B.

On PS2, matching the R10 CPS2 GUI `SAVE_STATE=ON` + `COMMAND_LIST=ON`
configuration, total sections fall from 8,654,170 B to 5,985,474 B, a
2,668,696-byte reduction. The generated font asset is installed with the
external translation packs rather than being resident in the ELF.

Storage-I/O latency for cold CJK cache misses should still be profiled on
physical PSP/PS2 hardware. The cache makes repeated UI glyphs resident, but the
cross-build/unit validation does not substitute for device timing.

## 3. R12 result: remove the PSP GU list from non-PSP builds

The first `.bss` audit found a platform-ownership bug rather than a buffer that
needed a more complicated lifetime change. `gulist` is the 300 KiB command-list
buffer passed to `sceGuStart()` by `src/psp/psp_video.c`; there are no PS2 or
Desktop users. It was nevertheless defined unconditionally in `emumain.c`, so
every platform paid for it for the entire process lifetime.

R12 keeps the exact same static buffer on PSP and compiles it out everywhere
else. This does not replace static memory with heap memory: PS2/Desktop simply
stop reserving the unused 307,200 bytes.

Measured on the MVS PS2 GUI build with `SAVE_STATE=ON` and
`COMMAND_LIST=ON`:

| Section | Before R12 | R12 | Delta |
| --- | ---: | ---: | ---: |
| `.text` | 913,416 B | 913,416 B | 0 B |
| `.rodata` | 101,992 B | 101,992 B | 0 B |
| `.data` | 406,432 B | 406,432 B | 0 B |
| `.bss` | 2,735,944 B | 2,428,744 B | **-307,200 B** |
| total sections | 6,205,501 B | 5,898,301 B | **-307,200 B** |

Validation performed:

- Desktop MVS GUI build succeeds and all 10 CTest tests pass;
- PS2 MVS GUI cross-build succeeds and the linked ELF no longer contains a
  `gulist` symbol;
- the `PSP` preprocessor path retains the original declaration unchanged. The
  local PSP compiler/toolchain installation is not currently available on this
  shell's configured paths, so this small platform-guard change was not
  re-cross-built for PSP in this R12 pass.

## 4. R13 result: replace derived full-palette LUTs

The next large `.bss` candidates were `video_clut16` tables. They do not hold
emulated state: they cache deterministic conversions from each machine's native
palette word to the renderer's 15-bit color format.

For MVS/NCDZ the 32,768-entry table is unnecessary. Neo Geo's palette encoding
is only a permutation of the five red/green/blue bits, so R13 converts it with
bit operations and removes the 65,536-byte table completely.

CPS1/CPS2 include a four-bit brightness term, but each output component depends
only on `(brightness, component)`. The old 65,536-entry `uint16_t` table is now
a 16x16 `uint8_t` component table (256 bytes). Palette conversion performs three
lookups in this cache-hot table and combines the resulting five-bit channels.
The net static-RAM saving is **130,816 bytes per CPS core**.

`palette_convert_tests` exhaustively compares the replacement against the old
algorithms for all 32,768 Neo Geo colors and all 65,536 CPS palette words. This
proves exact 15-bit output equivalence rather than relying on visual inspection.

Measured MVS PS2 GUI result on the same R12 configuration:

| Section | R12 | R13 | Delta |
| --- | ---: | ---: | ---: |
| `.text` | 913,416 B | 913,112 B | -304 B |
| `.rodata` | 101,992 B | 101,992 B | 0 B |
| `.data` | 406,432 B | 406,432 B | 0 B |
| `.bss` | 2,428,744 B | 2,363,208 B | **-65,536 B** |
| total sections | 5,898,301 B | 5,832,461 B | **-65,840 B** |

Representative PS2 GUI section sizes after R13 are:

| Core | `.text` | `.rodata` | `.data` | `.bss` |
| --- | ---: | ---: | ---: | ---: |
| CPS1 | 899,256 B | 110,120 B | 673,632 B | 2,693,448 B |
| CPS2 | 812,104 B | 103,024 B | 402,256 B | 2,235,336 B |
| MVS | 913,112 B | 101,992 B | 406,432 B | 2,363,208 B |
| NCDZ | 868,408 B | 141,544 B | 395,200 B | 2,244,552 B |

Validation performed:

- Desktop CPS1/CPS2/MVS/NCDZ builds succeed;
- the exhaustive palette-conversion test passes in all Desktop core builds;
- MVS additionally passes the complete 11-test Desktop CTest suite;
- PS2 CPS1/CPS2/MVS/NCDZ GUI cross-builds succeed;
- the local PSP compiler installation remains unavailable in this shell, so
  PSP cross-build validation is still pending. The replacement code is shared
  C with no platform-specific API dependency.

The complete CPS1 Desktop CTest suite still has the existing target-specific
`memory_plan_tests` assertion failure; the CPS1 application build and the new
palette test both pass, and this failure is unrelated to the palette changes.

## 5. R14 result: scope the ROM-browser ZIP-name database to the menu

`common/filer.c` kept a `MAX_GAMES` array of ZIP names/titles in `.bss`. On the
32-bit console targets the 512 entries occupy 75,776 bytes. The browser already
called `free_zipname()` immediately before `emu_main()`, but that function only
reset the entry count, so none of the memory was actually returned to the heap.

R14 changes the database to one contiguous heap allocation owned by the file
browser. `load_zipname()` allocates it when the ROM list needs titles and
`free_zipname()` now releases it before emulation starts. Returning from a game
reloads the database exactly as before. This is intentionally a lifetime change,
not a smaller database: all 512 entries and full 128-byte titles remain.

Measured MVS PS2 GUI result on the R13 configuration:

| Section | R13 | R14 | Delta |
| --- | ---: | ---: | ---: |
| `.text` | 913,112 B | 913,080 B | -32 B |
| `.rodata` | 101,992 B | 101,992 B | 0 B |
| `.data` | 406,432 B | 406,432 B | 0 B |
| `.bss` | 2,363,208 B | 2,287,432 B | **-75,776 B** |
| total sections | 5,832,461 B | 5,756,653 B | **-75,808 B** |

The same 75,776-byte `.bss` reduction is present in CPS1/CPS2. More
importantly, the allocation is gone before `emu_main()` performs ROM/cache
planning, so this memory is genuinely available to the R10 allocation probes
during gameplay rather than merely moving permanent state from `.bss` to heap.

Validation performed:

- Desktop MVS passes the complete 11-test CTest suite;
- Desktop CPS1/CPS2 application builds succeed and the translation/font/palette
  focused tests pass;
- PS2 CPS1/CPS2/MVS GUI cross-builds succeed;
- PSP cross-build remains unavailable in the current local shell environment.
  The changed ownership code is common C and does not alter PSP-specific paths.

## 6. R15 result: scope ZIP decompression scratch to an open entry

The legacy unzip implementation kept one `zip_read_info_s` object in `.bss` for
the whole process. Almost all of it is the 16 KiB compressed-input buffer, and
the object is only meaningful between `unzOpenCurrentFile()` and
`unzCloseCurrentFile()`.

R15 allocates that object when a ZIP entry is opened and frees it when the entry
is closed (including the existing close-on-ZIP-close path). This removes 16,512
bytes of permanent `.bss` on the 32-bit console builds without changing the
buffer size, inflate algorithm or read loop. CPS1/CPS2/MVS therefore return the
scratch memory between ROM reads and before the post-load allocation probes;
NCDZ may reacquire it while actively reading a zipped CD entry, as expected.

Measured MVS PS2 GUI result:

| Section | R14 | R15 | Delta |
| --- | ---: | ---: | ---: |
| `.text` | 913,080 B | 913,112 B | +32 B |
| `.rodata` | 101,992 B | 101,992 B | 0 B |
| `.data` | 406,432 B | 406,432 B | 0 B |
| `.bss` | 2,287,432 B | 2,270,920 B | **-16,512 B** |
| total sections | 5,756,653 B | 5,740,173 B | **-16,480 B** |

Validation performed:

- Desktop CPS1/CPS2/MVS/NCDZ application builds succeed;
- MVS passes the complete 11-test Desktop CTest suite;
- PS2 CPS1/CPS2/MVS/NCDZ GUI cross-builds succeed;
- a standalone stored-ZIP smoke test successfully exercises
  open-entry -> read -> close-entry -> close-ZIP with the dynamic scratch;
- PSP cross-build remains unavailable in the current local shell environment.

## 7. R16 result: allocate the CPS1 stars vertex buffer only when supported

CPS1 kept a 4,096-point stars vertex buffer permanently in every build even
though the driver table enables stars only for the `GFX_FORGOTTN` and
`GFX_STRIDER` profiles. On PS2 `GSPRIMPOINT` is 32 bytes, so that unused buffer
cost 131,072 bytes for the large majority of CPS1 games. PSP/Desktop use the
8-byte common point vertex, for a 32,768-byte buffer.

R16 makes this buffer feature-scoped. `cps1_video_init()` allocates the exact
same capacity only when `driver->has_stars` is true, preserving 64-byte
alignment on PSP/PS2, and `cps1_video_exit()` releases it before returning to
the ROM browser. The stars render loop itself is unchanged: it still writes a
contiguous array and submits the same point count, so there is no added per-star
or per-frame work.

Measured CPS1 PS2 GUI result:

| Section | R15 | R16 | Delta |
| --- | ---: | ---: | ---: |
| `.text` | 899,240 B | 898,904 B | -336 B |
| `.rodata` | 110,120 B | 110,120 B | 0 B |
| `.data` | 673,632 B | 673,632 B | 0 B |
| `.bss` | 2,601,160 B | 2,470,088 B | **-131,072 B** |
| total sections | 6,333,383 B | 6,202,039 B | **-131,344 B** |

For non-stars games the full 128 KiB PS2 / 32 KiB PSP payload remains available
to the rest of the process. Stars games allocate that payload during video
initialization and therefore retain the original rendering capacity and runtime
memory requirement (apart from negligible allocator metadata).

Validation performed:

- Desktop CPS1 application build succeeds and the focused translation/font/
  palette tests pass;
- PS2 CPS1 GUI cross-build succeeds and `vertices_stars` is now only a 4-byte
  pointer in the ELF;
- PSP source keeps the original 64-byte alignment through `memalign(64, ...)`,
  but the current shell still lacks a runnable PSP cross-toolchain for a fresh
  link validation.

## 8. Static RAM (`.bss`) follow-up audit

Representative large PSP symbols include. `gulist` remains in this list because
it is genuinely required by PSP; R12 only removes its accidental cost on the
other platforms.

### CPS2

- `gulist`: 307,200 B
- `JumpTable`: 262,144 B
- `cps1_gfxram`: 196,608 B
- `vertices_object`: 163,840 B
- `SZHVC_add`: 131,072 B
- `SZHVC_sub`: 131,072 B
- `vertices_object_flat`: 122,880 B
- `vertices_scroll`: 72,000 B
- `cps1_ram`: 65,536 B
- `scroll1_data`: 65,536 B

### MVS

- `gulist`: 307,200 B
- `vertices_spr`: 294,912 B
- `JumpTable`: 262,144 B
- `lfo_pm_table`: 131,072 B
- `neogeo_videoram`: 131,072 B
- `SZHVC_add`: 131,072 B
- `SZHVC_sub`: 131,072 B
- several 64 KiB video/RAM/fix buffers.

These should be audited separately from executable-size work. Candidates that
are not required simultaneously could potentially become lifecycle-scoped heap
allocations or share storage, but only after proving their ownership and hot-
path requirements.

The audit intentionally leaves the remaining large CPU/audio tables alone:

- C68K `JumpTable` is the opcode dispatch table used on every instruction;
- CZ80 `SZHVC_add`/`SZHVC_sub` avoid flag recomputation in arithmetic hot paths;
- MVS/NCDZ `lfo_pm_table` is read by YM2610 synthesis;
- RAM/VRAM/GFX buffers contain live emulated state.

The PS2 sprite vertex arrays were also considered for a compact representation.
`GSPRIMUVPOINTFLAT` stores tagged 128-bit UV and XYZ2 values, so retaining only
the payload coordinates could save hundreds of KiB. It was deliberately
rejected: `ps2_video.c` currently submits those arrays with one linear `memcpy`,
whereas compact storage would require reconstructing/tagging every vertex in a
loop on the renderer hot path. The RAM saving does not justify that performance
regression without profiling evidence to the contrary.

## 9. Selective `-Os`: deferred

Selective `-Os` is outside the scope of the current memory work. If revisited in
a future optimization phase, the existing measurements below remain useful for
choosing cold translation units while keeping CPU/render/audio paths at `-O3`.

Representative PSP GUI object code sizes are:

- `common/ui_draw.c`: ~22.2 KiB `.text`
- `common/ui_menu.c`: ~19.3 KiB
- `common/ui.c`: ~10 KiB
- `common/config.c`: ~3.8 KiB `.text`
- `common/ui_text_catalog.c`: ~2.0 KiB
- `common/ui_layout.c`: ~1.3 KiB
- `common/ui_utf8.c`: <1 KiB
- R10 `common/memory_plan.c`: ~15.3 KiB including its small read-only tables,
  and almost entirely ROM-load/startup-only.

Even an unusually strong percentage reduction across the menu/UI code would
save tens of KiB, not the multi-MiB available by addressing the embedded font.

If selective optimization is pursued later, good cold/startup candidates are:

- menu/UI/configuration and translation plumbing;
- `memory_plan.c`, because allocation probing happens only during ROM loading;
- crypto/decode setup such as MVS `neocrypt.c` or CPS2 decrypt code, subject to
  measuring acceptable ROM-load latency.

The following should remain `-O3` unless profiling proves otherwise:

- C68K/CZ80 execution cores;
- renderer/video/sprite hot loops;
- mixer/audio synthesis loops;
- per-frame cache/address translation paths.

## 10. PS2 IRX externalization

The deferred `ps2_drivers` work is now available as an external-IRX-image
provider. Before switching NJEMU to that flavor, a fresh representative MVS
build was captured with:

```text
TARGET=MVS
PLATFORM=PS2
GUI=ON
SAVE_STATE=ON
COMMAND_LIST=ON
ADHOC=OFF
```

The baseline still links the embedded `libps2_drivers.a` flavor. Its final ELF
measurements are:

| Metric | Bytes |
| --- | ---: |
| ELF file | 3,504,216 |
| `.text` | 983,976 |
| `.rodata` | 101,236 |
| `.data` | 407,272 |
| `.bss` | 1,725,792 |
| GNU `size` runtime image (`text + data + bss`) | 3,218,336 |

`mips64r5900el-ps2-elf-nm -S -t d` reports these embedded IRX payload arrays:

| Symbol | Bytes |
| --- | ---: |
| `audsrv_irx` | 19,389 |
| `bdm_irx` | 10,745 |
| `bdmfs_fatfs_irx` | 36,205 |
| `cdfs_irx` | 10,705 |
| `fileXio_irx` | 5,637 |
| `iomanX_irx` | 10,401 |
| `libsd_irx` | 15,813 |
| `mcman_irx` | 72,101 |
| `mcserv_irx` | 8,197 |
| `mtapman_irx` | 7,781 |
| `mx4sio_bd_irx` | 11,841 |
| `padman_irx` | 36,741 |
| `poweroff_irx` | 3,489 |
| `ps2atad_irx` | 10,653 |
| `ps2dev9_irx` | 10,977 |
| `ps2fs_irx` | 42,477 |
| `ps2hdd_irx` | 23,701 |
| `sio2man_irx` | 5,241 |
| `usbd_irx` | 26,241 |
| `usbmass_bd_irx` | 12,681 |
| **Total embedded IRX payloads** | **381,016** |

There are also 20 linked `size_*_irx` metadata symbols at four bytes each (80
bytes total). The payload arrays alone account for 93.6% of the baseline
`.data` section, so final-section deltas rather than archive size are the useful
measure of the migration.

The runtime requirement audit is narrower than staging the complete external
image. NJEMU itself needs only audio (`PS2_DRIVER_REQ_AUDIO`) and, after platform
initialization, joystick/multitap input (`PS2_DRIVER_REQ_JOYSTICK`). The
filesystem layer continues to use `init_only_boot_ps2_filesystem_driver()`.

NJEMU does not duplicate the boot-device-to-filesystem policy. Before resetting
the IOP it calls:

```c
ps2_drivers_img_stage_default_for_current_boot(
    PS2_DRIVER_REQ_AUDIO | PS2_DRIVER_REQ_JOYSTICK);
```

`ps2_drivers` therefore owns the current-working-directory lookup, boot-device
detection, filesystem requirement selection, transitive dependency expansion,
deduplication, and the default relative `ps2_drivers.irximg` path. No PS2
network driver is initialized by NJEMU in this configuration, so `NETMAN`,
`SMAP`, `EEIP`, and `IOPIP` are not application requirements.

The representative MVS build after switching to `libps2_drivers_img.a` is:

| Metric | Embedded baseline | External image | Delta |
| --- | ---: | ---: | ---: |
| ELF file | 3,504,216 | 3,129,888 | -374,328 |
| `.text` | 983,976 | 988,680 | +4,704 |
| `.rodata` | 101,236 | 102,404 | +1,168 |
| `.data` | 407,272 | 25,992 | -381,280 |
| `.bss` | 1,725,792 | 1,725,792 | 0 |
| GNU `size` runtime image | 3,218,336 | 2,842,928 | **-375,408** |

`mips64r5900el-ps2-elf-nm -S -t d` reports no embedded `*_irx` payload
arrays or `size_*_irx` metadata in the external-image ELF. The copied
`ps2_drivers.irximg` is 547,456 bytes and is byte-identical to the image
installed by `ps2_drivers`; it is deployment data rather than a monolithic EE
allocation.

The old ELF carried 381,016 bytes of IRX payload arrays. The external provider
adds a small amount of code/read-only data, so the measured steady-state loaded
reduction is:

```text
381,016 B embedded IRX payloads
- 5,608 B net provider/parser overhead
= 375,408 B steady-state EE image recovered
```

For the PCSX2 `host:` launch used for validation, the current-boot filesystem
path plus audio and joystick requirements stage seven modules totaling 101,003
bytes before the IOP reset. This is a temporary peak specific to that launch
device, not a permanent cost or a universal filesystem maximum. The provider
releases each EE IRX payload after `SifExecModuleBuffer()`; NJEMU verifies after
joystick initialization that both staged module count and staged bytes are zero
before video/UI startup continues.

PCSX2 validation of the final representative ELF confirms the NJEMU IOP reset
followed by external registration of `iomanx`, `libsd`, `audsrv`,
`sio2man`, `mtapman`, and `padman`, then normal NTSC GS setup. No staging
failure or platform/input/video initialization failure was reported. A
Windjammers archive was available in an older test deployment and was copied
only to a temporary deployment for a ROM smoke attempt, but macOS denied the
synthetic keyboard events needed to drive the GUI, so this session does not
claim a new ROM-load/cache-allocation measurement.

Existing runtime diagnostics relevant to the post-migration comparison are the
startup `[memory]` snapshot and the MVS `[memory_plan]` retained-allocation
probe. The latter is the authoritative signal for whether reclaimed steady-state
EE memory increases the C-ROM/PCM cache shape after mandatory allocations are
resident.

## 11. Current status / future work

Final representative PS2 GUI sizes after R16 (`SAVE_STATE=ON`,
`COMMAND_LIST=ON`) are:

| Core | `.text` | `.rodata` | `.data` | `.bss` | total sections |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPS1 | 898,904 B | 110,120 B | 673,632 B | 2,470,088 B | 6,202,039 B |
| CPS2 | 812,104 B | 103,024 B | 402,256 B | 2,143,048 B | 5,454,887 B |
| MVS | 913,112 B | 101,992 B | 406,432 B | 2,270,920 B | 5,740,173 B |
| NCDZ | 868,440 B | 141,544 B | 395,200 B | 2,228,040 B | 5,820,337 B |

The cumulative linked `.bss` reduction from R12-R16 is **661,376 B for
CPS1**, **530,304 B for CPS2**, **465,024 B for MVS**, and **389,248 B for
NCDZ**. For CPS1, 131,072 bytes of that total are feature-scoped rather than
unconditionally eliminated: Forgotten Worlds/Strider reacquire the stars
buffer at runtime, while other CPS1 games retain the full saving.

Final validation status:

- all four Desktop application targets build successfully;
- MVS passes the complete 11-test CTest suite in the existing assertions-on
  build;
- the CPS1/CPS2/NCDZ Release audit directories compile tests with `-DNDEBUG`;
  `memory_plan_tests` uses side-effecting calls inside `assert(...)`, so that
  test binary is invalid in Release and aborts after the calls are compiled
  out. Fresh assertions-on CPS1/CPS2/NCDZ builds all pass
  `memory_plan_tests: OK`; this is a test-configuration issue, not a runtime
  memory regression;
- all four PS2 GUI cross-builds succeed with the final sources;
- a fresh PSP cross-build cannot be run in the current shell because no
  runnable installed PSP cross-toolchain is available. R11 had previously been
  cross-built with PSPSDK; the later common/PSP source changes preserve the
  platform's existing alignment/capacity contracts but still require a future
  PSP link/device validation when that toolchain is available.

1. **Completed in R11:** externalize the ~2.65 MiB embedded CJK font/lookup
   payload without reducing the glyph repertoire.
2. **Completed for the current phase:** audit large `.bss`/lifetime buffers.
   R12 removes the unused
   300 KiB PSP GU list from PS2/Desktop; R13 removes 64 KiB from MVS/NCDZ and
   127.75 KiB from CPS1/CPS2 color conversion storage; R14 returns 75,776 bytes
   of ROM-browser metadata before emulation on CPS1/CPS2/MVS; R15 removes a
   further 16,512 bytes of permanent ZIP decompression scratch on all cores;
   R16 makes the CPS1 stars vertex buffer conditional, saving 128 KiB on PS2
   (32 KiB on PSP) for games that do not implement the stars layer.
3. Further RAM work should only resume when a new candidate can shorten
   lifetime or remove storage without adding work to CPU/render/audio hot paths.
4. **Completed:** PS2 now uses the external `ps2_drivers` IRX image. The
   representative MVS build removes all embedded IRX arrays and recovers
   **375,408 bytes** of loaded EE image. NJEMU expresses only its audio/input
   needs; `ps2_drivers` owns boot-device filesystem requirements and dependency
   expansion.
5. **Deferred:** selective `-Os`; keep the current optimization policy for this
   phase.

R10 itself deliberately stops at measurement. Its runtime-memory improvement
comes from empirical allocation-shape probing, not from changing optimization
flags or removing assets.
## Post-S1/S2 compression baseline (2026-09-28)

After `af8b608` (`Unify PNG and compression backends`), the full
`GUI=ON SAVE_STATE=ON COMMAND_LIST=ON ADHOC=OFF` PSP/PS2 matrix measures:

| Platform | Target | `.text` | `.rodata` | `.data` | `.bss` | Runtime image | ELF | PRX/PBP |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| PSP | CPS1 | 802,188 | 94,300 | 290,384 | 2,306,828 | 3,497,392 | 2,991,700 | 1,421,186 / 1,431,254 |
| PSP | CPS2 | 739,400 | 88,084 | 18,656 | 1,767,740 | 2,617,400 | 2,494,784 | 988,778 / 998,363 |
| PSP | MVS | 827,608 | 87,172 | 22,288 | 1,954,812 | 2,895,768 | 2,672,616 | 1,099,586 / 1,108,855 |
| PSP | NCDZ | 810,224 | 126,092 | 10,176 | 1,913,244 | 2,863,512 | 2,666,432 | 1,095,766 / 1,104,157 |
| PS2 | CPS1 | 931,976 | 101,676 | 294,360 | 2,076,496 | 3,404,568 | 3,343,784 | - |
| PS2 | CPS2 | 856,400 | 95,100 | 22,344 | 1,538,480 | 2,512,384 | 2,928,036 | - |
| PS2 | MVS | 959,744 | 94,164 | 25,992 | 1,725,792 | 2,805,752 | 3,090,440 | - |
| PS2 | NCDZ | 905,960 | 133,332 | 13,840 | 1,683,072 | 2,736,264 | 3,196,216 | - |

Relative to `docs/S0_POST_VITA_BASELINE.md`, PSP runtime images are smaller by
43,188 / 43,048 / 43,652 / 67,336 bytes for CPS1/CPS2/MVS/NCDZ respectively.
PS2 runtime images are smaller by 412,048 / 412,600 / 412,584 / 431,456 bytes;
most of the PS2-only delta is the external-IRX migration documented below.

The installed mainstream miniz remains intentionally unmodified. `nm -S` on
the MVS full builds shows that its static archive still contributes 24 ZIP
writer symbols totaling 24,896 bytes on PSP and 24,884 bytes on PS2, despite
NJEMU runtime ZIP access being read-only. This is now a measured opportunity,
not an action item: NJEMU will keep using the mainstream package rather than a
custom/lean miniz variant. The same binaries contain no libz compression symbol
family; PNG/save-state/CRC use miniz directly.

### MSLUG3 PS2 mandatory-memory decomposition

The post-S2 MVS build makes the next RAM target clearer. `mslug3` declares a
9 MiB CPU1 region, 576 KiB CPU2 region, 512 KiB GFX2 region, 64 MiB C-ROM and
16 MiB SOUND1/V-ROM. Before the R10 cache-shape probe, the allocations that are
directly attributable to MVS ROM/runtime regions are:

| Allocation | Bytes | MiB |
|---|---:|---:|
| CPU1 / 68000 program region | 9,437,184 | 9.000 |
| CPU2 / Z80 program region | 589,824 | 0.5625 |
| GFX1 / system FIX | 131,072 | 0.125 |
| GFX2 / game FIX | 524,288 | 0.500 |
| GFX4 / LO ROM + zoom-table backing | 131,072 | 0.125 |
| USER1 / BIOS | 131,072 | 0.125 |
| USER3 / zoom tables | 65,536 | 0.0625 |
| FIX pen-usage tables | 20,480 | 0.0195 |
| C-ROM sprite pen-usage table | 524,288 | 0.500 |
| **Known mandatory MVS heap** | **11,554,816** | **11.0195** |

MSLUG3's C-ROM and SOUND1 regions are deliberately excluded from that mandatory
sum: R10 handles them as the dynamic GFX/PCM cache shape. The previously
measured MSLUG3 shape was 13,828,096 bytes including the temporary 2 MiB safety
reserve, with 8,585,216 bytes retained for C-ROM and 3,145,728 bytes for PCM.

The current post-S2 PS2/MVS executable has a 2,805,752-byte GNU-size runtime
image. Therefore the two largest explained EE-memory consumers before accounting
for allocator/subsystem overhead are about 11.02 MiB of mandatory MSLUG3 heap
and 2.68 MiB of static executable image. CPU1 alone is 9 MiB and is the dominant
non-cache game allocation. MSLUG3 decrypts this region in place and runtime bank
switching directly points the 68000 fetch window into it, so reducing it would
require a program-ROM paging/banking design rather than a simple lifetime free.

This is an accounting decomposition, not a claim that the remaining bytes up to
32 MiB are all available to `malloc()`: PS2SDK/kernel state, common subsystem
heap allocations, allocator fragmentation and transient I/O allocations remain
outside the table. The empirical R10 probe remains authoritative for actual
cache capacity.
