# Releasing NJEMU

NJEMU uses Git tags as the release-version source of truth. Do not add or update independent version constants in C, CMake, platform code, or GitHub Actions.

## Semantic Versioning policy

Release versions use `MAJOR.MINOR.PATCH` and release tags use `vMAJOR.MINOR.PATCH`.

- **MAJOR**: incompatible or intentionally breaking user-visible changes.
- **MINOR**: backwards-compatible features or substantial new capabilities.
- **PATCH**: backwards-compatible fixes.

Pre-release tags are not part of the automated release workflow today. The release workflow accepts strict stable tags such as `v3.0.0` or `v3.0.1`.

## Migration to SemVer

The repository had no Git tags when this policy was introduced, but the existing CMake/README lineage identified the cross-platform tree as `2.4.0`. NJEMU therefore uses `2.4.0` as the migration baseline rather than inventing a new unrelated version or retroactively tagging old commits.

Before the first SemVer tag existed, Git builds identified themselves as:

```text
2.4.0-pre.<commit-count>+g<sha>
```

The first stable SemVer release is `v3.0.0`, reflecting the stabilized
multi-platform release line. `2.4.0` remains a historical migration
baseline, not a published stable Git tag. With the first stable release
published, **untagged source archives** use `3.0.0` as their fallback
version (and should pass `NJEMU_VERSION_OVERRIDE` for any later release).
Do not reconstruct historical tags simply to make old versions look
SemVer-complete.

## Build version identity

`cmake/NJEMUVersion.cmake` derives the version centrally from Git and is shared by normal builds and release automation.

The build keeps two related identities:

- a machine-oriented artifact identity used by packaging and release automation;
- a human-readable runtime identity shown by the emulator as `X.Y.Z (buildnumber)`,
  where `buildnumber` is the short commit SHA.

Artifact identities remain SemVer-compatible:

| Source state | Artifact identity |
| --- | --- |
| Exact clean SemVer tag | `3.0.0` |
| 12 commits after `v3.0.0` | `3.0.0+12.gabcdef0` |
| Dirty tree after that tag | `3.0.0+12.gabcdef0.dirty` |
| Git tree before the first SemVer tag (historical) | `2.4.0-pre.967+gabcdef0` |
| Source tree without `.git` | `3.0.0+source` |

For the same Git-backed builds, the runtime/frontend version is simply:

```text
3.0.0 (abcdef01)
```

NJEMU currently uses up to eight hexadecimal characters for this short SHA.

The native ROMCNV converters and the browser ROM converter use the same
Git-derived human-readable version. The separately maintained cache-header
signature (`CPS2V24`/`MVS_V24`) and save-state signatures
(`CPS1SV23`, `CPS2SV23`, `MVS_SV23`, `NCDZSV23`) are **file format
versions**, not release versions. They must not change when a new SemVer
tag is published unless the corresponding serialized format becomes
incompatible.

Untracked files do not mark a build dirty. This is intentional because local ROMs, BIOS files, caches, saves, and other runtime data can exist beside a development checkout without changing the source revision.

For source snapshots without `.git`, `NJEMU_VERSION_OVERRIDE` can provide a known
SemVer identity to the version helper. `NJEMU_GIT_SHA_OVERRIDE` can additionally
provide the source commit so the runtime still shows `X.Y.Z (short-sha)`.
GitHub Actions' `GITHUB_SHA` is accepted automatically for this purpose. If no
commit identity is available at all, the runtime uses `X.Y.Z (unknown)` rather
than pretending that the snapshot came from a known revision.

The human-readable `X.Y.Z (short-sha)` value is:

- compiled into `VERSION_STR` and visible in normal application/frontend version displays;
- printed to the startup log as `<app> <version> [<core>/<platform>]`;
- included in the PSP EBOOT title while the PSP SDK's numeric application-version field keeps its required numeric format;

The artifact identity remains installed as `version.txt`, embedded as
`version.txt` in Vita VPKs, and used in release archive names so those machine
interfaces stay SemVer-safe and uniquely identify development artifacts.

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
| PSP Media Engine audio | ON | n/a | n/a | n/a |
| PSP MVS Ad Hoc | OFF | n/a | n/a | n/a |
| PS2 external IRX image | n/a | OFF | n/a | n/a |
| Profiling/sanitizer diagnostics | OFF | OFF | OFF | OFF |
| Legacy `RELEASE` content filter | OFF | OFF | OFF | OFF |

Important policy details:

- The canonical PSP package enables `PSP_ME_AUDIO`. The validated target-specific ME path retains Main CPU as a first-class runtime/failure fallback, while CI keeps explicit `PSP_ME_AUDIO=OFF` artifacts as the strongest CPU-only/PPSSPP reference configuration.
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

The PSP and PS2 archives explicitly preserve `CPS2/cache/`, `MVS/processed/`
(the canonical MVS converted-asset root), and `MVS/cache/` (a compatibility
fallback for older converted assets). CPS1 and NCDZ have no streaming cache
and must not ship a `cache/` directory. Desktop canonical builds use resident
CPS2 graphics and therefore do not ship CPS2 or MVS `cache/` directories;
Desktop MVS still needs `processed/`. The ZIP stores a zero-length
`_placeholder` in each required runtime directory and an explicit directory
entry, so both archive browsers and extracted packages expose the layout.
Release packaging rejects stale install trees with missing or unexpected
`cache/` or `processed/` directories. Vita packages carry the target-specific
runtime layout inside their individual VPKs.

The rolling `development` GitHub Release may retain assets from earlier
commits because their versioned filenames differ. GitHub Pages derives the
active development version from the release title and links only ZIPs whose
filenames match it exactly; old assets must never be selected just because
their platform suffix matches.

Packaging fixes the archive entry order and ZIP modification timestamps and produces a deterministic `release-manifest.json`. CMake's libarchive backend may still emit host-dependent Unix access/change-time extra fields, so raw ZIP bytes are not promised to be identical across invocations or hosts. The `.zip.json` SHA-256 always identifies the exact produced archive.

## Automated release flow

`.github/workflows/release.yml` is the release authority and publishes two channels from the same canonical package configuration.

### Stable channel

A push of a strict `vMAJOR.MINOR.PATCH` tag:

1. validates that the tag and centrally derived build version match exactly;
2. starts PSP, PS2, Vita, Linux Desktop, and macOS Desktop jobs independently; within each job, builds the four canonical CPS1/CPS2/MVS/NCDZ configurations in bounded parallel batches;
3. installs through CMake's explicit runtime-file manifests;
4. packages and validates the five public archives and checksum sidecars;
5. creates the versioned GitHub Release and marks it as **Latest**;
6. generates the release description automatically from merged pull requests.

A manual `workflow_dispatch` still accepts an existing stable tag and performs the complete build/package validation without publishing. This is the release dry-run path.

### Parallel core builds

`tools/ci/build_release_cores.sh` builds and installs the four emulator cores
into independent `build-release-<core>` and `release-stage/<CORE>` trees.
Each platform job installs its toolchain dependencies just once and then
normally runs **two core builds simultaneously**, allocating a share of
the runner's CPUs to each. The PS2 per-core cache, driver-image, and
sprite-upload feature checks remain mandatory.

To tune or troubleshoot the release runner:

- `NJEMU_RELEASE_CORE_CONCURRENCY=1|2|4` changes the number of simultaneous
  emulator builds (default 2, capped by the available CPU count).
- `NJEMU_RELEASE_CORE_BUILD_JOBS=N` sets the parallel compiler jobs within
  each emulator build (default CPU count divided by concurrent cores).

Core-specific logs are retained in `build_release_core_logs/`; on failure the
relevant log tail is reported and that platform's packaging step is skipped.
The final publish job still requires **all five complete platform archives**
and their checksum sidecars. Installations remain isolated per core, and
an incomplete build can never be published.

### Development channel

Every push to `master` runs the same canonical packaging path. After every successful matrix:

1. the mutable `development` tag is moved to the exact tested `master` commit;
2. the existing Development GitHub prerelease is updated in place (only its replaceable asset files are refreshed);
3. the five platform archives and checksum sidecars are attached;
4. the release title exposes the exact Git-derived development version;
5. commit-based release notes list changes since the nearest preceding stable tag.

Development is always a GitHub **prerelease** and is explicitly not marked as **Latest**, so GitHub's `releases/latest` endpoint continues to resolve only the stable channel.

The workflow uses concurrency cancellation for `master` pushes. If a newer commit arrives while an older Development build is still running, the obsolete release build is cancelled and only the newest source state proceeds.

## Commit-based release notes

The publish job checks out the **exact tested source commit** and generates
Markdown with the portable C99 command `njemu-tool release-notes`. The notes
include all non-merge commit subjects and links between the nearest preceding
stable tag and the release commit, plus a GitHub compare link. This includes
direct `master` commits, squashed PRs, and work that GitHub's PR-only
`--generate-notes` mechanism omitted in previous releases.

The `development` prerelease lists the commits since the latest stable tag and
identifies the exact source SHA in its title. Stable releases remain immutable:
re-running their workflow updates the changelog and adds only missing assets,
without replacing existing downloads or resetting their counts. Development
asset files can change on every build, but the existing prerelease is updated
rather than deleted and recreated.

Pull-request labels remain useful for reviewing changes, but they do not filter
or determine the automated changelog. Keep commit subjects descriptive enough
to be useful as user-visible release entries.

The normal platform CI workflows remain independent and continue exercising the broader developer option matrix. Their project checkout uses full history so development artifacts can identify the nearest SemVer release correctly.

## Creating a stable release

1. Ensure the intended release commit is on `master` and normal CI is green.
2. Confirm the intended commits have useful summaries for the release notes.
3. Confirm the version bump category follows the SemVer policy above. There is no version constant to edit.
4. If desired, run the Release workflow manually against an existing intended tag in a rehearsal context. Do not create throwaway production tags merely for testing.
5. Create and push the final annotated tag, for example:

   ```sh
   git tag -a v3.0.0 -m "NJEMU 3.0.0"
   git push origin v3.0.0
   ```

6. The tag push builds the canonical packages, creates the GitHub Release, attaches all platform archives/checksum sidecars, marks it as Latest, and generates a Git-based changelog against the previous tag.
7. Verify the NJEMU Pages site resolves the new stable release while the Development channel remains available independently.

`CHANGELOG.md` remains a curated project-history document, but publishing a stable release does not depend on manually copying its **Unreleased** section into the GitHub Release description.

Do not publish a real release/tag solely to test workflow syntax or packaging; use local validation and the manual workflow path where appropriate.

## GitHub Pages and downloads

The GitHub Pages site is the normal user-facing download entry point. It reads GitHub Release metadata in the browser and presents two independent channels:

- **Stable** uses GitHub's `/releases/latest` API and therefore resolves the latest non-prerelease SemVer version.
- **Development** uses the mutable `development` prerelease and exposes the newest successfully packaged `master` commit.

The website labels the stable version with its immutable `vMAJOR.MINOR.PATCH` tag,
provides a browsable tagged-release history, and identifies Development as a
rolling build from `master` (with its short commit SHA on newly published
snapshots). Both sections show the version, publication date, package size,
release-notes link, and direct platform downloads. The binaries remain GitHub Release assets rather than being copied into the Pages deployment.

The existing MVS/CPS2 browser ROM converter is preserved at `converter.html` and remains part of the same Pages deployment.

## Local validation before release changes

At minimum validate:

```sh
cmake -P cmake/NJEMUVersionTests.cmake
cmake -P cmake/NJEMUReleasePackageTests.cmake
git diff --check
```

For build-system/profile changes, configure a canonical Desktop build and inspect the resulting cache/options, then build, run CTest where enabled, install, and verify `version.txt`. Cross-platform release workflow changes should preserve the normal PSP/PS2/Vita/Desktop CI matrix and should be exercised through CI before a real tag is created.
