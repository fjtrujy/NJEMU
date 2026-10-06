# Releasing NJEMU

NJEMU uses Git tags as the release-version source of truth. Do not add or update independent version constants in C, CMake, platform code, or GitHub Actions.

## Semantic Versioning policy

Release versions use `MAJOR.MINOR.PATCH` and release tags use `vMAJOR.MINOR.PATCH`.

- **MAJOR**: incompatible or intentionally breaking user-visible changes.
- **MINOR**: backwards-compatible features or substantial new capabilities.
- **PATCH**: backwards-compatible fixes.

Pre-release tags are not part of the automated release workflow today. The release workflow accepts strict stable tags such as `v2.4.0` or `v2.5.1`.

## Migration to SemVer

The repository had no Git tags when this policy was introduced, but the existing CMake/README lineage identified the cross-platform tree as `2.4.0`. NJEMU therefore uses `2.4.0` as the migration baseline rather than inventing a new unrelated version or retroactively tagging old commits.

Before the first SemVer tag exists, Git builds identify themselves as:

```text
2.4.0-pre.<commit-count>+g<sha>
```

The first stable SemVer release should be tagged `v2.4.0` once the maintainers consider that source state release-ready. Do not reconstruct historical tags simply to make old versions look SemVer-complete.

## Build version identity

`cmake/NJEMUVersion.cmake` derives the version centrally from Git and is shared by normal builds and release automation.

| Source state | Example |
| --- | --- |
| Exact clean SemVer tag | `2.4.0` |
| 12 commits after `v2.4.0` | `2.4.0+12.gabcdef0` |
| Dirty tree after that tag | `2.4.0+12.gabcdef0.dirty` |
| Git tree before the first SemVer tag | `2.4.0-pre.967+gabcdef0` |
| Source tree without `.git` | `2.4.0+source` |

Untracked files do not mark a build dirty. This is intentional because local ROMs, BIOS files, caches, saves, and other runtime data can exist beside a development checkout without changing the source revision.

For source snapshots without `.git`, `NJEMU_VERSION_OVERRIDE` can provide a known SemVer identity to the version helper. Otherwise the build uses the documented source-archive fallback rather than failing configuration.

The exact derived value is:

- compiled into `VERSION_STR` and visible in normal application/frontend version displays;
- printed to the startup log as `<app> <version> [<core>/<platform>]`;
- installed as `version.txt`;
- embedded as `version.txt` in Vita VPKs;
- included in the PSP EBOOT title while the PSP SDK's numeric application-version field keeps its required numeric format;
- used in release archive names.

Issue reports should copy this exact version when possible.

## Canonical downloadable configuration

Official downloads configure CMake with:

```text
-DNJEMU_CANONICAL_RELEASE=ON
-DCMAKE_BUILD_TYPE=Release
```

The canonical profile is centralized in `CMakeLists.txt`. It enables the stable features users normally want without exposing the CI option matrix.

| Capability | PSP | PS2 | PS Vita | Desktop |
| --- | --- | --- | --- | --- |
| GUI | ON | ON | ON | ON |
| Save states | ON | ON | ON | ON |
| Command list | ON | ON | ON | ON |
| CPS2/MVS streaming-cache capability | ON | ON | OFF | OFF |
| Desktop SDL + OpenGL backends | n/a | n/a | n/a | ON |
| Vita GXM + VitaGL backends | n/a | n/a | Always built | n/a |
| PS2 accelerated cache I/O | n/a | ON for cached cores | n/a | n/a |
| PSP Media Engine audio | OFF | n/a | n/a | n/a |
| PSP MVS Ad Hoc | OFF | n/a | n/a | n/a |
| PS2 external IRX image | n/a | OFF | n/a | n/a |
| Profiling/sanitizer diagnostics | OFF | OFF | OFF | OFF |
| Legacy `RELEASE` content filter | OFF | OFF | OFF | OFF |

Important policy details:

- `PSP_ME_AUDIO` remains outside official downloads because the path is experimental and PPSSPP cannot execute Media Engine code. The canonical PSP EBOOT keeps the normal CPU-audio path.
- PSP MVS Ad Hoc remains an optional developer/specialized build capability rather than changing the recommended package for every user.
- The legacy CMake `RELEASE` option is not the same thing as `CMAKE_BUILD_TYPE=Release`; it filters legacy game/bootleg code paths. Canonical packages leave that content filter disabled.
- PS2 uses the embedded driver image. The external-IRX-image path remains a validation/developer variant.
- Desktop and Vita default to full-resident MVS/CPS2 operation, while PSP/PS2 retain streaming-cache fallback support for constrained memory.

Developers and CI may continue to build other supported combinations directly.

## Release artifacts

`cmake/NJEMUReleasePackage.cmake` combines the four per-core CMake install trees and refuses to package ROMs, BIOS files, cache data, save states, NVRAM, or other non-placeholder runtime data.

A stable release publishes:

```text
njemu-<version>-psp.zip
njemu-<version>-ps2.zip
njemu-<version>-psvita.zip
njemu-<version>-desktop-linux.zip
njemu-<version>-desktop-macos.zip
```

Desktop has two native archives because Linux and macOS executables are not interchangeable; both use the same canonical Desktop feature configuration.

Each archive has a matching `.zip.json` sidecar containing the platform, version, size, and SHA-256 digest. The archive itself contains `release-manifest.json`, `version.txt`, the project README/license, and the relevant binaries/runtime install files. The Vita archive contains the four ready-to-install VPKs rather than duplicating each VPK's embedded runtime tree.

Packaging fixes the archive entry order and ZIP modification timestamps and produces a deterministic `release-manifest.json`. CMake's libarchive backend may still emit host-dependent Unix access/change-time extra fields, so raw ZIP bytes are not promised to be identical across invocations or hosts. The `.zip.json` SHA-256 always identifies the exact produced archive.

## Automated release flow

`.github/workflows/release.yml` is the release authority and publishes two channels from the same canonical package configuration.

### Stable channel

A push of a strict `vMAJOR.MINOR.PATCH` tag:

1. validates that the tag and centrally derived build version match exactly;
2. builds the canonical CPS1/CPS2/MVS/NCDZ configurations for PSP, PS2, Vita, Linux Desktop, and macOS Desktop;
3. installs through CMake's explicit runtime-file manifests;
4. packages and validates the five public archives and checksum sidecars;
5. creates the versioned GitHub Release and marks it as **Latest**;
6. generates the release description automatically from merged pull requests.

A manual `workflow_dispatch` still accepts an existing stable tag and performs the complete build/package validation without publishing. This is the release dry-run path.

### Development channel

Every push to `master` runs the same canonical packaging path. After every successful matrix:

1. the mutable `development` tag is moved to the exact tested `master` commit;
2. the previous Development GitHub prerelease is replaced;
3. the five platform archives and checksum sidecars are attached;
4. the release title exposes the exact Git-derived development version;
5. generated notes describe the changes since the latest stable release when one exists.

Development is always a GitHub **prerelease** and is explicitly not marked as **Latest**, so GitHub's `releases/latest` endpoint continues to resolve only the stable channel.

The workflow uses concurrency cancellation for `master` pushes. If a newer commit arrives while an older Development build is still running, the obsolete release build is cancelled and only the newest source state proceeds.

## Automatic release-note categories

`.github/release.yml` configures GitHub's generated release notes. Pull-request labels determine the section used in stable and Development descriptions:

| Release-note section | PR labels |
| --- | --- |
| **Breaking Changes** | `breaking-change`, `breaking` |
| **Features** | `enhancement`, `feature` |
| **Fixes** | `bug`, `fix` |
| **Other Changes** | any other label/no matching category |

`skip-changelog` and `documentation-only` exclude a pull request from generated release notes. The pull-request template includes the classification checklist so user-visible changes can be categorized before merge.

GitHub generated release notes are PR-oriented. User-visible changes should normally land through pull requests with the appropriate label; direct commits to `master` still produce a Development package, but they do not provide the same structured release-note metadata.

The normal platform CI workflows remain independent and continue exercising the broader developer option matrix. Their project checkout uses full history so development artifacts can identify the nearest SemVer release correctly.

## Creating a stable release

1. Ensure the intended release commit is on `master` and normal CI is green.
2. Confirm merged user-visible pull requests carry the appropriate release-note labels.
3. Confirm the version bump category follows the SemVer policy above. There is no version constant to edit.
4. If desired, run the Release workflow manually against an existing intended tag in a rehearsal context. Do not create throwaway production tags merely for testing.
5. Create and push the final annotated tag, for example:

   ```sh
   git tag -a v2.4.0 -m "NJEMU 2.4.0"
   git push origin v2.4.0
   ```

6. The tag push builds the canonical packages, creates the GitHub Release, attaches all platform archives/checksum sidecars, marks it as Latest, and generates the categorized description automatically.
7. Verify the NJEMU Pages site resolves the new stable release while the Development channel remains available independently.

`CHANGELOG.md` remains a curated project-history document, but publishing a stable release does not depend on manually copying its **Unreleased** section into the GitHub Release description.

Do not publish a real release/tag solely to test workflow syntax or packaging; use local validation and the manual workflow path where appropriate.

## GitHub Pages and downloads

The GitHub Pages site is the normal user-facing download entry point. It reads GitHub Release metadata in the browser and presents two independent channels:

- **Stable** uses GitHub's `/releases/latest` API and therefore resolves the latest non-prerelease SemVer version.
- **Development** uses the mutable `development` prerelease and exposes the newest successfully packaged `master` commit.

Both sections show the exact version, publication date, package size, release-notes link, and direct platform downloads. The binaries remain GitHub Release assets rather than being copied into the Pages deployment.

The existing MVS/CPS2 browser ROM converter is preserved at `converter.html` and remains part of the same Pages deployment.

## Local validation before release changes

At minimum validate:

```sh
cmake -P cmake/NJEMUVersionTests.cmake
cmake -P cmake/NJEMUReleasePackageTests.cmake
git diff --check
```

For build-system/profile changes, configure a canonical Desktop build and inspect the resulting cache/options, then build, run CTest where enabled, install, and verify `version.txt`. Cross-platform release workflow changes should preserve the normal PSP/PS2/Vita/Desktop CI matrix and should be exercised through CI before a real tag is created.
