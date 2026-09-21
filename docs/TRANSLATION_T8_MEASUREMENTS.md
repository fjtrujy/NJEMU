# Translation T8 memory/performance measurements

Measured on 2026-09-21 with `49414d6` as the post-migration state and `e56539d` as the pre-migration comparison point. The historical source was built outside the repository with the same PS2 toolchain and GUI/SAVE_STATE/COMMAND_LIST configuration; only the obsolete `-Wno-restrict` warning flag was removed from the temporary historical snapshot so it could compile with the current compiler.

## PS2 executable/loadable image size

`mips64r5900el-ps2-elf-size` is the meaningful resident-RAM comparison. Raw ELF size is also recorded because T8 requested executable size, but it includes debug/symbol/layout metadata.

| Core | Raw ELF before | Raw ELF after | Raw delta | Loadable before (`text+data+bss`) | Loadable after | Loadable delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPS1 | 6,504,900 B | 6,530,616 B | +25,716 B | 7,482,132 B | 7,462,492 B | **-19,640 B** |
| CPS2 | 6,049,840 B | 6,072,752 B | +22,912 B | 6,642,964 B | 6,620,740 B | **-22,224 B** |
| MVS | 6,240,792 B | 6,261,720 B | +20,928 B | 6,836,756 B | 6,812,428 B | **-24,328 B** |
| NCDZ | 6,361,420 B | 6,385,476 B | +24,056 B | 6,693,740 B | 6,672,532 B | **-21,208 B** |

For CPS1, `.rodata` fell from 2,809,128 B to 2,784,584 B (-24,544 B), while `.text` increased from 878,640 B to 883,576 B (+4,936 B) for the common loader/validation code.

The old PS2 translation object alone was 27,071 B (`text+data+bss`) for CPS1. The new common `ui_text_driver` plus `ui_text_catalog` objects are about 3.3 KiB before final linking.

## Resident catalog allocation

The runtime loader uses one allocation containing the catalog header, 377 `uint16_t` offsets and one NUL-terminated string blob.

| Language | `.lng` file | PS2 resident allocation |
| --- | ---: | ---: |
| English | 7,452 B | 7,456 B |
| Japanese | 8,446 B | 8,450 B |
| Spanish | 8,598 B | 8,602 B |
| Simplified Chinese | 5,298 B | 5,302 B |
| Traditional Chinese | 5,294 B | 5,298 B |

The old PS2 driver allocated about 1,512 B for a language field plus 377 pointers while the strings remained embedded in the ELF. English therefore adds about 5,944 B of dynamic heap use but removes 19,640-24,328 B from the loaded image. Conservative net RAM reduction:

| Core | Net RAM reduction |
| --- | ---: |
| CPS1 | **13,696 B** |
| CPS2 | **16,280 B** |
| MVS | **18,384 B** |
| NCDZ | **15,264 B** |

## PS2 startup I/O timing

A temporary, non-committed probe around `ui_text_catalog_load()` was run in PCSX2 using the normal `host:` build-root layout, then removed.

Three repeated CPS1/English runs reported:

```text
allocation=7456  load_us=2055  heap_delta=8192
allocation=7456  load_us=2055  heap_delta=8192
allocation=7456  load_us=2055  heap_delta=8192
```

An earlier warm-up run reported 2016 us. PCSX2 timing is not a substitute for real-PS2 storage timing; the useful result is that initialization performs one short catalog load and translation lookup performs no later I/O. The EE program break advanced by 8192 B, consistent with allocator granularity around the exact 7456 B allocation.

PS2SDK exposes physical RAM via `GetMemorySize()` but no reliable query for current EE heap free space, so no fabricated free-RAM-before/after number is reported. The loadable-image reduction plus exact allocation size gives the relevant resident-RAM accounting.

## Lookup cost / recurring I/O

`ui_text_catalog_get()` is a bounds check, one 16-bit offset load and a pointer addition. It performs no allocation, hash lookup, string comparison or disk I/O, so no synthetic microbenchmark is needed for acceptance.

## PSP status

The same loader and pack format are shared by PSP, but native PSP compilation/runtime measurement is unavailable locally. PSP-specific free-RAM and startup-I/O figures remain external validation. PSP can use `sceKernelTotalFreeMemSize()` for a real before/after free-memory sample when hardware/toolchain validation is available.

## T8 acceptance

- one compact catalog allocation: yes;
- no permanent per-message pointer copy: yes;
- no embedded full-language tables: yes;
- no recurring file I/O during GUI rendering: yes;
- clear net PS2 RAM reduction: yes, about 13.7-18.4 KiB depending on core;
- PSP hardware numbers: pending external validation.
