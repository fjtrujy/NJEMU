# Host Tooling Migration Plan

## Goal

Remove Python as a normal NJEMU build and release dependency where portable C99 or CMake/Git provides a simpler implementation, while preserving generated formats and validation coverage.

This document is the authoritative status for the migration.

## Non-negotiable constraints

- Never use target-built executables as build-time generators in PSP, PS2, PS Vita, or WASM cross-builds.
- Preserve generator inputs and outputs byte-for-byte unless an intentional format change is explicitly documented.
- Keep generated metadata endian-stable and independent of host C structure layout.
- Keep release/install manifests explicit. Never package the local `resources/` tree recursively.
- Do not add third-party host runtime/parser dependencies.
- Python reference implementations are removed only after replacement parity is demonstrated.

## Architecture

NJEMU uses one portable C99 host executable, `njemu-tool`, for project-specific parsing and generation logic. Subcommands share file, text, UTF-8, endian, checksum, and parser helpers rather than duplicating infrastructure.

The emulator and ROM converter configure that executable through `cmake/NJEMUHostTools.cmake`. Native builds may execute ordinary target executables, but cross-builds must not. The host-tool module therefore owns a separate native CMake sub-build and exposes the installed host executable as a build dependency. The sub-build does not inherit the PSP, PS2, Vita, or Emscripten target toolchain. A native compiler can be selected explicitly with `NJEMU_HOST_C_COMPILER`; cross-builds otherwise locate a host `cc`, `clang`, or `gcc` outside the target sysroot.

For direct development/validation the tool can be built independently:

```sh
cmake -S tools/host -B build-host-tools -DCMAKE_BUILD_TYPE=Release
cmake --build build-host-tools --parallel
./build-host-tools/njemu-tool <command> [options]
```

Version derivation and release packaging are intentionally not part of `njemu-tool` unless CMake proves insufficient. Semantic Git identity belongs in CMake/Git logic; release staging, validation, hashing, and archive creation should use CMake primitives.

## Baseline Python inventory and replacement decision

| Python file | Current role | Classification | Replacement |
| --- | --- | --- | --- |
| `tools/build_translations.py` | validates editable catalogs, emits `.lng` V2 packs and Unicode glyph lookup C | build-critical generator | `njemu-tool translations` |
| `tools/build_romcnv_translations.py` | validates ROM converter catalogs and emits C include | build-critical generator | `njemu-tool romcnv-translations` |
| `tools/build_font_asset.py` | validates fixed GBK font tables and emits external bitmap | build-critical generator | `njemu-tool font` |
| `tools/game_metadata.py` | validates TSV/core identity and emits metadata/gamelists | build-critical generator | `njemu-tool game-metadata` |
| `tools/game_database.py` | merges CPS2 metadata plus rominfo and emits unified database/gamelist | build-critical generator | `njemu-tool game-database` |
| `tools/dip_metadata.py` | validates localized DIP JSON and emits compact runtime data | build-critical generator | `njemu-tool dip-metadata` |
| `tools/rominfo.py` | shared textual rominfo parser/parent validator | build-library helper | shared C rominfo parser; optional `rominfo-validate` subcommand for regression/debug use |
| `tools/validate_cps2_cache_layouts.py` | validates converter-only cache layout TSV | build-time validation | `njemu-tool validate-cps2-cache` |
| `tools/compare_frames.py` | 5-bit renderer frame comparison and optional diff PPM | developer validation | `njemu-tool compare-frames` |
| `tools/njemu_version.py` | SemVer/Git/source-archive build identity | configure/release orchestration | CMake + Git (`cmake/NJEMUVersion.cmake`) |
| `tools/package_release.py` | validates four install trees and creates release ZIP/manifest/sidecar | release orchestration | CMake script using explicit staging, hashing and `cmake -E tar` |

## Invocation inventory

At the migration baseline:

- top-level `CMakeLists.txt` requires Python at configure time;
- translation, font, game metadata/database, and DIP custom commands invoke Python;
- Desktop CTest invokes Python generator, packaging/version, translation, rominfo, and cache tests;
- `romcnv/CMakeLists.txt` requires Python for translations, MVS metadata, CPS2 database, and cache-layout validation;
- PSP CI invokes the translation script explicitly in addition to CMake;
- PSP, PS2, Vita, and release workflows install Python because builds/package creation require it;
- release validation invokes `njemu_version.py` and `package_release.py` directly;
- the Python generators import only project Python modules and the Python standard library; there are no third-party Python package dependencies.

## Compatibility contracts

### Translation packs

Preserve `NJTL` V2, stable numeric IDs, FNV-1a schema hash, little-endian header/offsets, `<NULL>`, source escapes, graphic-token private-use codepoints, printf contracts, UTF-8 validation, GBK font coverage, deterministic Unicode glyph lookup source, and exact catalog order.

### Font asset

Preserve exactly `0x5e80` 14x14 4bpp glyphs (`98` bytes each), arithmetic `gbk_s14_pos` offsets, uniform metrics, the `0x7dc0` GBK table validation, and byte-identical `gbk_s14.bin` output.

### Game metadata/database

Preserve TSV syntax, canonical set identity checks, sorting, string deduplication, little-endian serialized records, CRC32, all CPS2 rominfo topology constraints, runtime capacity checks, and generated gamelist text.

### DIP metadata

Preserve the current JSON schema, locale/profile structural validation, sorted profile order, string deduplication, little-endian `NJDP` V1 layout, and CRC32.

### Version identity

Preserve strict stable tags, nearest merged SemVer tag selection, exact-tag behavior, post-tag build metadata, tracked dirty-state handling (untracked files ignored), pre-first-tag fallback, archive fallback, explicit override, Git SHA, and numeric major/minor/patch values.

### Release packaging

Preserve the five canonical platform archive names, four-core completeness, Vita VPK-only package semantics, project/version/manifest files, SHA-256 sidecars, forbidden runtime-data rejection, executable permission preservation where supported, and deterministic archive ordering. Archive timestamps should be normalized when the available CMake version supports it; reproducible contents and ordering remain mandatory.

## Migration phases

### Phase 0 - audit and architecture

Status: **complete**

- [x] audit repository state and instructions;
- [x] inventory maintained Python files and direct invocations;
- [x] classify each Python tool;
- [x] add native host-tool sub-build architecture;
- [x] document manual host-tool invocation.

### Phase 1 - build-time generators

Status: **complete**

- [x] font asset (C output is byte-identical to the Python reference);
- [x] translations and Unicode glyph lookup (all five packs and every per-core generated Unicode source are byte-identical to Python);
- [x] ROM converter translations (generated C include is byte-identical to Python);
- [x] game metadata for CPS1/CPS2/MVS/NCDZ (binary and gamelist parity);
- [x] rominfo parser (CPS2 topology regression: 286 games / 1387 regions / 5382 ROM records);
- [x] CPS2 unified game database (byte-identical binary and gamelist parity);
- [x] DIP metadata for CPS1/MVS (byte-identical `NJDP` output);
- [x] migrate emulator and ROM converter CMake generators to the native host-tool sub-build;
- [x] prove old/new output parity before deleting references.

Exit condition: a normal emulator or ROM converter build requires no Python and every cross-build executes a native host tool.

### Phase 2 - validation/developer tools

Status: **complete**

- [x] CPS2 cache-layout validator (acceptance/output parity on canonical data);
- [x] frame comparator (exit status and diff PPM parity on tie/real-difference fixtures);
- [x] migrate generator/format regression coverage to native host tests/CTest;
- [x] migrate cache-reader/PCM test harnesses to C without weakening coverage.

### Phase 3 - version derivation

Status: **complete**

- [x] implement shared CMake/Git version helper;
- [x] port exact-tag, post-tag, no-tag, archive, override, and dirty regression cases;
- [x] migrate CMake and release workflow;
- [x] remove `tools/njemu_version.py` after parity.

### Phase 4 - release packaging

Status: **complete**

- [x] implement CMake release packaging/validation script;
- [x] preserve archive structure, manifest, SHA-256 sidecar, and forbidden-data checks;
- [x] port package regression cases;
- [x] migrate release workflow;
- [x] remove `tools/package_release.py` after parity.

`cmake -E tar` normalizes ZIP modification times and the packager fixes file ordering and manifest content. Its libarchive backend may add host-dependent Unix access/change-time extra fields, so raw ZIP bytes are not guaranteed identical between invocations; package contents, order, manifest semantics, and the per-build sidecar digest remain authoritative.

### Phase 5 - final Python removal and validation

Status: **complete locally; real console/WASM toolchain execution remains CI-only until these commits are pushed**

- [x] remove remaining project-maintained Python tests after equivalent native/CMake coverage was added;
- [x] remove `find_package(Python3)` and Python CI setup no longer needed;
- [x] explain the remaining Python text matches here;
- [x] build/test all four Desktop cores under the canonical release feature profile;
- [x] verify cross-build host-tool provenance with a synthetic CMake cross toolchain; no PSP, PS2, or Vita SDK is installed in the local environment, so real console builds remain CI coverage;
- [x] validate native CPS2/MVS ROM converter builds; Emscripten is not installed locally, so the WASM build remains CI coverage while its workflow/configuration contains no Python dependency;
- [x] validate canonical release packaging using the four real Desktop install trees;
- [x] run `git diff --check` and final Python-reference audit;
- [x] verify no `resources/` path is modified or staged.

## Final local validation

Post-removal validation was repeated from clean build directories against commit `5c17768` and the final documentation update:

- standalone `njemu-host-tests` passed the metadata, rominfo, game-database, DIP, and translation suites;
- a synthetic cross-compilation probe used a dedicated target-compiler wrapper with `CMAKE_CROSSCOMPILING=TRUE`, while the `njemu_host_tools` external project selected `/usr/bin/cc` and produced a native macOS arm64 `njemu-tool`; this proves target executables are not used as build-time generators;
- native ROM converter builds passed for CPS2 and MVS, including native host-tool generation of ROMCNV translations, metadata/database content, and CPS2 cache-layout validation;
- canonical Desktop builds and CTests passed for all four cores: CPS1 `28/28`, CPS2 `29/29`, MVS `33/33`, and NCDZ `27/27`;
- all four Desktop install trees were successfully generated with version `2.4.0-pre.979+g5c177685` during this validation run;
- `cmake/NJEMUReleasePackage.cmake` successfully created the canonical `desktop-macos` archive from those real install trees, with four cores, generated metadata/translations/font assets, placeholders only in runtime-data directories, release manifest, and SHA-256 sidecar;
- `find . -name '*.py'` returns no project-maintained Python sources, and active CMake/workflow/test/tooling paths contain no Python dependency;
- `git diff --check` is clean and no path under `resources/` is modified or staged.

The local machine does not provide PSPDEV, PS2DEV/PS2SDK, VitaSDK, or Emscripten. The corresponding GitHub Actions matrices are therefore the remaining execution environment for real PSP/PS2/Vita/WASM compiler coverage. The current branch is intentionally not pushed as part of this milestone unless explicitly requested, so those CI jobs cannot yet exercise these local commits.

## Remaining Python references

There is no project-maintained Python file or active Python build/test/release dependency.
The `.py` names in the baseline inventory above are intentionally retained as the historical
mapping from removed tools to their replacements. `docs/MVS_CACHE_IO_INVESTIGATION.md` also
mentions the external PS2SDK test `ps2sdk/iop/fs/libbdm/tests/test_bd_cache.py`; that path is
documentation about another repository and is not an NJEMU dependency.
