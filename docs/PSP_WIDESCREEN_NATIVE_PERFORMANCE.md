# PSP widescreen-off performance and memory safeguards

This change follows the already merged PR #14 (merge commit `715dbb2c`).
Its goal is to make the default **native 4:3** PSP rendering path cheaper
without disabling widescreen for MVS, CPS1 or CPS2.

## Runtime behavior

On PSP only:

- `mvs_wide_init`, `cps1_wide_init` and `cps2_wide_init` now reset
  the per-game state **without reading an INI profile or allocating**
  `wide_profile_t` while the mode is off.
- The first OFF-to-ON transition opens and validates the profile, verifies
  the guarded game-program revision, and applies its widescreen patch.
- An ON-to-OFF transition restores the original game program before freeing
  the profile. Enabling again reloads and rechecks it against the restored
  program. There is no persistent profile allocation in native mode.
- Failed loads are reported once and never retried each emulated frame.
  Reloading the game resets the error state and permits another attempt.
- The PSP MVS sprite-batch path samples wide mode once at entry and avoids
  reading the per-column widescreen pan array for native-mode sprites.
  Geometry, clipping and pixel output remain unchanged.
- Desktop and PS Vita keep the original eager profile-check behavior.
  PS2 still compiles the widescreen feature out entirely.

`sizeof(wide_profile_t)` was measured as **33,000 bytes** on the host
(32,768 bytes for the patch-word array), excluding allocator overhead.
This is the maximum-size profile allocation avoided while native mode is
selected on PSP; it is not a measured free-heap improvement on hardware.
No new runtime profile I/O occurs per frame.

## Validation

- Host lifecycle test `tests/psp_wide_lazy_tests.c` is built for each
  Desktop target using PSP-specific compile switches and mocked profile
  services. It verifies native-mode zero loads, OFF/ON/OFF/ON behavior,
  repeated validation after re-enable, failed-load suppression, and reset.
  Assertions remain active in Release, and the PSP-mock target drops the
  directory-wide Desktop GUI definition; all three tests also pass with
  the parent build configured `GUI=ON`.
- Desktop `ctest` before changes to this document: **MVS 53/53,
  CPS1 44/44, CPS2 46/46**, including each lifecycle test.
- Cross-compiled PSP Release GUI-on, cache-off binaries:
  **MVS, CPS1, CPS2 all passed**.
- PS2 MVS Release GUI-on cross-compilation passed; its feature flags
  remain `MVS_WIDESCREEN=0` and `CPS_WIDESCREEN=0`.
- The prior review found matching Windjammers native frame captures and
  PCSX2 no-widescreen timing deltas of ~0.05% MVS, ~0.15% CPS1.
  These are **PR #14 versus master before this PSP-only optimization**, not
  measurements of PSP FPS.
- A fresh PPSSPP 1.20.4 run was attempted, but no benchmark marker
  was captured; another OpenOMF session was active, and the PPSSPP log
  contained repeated `ISO looks bogus` messages. The NJEMU-owned emulator
  process was terminated while leaving the other session untouched.
  Therefore **no credible before/after PSP FPS number is claimed**.
  A future exclusive PPSSPP session and real PSP smoke test should
  check native mode and toggle behavior with a matching ROM/profile.

The release code, resources and user-supplied ROM archives were not altered
by the review harness; the test fixtures/build logs stay in ignored
`build_pr14_review/` directories.
