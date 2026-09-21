# Translation T9 cross-build fragility matrix

Final UTF-8 V2 refresh performed on 2026-09-21 after Encoding Phase 3.

The purpose of T9 is to ensure translation identity, pack contents and renderer
support do not accidentally depend on core/platform compilation flags.

## Desktop matrix

All 16 GUI builds below compiled successfully and passed the five
translation-specific tests used by the final matrix:

- `ui_text_id_tests`
- `ui_text_catalog_tests`
- `ui_utf8_tests`
- `translation_source_validation`
- `translation_pack_tests`

For every core, all four combinations of `SAVE_STATE={ON,OFF}` and
`COMMAND_LIST={ON,OFF}` were tested:

| Core | SAVE_STATE | COMMAND_LIST | Result |
| --- | --- | --- | --- |
| CPS1 | ON | ON | pass |
| CPS1 | ON | OFF | pass |
| CPS1 | OFF | ON | pass |
| CPS1 | OFF | OFF | pass |
| CPS2 | ON | ON | pass |
| CPS2 | ON | OFF | pass |
| CPS2 | OFF | ON | pass |
| CPS2 | OFF | OFF | pass |
| MVS | ON | ON | pass |
| MVS | ON | OFF | pass |
| MVS | OFF | ON | pass |
| MVS | OFF | OFF | pass |
| NCDZ | ON | ON | pass |
| NCDZ | ON | OFF | pass |
| NCDZ | OFF | ON | pass |
| NCDZ | OFF | OFF | pass |

CPS2 and MVS exercise the cache-enabled core configuration; CPS1 and NCDZ
exercise the non-cache translation paths.

Every Desktop build generated exactly the same five UTF-8 V2 packs. The
SHA-256 digest of the ordered five-pack hash set was identical in all 16 builds:

```text
7c1f8343c876e431209f5d686ee7737c56aa00bb523b7141e7180e13a2c184ff
```

## PS2 matrix

The same 16 GUI/core/flag combinations were cross-compiled successfully with
the current PS2 toolchain:

- CPS1: 4/4
- CPS2: 4/4
- MVS: 4/4
- NCDZ: 4/4

All 16 PS2 builds generated the exact same V2 five-pack set as Desktop,
including the aggregate digest above.

This proves that core selection, `SAVE_STATE`, `COMMAND_LIST`, platform
selection and cache-enabled cores do not alter stable IDs, catalog contents,
UTF-8 pack schema or generated Unicode glyph coverage.

## Stable IDs and UTF-8 renderer

`tests/ui_text_id_tests.c` locks representative IDs from across the namespace,
including normalized stretch/button/autofire/reset-help/ROM-info groups and
`UI_TEXT_MAX`. It is executed in every Desktop matrix configuration.

The final renderer also removes a previous fragility risk: Latin-1 rendering is
now available independently of `COMMAND_LIST`. The matrix exercises both
states on every Desktop and PS2 core. The generated CJK/non-Latin-1 map is built
from the same unconditional five catalogs in every configuration.

## Generator/loader robustness

`tests/translation_pack_tests.py` now contains 28 tests. Together with the C
loader/UTF-8 tests, coverage includes:

- complete catalog success;
- missing/extra/duplicate/reordered key rejection;
- unsupported escape and embedded-NUL-escape rejection;
- invalid UTF-8 source rejection;
- missing-font-glyph rejection;
- `printf` contract mismatch rejection;
- V2 string-blob overflow rejection;
- deterministic all-language generation and round trips;
- schema-hash stability;
- PUA graphic-token encoding;
- Latin-1 direct-font coverage;
- compact Unicode map coverage;
- bad magic/version/language/count/reserved/size/schema;
- invalid UTF-8 runtime payload rejection;
- out-of-range offsets, truncation and missing NUL termination;
- real pack loading, requested-language -> English fallback and
  missing-English failure.

## PSP / ADHOC

The final V2 renderer/build was cross-compiled with the local PSPSDK using the
feature combinations that are most sensitive to platform/flag coupling:

| Core | ADHOC | SAVE_STATE | COMMAND_LIST | Result |
| --- | --- | --- | --- | --- |
| CPS1 | ON | ON | OFF | pass |
| CPS2 | ON | ON | OFF | pass |
| MVS | ON | ON | OFF | pass |
| NCDZ | OFF | ON | ON | pass |

NCDZ does not support ADHOC. CPS1/CPS2/MVS therefore exercise both the PSP
networking path and the `COMMAND_LIST=OFF` path where Latin-1 must still be
available.

Every PSP build generated the same aggregate V2 pack digest as Desktop and PS2.

## PPSSPP runtime validation

The final PSP MVS build was booted from isolated runtime directories with
adjacent `lang/` packs. PPSSPP system-language overrides and its HLE trace
confirmed NJEMU receives the expected PSP language IDs:

| PSP system language | PSP value | Catalog selected | Result |
| --- | ---: | --- | --- |
| English | `1` | `lang/en.lng` | previously validated |
| Japanese | `0` | `lang/ja.lng` | pass |
| Spanish | `3` | `lang/es.lng` | pass |
| Traditional Chinese | `10` / `0x0a` | `lang/zh-Hant.lng` | pass |
| Simplified Chinese | `11` / `0x0b` | `lang/zh-Hans.lng` | pass |

The four final Phase-3 non-English boots emitted no translation-loader warning.
The earlier requested-language -> English fallback smoke remains valid because
the loader path and fallback semantics are unchanged by V2.

## T9 result

T9 is complete for final UTF-8 V2. The same prebuilt language packs are valid
across all four cores and tested Desktop, PS2 and PSP feature combinations,
including PSP ADHOC. Stable IDs, V2 pack bytes and generated glyph coverage are
independent of `EMU_SYSTEM`, `SAVE_STATE`, `COMMAND_LIST`, cache use,
`ADHOC` and platform-specific storage code.
