# Translation T8 memory/performance measurements

Final measurements refreshed on 2026-09-21 after the UTF-8 end-to-end Phase 3
migration. The final comparison point is `77bc296` (implementation in
`831c9eb` plus final validation docs); the historical pre-migration point is
`e56539d`.

Historical source was rebuilt outside the repository with the current PS2/PSP
toolchains using the same feature-rich GUI profile
(`GUI=ON`, `SAVE_STATE=ON`, `COMMAND_LIST=ON`, `ADHOC=OFF`). No worktree or
`resources/` mutation was used.

## PS2 executable/loadable image size

`mips64r5900el-ps2-elf-size` is the meaningful resident-RAM comparison. Raw
ELF size is also recorded because T8 requested executable size, although it
contains debug/symbol/layout metadata.

| Core | Raw ELF before | Raw ELF final V2 | Raw delta | Loadable before (`text+data+bss`) | Loadable final V2 | Loadable delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPS1 | 6,504,900 B | 6,534,776 B | +29,876 B | 7,482,132 B | 7,466,036 B | **-16,096 B** |
| CPS2 | 6,049,840 B | 6,076,916 B | +27,076 B | 6,642,964 B | 6,624,284 B | **-18,680 B** |
| MVS | 6,240,792 B | 6,265,876 B | +25,084 B | 6,836,756 B | 6,815,972 B | **-20,784 B** |
| NCDZ | 6,361,420 B | 6,389,504 B | +28,084 B | 6,693,740 B | 6,676,084 B | **-17,656 B** |

For CPS1, final V2 `.rodata` is 2,786,832 B versus 2,809,128 B before the
translation migration (-22,296 B). Final `.text` is 884,872 B versus
878,640 B (+6,232 B); the extra code covers the common external-pack loader,
strict UTF-8 validation/decoding and Unicode glyph lookup.

The earlier byte-compatible V1/Phase-2 MVS image was 6,812,428 B loadable.
Final UTF-8 V2 is 6,815,972 B, so the UTF-8 decoder/map adds 3,544 B to the
loaded MVS image while preserving most of the original storage-migration saving.

## Resident catalog allocation

The runtime loader uses one allocation containing the catalog object, 377
`uint16_t` offsets and the NUL-terminated UTF-8 string blob. On both PS2 and
PSP the resident allocation is 4 bytes larger than the corresponding `.lng`
file because the 20-byte disk header is replaced by the 24-byte in-memory
catalog object.

| Language | Final V2 `.lng` file | Resident allocation |
| --- | ---: | ---: |
| English | 7,500 B | 7,504 B |
| Japanese | 11,537 B | 11,541 B |
| Spanish | 8,716 B | 8,720 B |
| Simplified Chinese | 6,791 B | 6,795 B |
| Traditional Chinese | 6,785 B | 6,789 B |

The old runtime allocated about 1,512 B for a language field plus 377 pointers
while all translation strings remained embedded in the executable. Therefore
the extra dynamic heap cost is 5,992 B for English and 10,029 B in the
worst-case shipped catalog (Japanese).

### Final PS2 net resident-RAM reduction

| Core | Image reduction | Net with English | Net with largest catalog (Japanese) |
| --- | ---: | ---: | ---: |
| CPS1 | 16,096 B | **10,104 B** | **6,067 B** |
| CPS2 | 18,680 B | **12,688 B** | **8,651 B** |
| MVS | 20,784 B | **14,792 B** | **10,755 B** |
| NCDZ | 17,656 B | **11,664 B** | **7,627 B** |

Thus UTF-8 V2 still gives a clear net PS2 RAM reduction for every core and
every shipped language. The conservative worst observed combination is CPS1
with Japanese, which still saves 6,067 B of resident RAM versus the original
embedded system.

## PSP executable/loadable image size

The historical `e56539d` snapshot now also cross-compiles successfully with the
local PSPSDK, so T8 can record real before/after PSP figures instead of only
inferring them from the 32-bit ABI.

| Core | PRX before | PRX final V2 | PRX delta | Loadable before (`text+data+bss`) | Loadable final V2 | Loadable delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPS1 | 4,082,058 B | 4,055,898 B | **-26,160 B** | 6,400,636 B | 6,384,468 B | **-16,168 B** |
| CPS2 | 3,615,682 B | 3,587,450 B | **-28,232 B** | 5,460,580 B | 5,442,900 B | **-17,680 B** |
| MVS | 3,763,114 B | 3,731,538 B | **-31,576 B** | 5,709,312 B | 5,689,548 B | **-19,764 B** |
| NCDZ | 3,758,998 B | 3,732,334 B | **-26,664 B** | 5,593,240 B | 5,576,776 B | **-16,464 B** |

EBOOT deltas are identical to the PRX deltas for these builds because the PBP
wrapper overhead is unchanged. For example, MVS falls from 3,763,490 B to
3,731,914 B.

### Final PSP net resident-RAM reduction

Using the same exact catalog allocations and old 1,512-byte pointer-copy heap
cost:

| Core | Image reduction | Net with English | Net with largest catalog (Japanese) |
| --- | ---: | ---: | ---: |
| CPS1 | 16,168 B | **10,176 B** | **6,139 B** |
| CPS2 | 17,680 B | **11,688 B** | **7,651 B** |
| MVS | 19,764 B | **13,772 B** | **9,735 B** |
| NCDZ | 16,464 B | **10,472 B** | **6,435 B** |

The final UTF-8 design therefore also saves resident RAM on PSP for every core
and every shipped language, while reducing PRX/EBOOT size on disk.

## PS2 startup I/O timing

A temporary, non-committed probe around `ui_text_catalog_load()` was run during
the original T8 V1 measurement in PCSX2 using the normal `host:` build-root
layout, then removed.

Three repeated CPS1/English runs reported:

```text
allocation=7456  load_us=2055  heap_delta=8192
allocation=7456  load_us=2055  heap_delta=8192
allocation=7456  load_us=2055  heap_delta=8192
```

An earlier warm-up run reported 2016 us. That timing is retained as the
external-pack I/O baseline; Phase 3 does not add recurring I/O and changes the
English payload by only 48 bytes. PCSX2 timing is not a substitute for
real-PS2 storage timing.

PS2SDK exposes physical RAM via `GetMemorySize()` but no reliable query for
current EE heap free space, so no fabricated free-RAM-before/after number is
reported. The loadable-image reduction plus exact allocation size gives the
relevant resident-RAM accounting.

## Lookup cost / recurring I/O

`ui_text_catalog_get()` remains a bounds check, one 16-bit offset load and a
pointer addition. It performs no allocation, hash lookup, string comparison or
disk I/O.

UTF-8 decoding occurs only when text is measured/drawn. ASCII stays a one-byte
fast path; Latin-1 code points use the existing `latin1_14` font, and the 557
remaining shipped non-ASCII code points use a compact generated binary-search
lookup into the existing GBK-backed glyph data. No full Unicode font/table is
resident.

## Runtime validation

PSPSDK and PPSSPP validate the final PSP runtime path. PPSSPP boots the final
MVS build with system-language values for English, Japanese, Spanish,
Traditional Chinese and Simplified Chinese; the HLE trace confirms the PSP
language IDs used by NJEMU and no translation-loader warnings are emitted.

PS2 and PSP final feature-rich builds install exactly the same five V2 catalogs.

## T8 acceptance

- one compact catalog allocation: yes;
- no permanent per-message pointer copy: yes;
- no embedded full-language tables: yes;
- no recurring file I/O during GUI rendering: yes;
- clear net PS2 RAM reduction: yes, **6.1-14.8 KiB** depending on core/language;
- clear net PSP RAM reduction: yes, **6.1-13.8 KiB** depending on core/language;
- PSP executable comparison: now measured directly against `e56539d`;
- PSP build/runtime path: validated with PSPSDK + PPSSPP;
- real-hardware free-RAM/storage timing: optional follow-up, not a migration
  blocker.
