# S0 Post-Vita PSP/PS2 Baseline

Date: 2026-09-28

Commit under test: `72b8e5f Add size and portability optimization plan`

This report freezes the post-Vita PSP/PS2 binary-size baseline before any PNG or compression implementation changes. All measurements below come from the `memoryImprovements` branch at the commit above. Build directories and logs are intentionally untracked.

## Build matrix

"Full" means `GUI=ON SAVE_STATE=ON COMMAND_LIST=ON ADHOC=OFF`.

| Platform | Target / variant | GUI | Save state | Command list | .text | .rodata | .data | .bss | Runtime image | ELF | PRX | PBP |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| PSP | CPS1 full | ON | ON | ON | 837,604 | 102,072 | 290,384 | 2,306,828 | 3,540,580 | 3,038,428 | 1,466,122 | 1,476,190 |
| PSP | CPS2 full | ON | ON | ON | 774,664 | 95,856 | 18,656 | 1,767,740 | 2,660,448 | 2,541,300 | 1,033,582 | 1,043,167 |
| PSP | MVS minimal | OFF | OFF | OFF | 709,364 | 43,504 | 12,256 | 1,936,508 | 2,705,100 | 2,417,756 | 893,226 | 902,495 |
| PSP | MVS GUI | ON | OFF | OFF | 823,496 | 81,744 | 22,288 | 1,954,812 | 2,886,088 | 2,635,124 | 1,079,042 | 1,088,311 |
| PSP | MVS GUI + command | ON | OFF | ON | 841,288 | 94,768 | 22,288 | 1,954,812 | 2,916,996 | 2,684,740 | 1,115,178 | 1,124,447 |
| PSP | MVS GUI + save | ON | ON | OFF | 845,084 | 81,920 | 22,288 | 1,954,812 | 2,907,900 | 2,669,540 | 1,108,274 | 1,117,543 |
| PSP | MVS full | ON | ON | ON | 863,052 | 95,380 | 22,288 | 1,954,812 | 2,939,420 | 2,720,172 | 1,145,226 | 1,154,495 |
| PSP | NCDZ full | ON | ON | ON | 866,660 | 137,008 | 10,176 | 1,913,244 | 2,930,848 | 2,739,944 | 1,166,214 | 1,174,605 |
| PS2 | CPS1 full | ON | ON | ON | 955,960 | 108,460 | 675,640 | 2,076,496 | 3,816,616 | 3,757,028 | - | - |
| PS2 | CPS2 full | ON | ON | ON | 880,648 | 102,172 | 403,624 | 1,538,480 | 2,924,984 | 3,341,812 | - | - |
| PS2 | MVS minimal | OFF | OFF | OFF | 814,160 | 47,856 | 390,488 | 1,705,568 | 2,958,132 | 3,230,876 | - | - |
| PS2 | MVS GUI | ON | OFF | OFF | 935,792 | 87,348 | 407,272 | 1,725,536 | 3,156,008 | 3,426,360 | - | - |
| PS2 | MVS GUI + command | ON | OFF | ON | 955,856 | 100,404 | 407,272 | 1,725,600 | 3,189,192 | 3,473,584 | - | - |
| PS2 | MVS GUI + save | ON | ON | OFF | 963,688 | 87,892 | 407,272 | 1,725,728 | 3,184,640 | 3,456,464 | - | - |
| PS2 | MVS full | ON | ON | ON | 983,976 | 101,236 | 407,272 | 1,725,792 | 3,218,336 | 3,504,216 | - | - |
| PS2 | NCDZ full | ON | ON | ON | 946,152 | 143,316 | 395,120 | 1,683,072 | 3,167,720 | 3,629,828 | - | - |

Sizes are bytes. `.text`, `.rodata`, `.data`, and `.bss` are the named ELF sections reported by the target GNU `size -A` tool. "Runtime image" is the normal GNU `size` `dec` value (`text + data + bss`); it is useful as a stable comparison metric, not as a claim that every byte is simultaneously resident on every platform.

The uncompressed PSP ELF is recorded separately from the final PRX/PBP because PRX relocation/packaging changes on-disk size substantially without changing the linked section baseline.

## MVS feature deltas

Relative to MVS minimal:

| Platform | Variant | Runtime delta | ELF delta | Final package delta |
|---|---|---:|---:|---:|
| PSP | GUI | +180,988 | +217,368 | +185,816 PRX/PBP |
| PSP | GUI + command | +211,896 | +266,984 | +221,952 PRX/PBP |
| PSP | GUI + save | +202,800 | +251,784 | +215,048 PRX/PBP |
| PSP | full | +234,320 | +302,416 | +252,000 PRX/PBP |
| PS2 | GUI | +197,876 | +195,484 | +195,484 ELF |
| PS2 | GUI + command | +231,060 | +242,708 | +242,708 ELF |
| PS2 | GUI + save | +226,508 | +225,588 | +225,588 ELF |
| PS2 | full | +260,204 | +273,340 | +273,340 ELF |

This confirms that both `SAVE_STATE` and `COMMAND_LIST` materially affect the image and should remain represented in later size comparisons.

## PNG object contribution

The direct platform PNG object is small compared with the compression libraries:

| Platform | Target | PNG object text | data | bss | total |
|---|---|---:|---:|---:|---:|
| PSP | CPS1 | 1,834 | 12 | 4 | 1,850 |
| PSP | CPS2 | 2,150 | 12 | 16 | 2,178 |
| PSP | MVS | 2,142 | 12 | 16 | 2,170 |
| PSP | NCDZ | 7,989 | 20 | 4 | 8,013 |
| PS2 | CPS1 | 2,199 | 0 | 4 | 2,203 |
| PS2 | CPS2 | 2,783 | 0 | 16 | 2,799 |
| PS2 | MVS | 2,775 | 0 | 16 | 2,791 |
| PS2 | NCDZ | 9,376 | 0 | 4 | 9,380 |

NCDZ is larger because it includes PNG loading/decoding in addition to screenshot encoding.

## Compression symbols linked today

Representative MVS full builds contain both miniz and zlib code. Approximate symbol-family totals from `nm -S`:

| Family | PSP symbols / bytes | PS2 symbols / bytes |
|---|---:|---:|
| miniz ZIP reader / validation / extraction | 40 / 24,752 | 40 / 24,436 |
| miniz ZIP writer | 26 / 26,104 | 26 / 26,148 |
| miniz tdefl | 24 / 22,368 | 24 / 23,624 |
| miniz tinfl | 6 / 10,352 | 6 / 11,292 |
| miniz zlib-compatible API | 18 / 5,696 | 18 / 6,212 |
| zlib deflate API/implementation | 19 / 19,576 | 22 / 15,122 |
| zlib crc32 family | 7 / 3,036 | 7 / 2,300 |

The exact family totals are heuristic grouping by symbol name, but the conclusion is unambiguous: ZIP writer and miniz deflate/tdefl code are linked even though NJEMU's runtime ZIP abstraction is reader-only, while zlib deflate/CRC code is also present. This is the duplication S2/S3 should target after S1.

Examples present in both console builds include `mz_zip_writer_init*`, `mz_zip_writer_add_*`, `mz_zip_writer_finalize_archive`, `tdefl_*`, `deflate*`, and `crc32_z`.

## Large symbols

The largest stable application-owned symbols in the full builds are dominated by emulator RAM/caches and generated CPU tables rather than PNG. Representative examples:

- PSP CPS1: `cps1_scroll_pen_usage` 512 KiB, `gulist` 300 KiB, `JumpTable` 256 KiB, `C68k_Exec` about 202 KiB, `cps1_gfxram` 192 KiB.
- PSP MVS/NCDZ: `gulist` 300 KiB, `vertices_spr` 288 KiB, `JumpTable` 256 KiB, `C68k_Exec` about 202 KiB, `neogeo_videoram` 128 KiB, `lfo_pm_table` 128 KiB.
- PS2 shows the same dominant application symbols, with `C68k_Exec` about 208 KiB. Linker-generated symbols with misleading wrapped sizes (for example `_gp` / `_fbss`) are excluded from this interpretation.

## Reproduction

Toolchains used:

```sh
export PS2DEV=/Users/fjtrujy/toolchains/ps2/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export GSKIT=$PS2DEV/gsKit
export PATH=$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin

PS2_TOOLCHAIN=$PS2DEV/share/ps2dev.cmake
PSP_TOOLCHAIN=/Users/fjtrujy/Projects/toolchains/psp/pspdev/psp/share/pspdev.cmake
```

Representative full builds:

```sh
cmake -S . -B build_s0_psp_mvs_full \
  -DCMAKE_TOOLCHAIN_FILE="$PSP_TOOLCHAIN" \
  -DTARGET=MVS -DPLATFORM=PSP \
  -DGUI=ON -DSAVE_STATE=ON -DCOMMAND_LIST=ON -DADHOC=OFF
cmake --build build_s0_psp_mvs_full -j4

cmake -S . -B build_s0_ps2_mvs_full \
  -DCMAKE_TOOLCHAIN_FILE="$PS2_TOOLCHAIN" \
  -DTARGET=MVS -DPLATFORM=PS2 \
  -DGUI=ON -DSAVE_STATE=ON -DCOMMAND_LIST=ON -DADHOC=OFF
cmake --build build_s0_ps2_mvs_full -j4
```

The other full targets only change `TARGET`. The MVS feature-isolation builds use:

```text
minimal:        GUI=OFF SAVE_STATE=OFF COMMAND_LIST=OFF
GUI:            GUI=ON  SAVE_STATE=OFF COMMAND_LIST=OFF
GUI + command:  GUI=ON  SAVE_STATE=OFF COMMAND_LIST=ON
GUI + save:     GUI=ON  SAVE_STATE=ON  COMMAND_LIST=OFF
full:           GUI=ON  SAVE_STATE=ON  COMMAND_LIST=ON
```

Measurement commands:

```sh
# PSP
psp-size -A build_s0_psp_mvs_full/MVS
psp-size build_s0_psp_mvs_full/MVS
psp-nm -S --size-sort build_s0_psp_mvs_full/MVS
stat -f%z build_s0_psp_mvs_full/{MVS,MVS.prx,EBOOT.PBP}

# PS2
mips64r5900el-ps2-elf-size -A build_s0_ps2_mvs_full/MVS
mips64r5900el-ps2-elf-size build_s0_ps2_mvs_full/MVS
mips64r5900el-ps2-elf-nm -S --size-sort build_s0_ps2_mvs_full/MVS
stat -f%z build_s0_ps2_mvs_full/MVS
```

For the direct PNG object cost, run the appropriate `*-size` tool on
`CMakeFiles/<TARGET>.dir/src/<platform>/png.c.obj`.

## Functional baseline

A fresh Desktop MVS test build with GUI, save states, command list, and testing enabled passed all existing tests:

```text
16/16 tests passed
zip_archive_tests: passed
frame pacing / UI / UTF-8 / font / memory / input / power tests: passed
```

The exact CTest run completed with 100% pass rate and 0 failures.

Current coverage gap relevant to later phases:

- there is no dedicated PNG encode/decode/round-trip/corruption test yet;
- there is no frozen save-state compression compatibility test yet;
- ZIP reader behavior is covered by `zip_archive_tests`;
- test fixtures create ZIPs with miniz writer APIs, but that test-only usage does not justify writer code in console runtime binaries.

S1 should add focused PNG tests before changing the codec. S2 should add save-state compatibility coverage before replacing zlib.

## S0 conclusion

S0 changes no implementation. The post-Vita PSP/PS2 baseline is now frozen and reproducible.

The data supports the next planned step, S1: unify PNG first without changing the compression backend, then compare every affected console build against this report before proceeding to S2.
