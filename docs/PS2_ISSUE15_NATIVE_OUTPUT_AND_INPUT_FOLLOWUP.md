# PS2 CPS1 issue #15: native scanout and between-frame input follow-up

Date: 2026-10-16
Report: https://github.com/fjtrujy/NJEMU/issues/15#issuecomment-6217356022
Fork reference: https://github.com/SumavisionQ5/NJEMU-PS2/tree/6c72b3b73fb1059675a9250207b0348e8444c80e

## Community symptoms

- The stage-one building background in `dino.zip` still shimmers at 240p.
- The old PS2 `Native Pixels (1:1)` display option appears too narrow.
- Quick repeated attacks in `dino.zip` can fail to register.

The reporter previously confirmed that QSound and random gameplay hitches
had improved. Do not treat those as reopened by this change.

## Opt-in native CPS1/CPS2 progressive scanout

The previous implementation always configured a 704x240 NTSC GS framebuffer.
When selecting `Native Pixels`, it copied the 384x224 CPS source 1:1 into a
384x224 rectangle within that much wider framebuffer. The resulting picture
was physically narrow. The other display modes instead stretch the 384 source
pixels over an aspect-correct approximately 657-pixel destination; nearest
sampling keeps the pixels sharp but can produce uneven pixel widths during
scrolling. This is distinct from missing pixels in the sprite renderer.

The PS2 video backend now selects a **384x224, noninterlaced NTSC GS scanout**
for CPS1/CPS2 when both the output mode is **240p** and the display mode is
**Native Pixels**. The 512-wide renderer and the existing 384x224 source crop
are unchanged. This is the native GS scanout geometry also used by the fork,
but it is integrated into NJEMU's current portable renderer and dynamic mode
switching rather than copied from the fork's older renderer.

Selecting the mode in the menu rebuilds the GS framebuffer and UI, with a
rollback to the previous mode if reallocation fails. Other display options
retain the 704x240 geometry, as do 480i/480p and MVS/NCDZ. Mode values in
saved configuration files are unchanged. Per-game display modes load after
the initial framebuffer is created, so the PS2 GUI reconciles the scanout
width at game-config load as well, avoiding a mismatch after relaunch or when
switching games. The NO_GUI build performs the same video-mode reconciliation
without reinitializing a menu. Since 384-wide GS output changes the
horizontal magnification, the PS2 pixel-aspect telemetry is updated for that
geometry as well. The actual CRT image, overscan and output sync must still be
checked on hardware. This is a comparison path, not a claim that all 240p
shimmer is eliminated in the other presentation modes.

## Between-frame pad edge capture

NJEMU already reads CPS1 controls once per emulated frame, before CPU work,
and bridges one invalid PADMAN sample. That cannot preserve a complete
press-and-release event between game-frame polls, especially after a slow
emulated frame. The reporter's fork uses a dedicated EE pad sampler.

The PS2 input backend now samples opened, established controllers every
8 ms from a normal EE thread. It records gameplay-button down-edges per
controller and merges each pending press into the next game-frame input
sample. A pending edge is consumed once. Invalid packets are ignored, a
one-sample cache replay does not invent a fresh press, and a disconnect
clears the pending state. Start/Select are excluded to avoid synthesizing a
menu shortcut from two unrelated presses. High-frequency capture is disabled
and the pending masks are cleared while the in-game menu is open; ordinary
menu polling continues unchanged. Capture is also off in the ROM browser and
during game loading, and is armed after the first emulated frame to prevent
replaying a previous browser selection as a gameplay attack. No IOP module,
input mapping, emulated CPS1 port timing, or other platform's input driver
was modified.

There are important limits: PADMAN itself may not observe a very short tap;
the game may ignore a punch during attack/recovery animations; and an extra
EE thread and periodic PADMAN reads can affect timing on real hardware.
Therefore the new sampler is not yet proof that every missed attack is fixed.

## Verification and hardware checklist

- Local host validation: `ctest --test-dir build_issue15_tests
  --output-on-failure -j4` passed 43/43 tests. The focused issue #15 test
  passed again after the final startup-lifecycle regression case was added.
- PS2 CPS1 and CPS2 GUI ELFs link successfully with the updated input and
  video code. PS2 MVS/NCDZ GUI ELFs linked during this investigation and
  their updated input/frame-loop objects compile cleanly. CPS1 NO_GUI
  input, configuration, and frame-loop objects compile cleanly.
- `ps2_issue15_tests` covers native-mode selection, transient invalid
  packets, disconnection, rapid press/release accumulation, cache replay,
  per-controller isolation, and menu-chord exclusion.
- Compare `Original Aspect` vs `Native Pixels` in `dino.zip` stage one on
  PCSX2 and **real PS2 + 240p CRT**. Inspect both building scroll and sprite
  positioning, as well as display width/overscan and GUI transitions.
- Log PADMAN samples and frame boundaries only if repeated punches still
  fail. Check both the delivery of button edges to CPS1 and the game input
  lockout states before changing emulation timing or IOP code.

This follow-up supersedes the native-width behavior documented in
`docs/PS2_ISSUE15_FORK_FOLLOWUP.md` (2026-10-12) without rewriting that
historical investigation. No `resources/` files are part of this work.

## Real PS2Link hardware validation (2026-10-17)

The fix was exercised on a physical PS2 running PS2Link. The CPS1 ELF was
built from `74338782` with the standalone embedded-driver configuration
(`PS2_EXTERNAL_IRX_IMAGE=OFF`, `PS2_FAST_CACHE=OFF`, `PS2_VIDEO_MODE=240p`).
The console accepted `ps2client -h <PS2_IP> reset` followed by
`ps2client -h <PS2_IP> execee host:CPS1.ELF` from the CPS1 build directory.
The GUI build initialized the PS2 platform, pad driver, video backend and
CLUT without an immediate error.

For automated gameplay startup, a separate NO_GUI build used
`game_name.ini` containing `dino`, a per-game `DisplayMode = 4` (Native
Pixels), and global `VideoOutputMode = 0` (240p). The console recognized
`ROM set "dino"`, loaded the program/graphics/QSound assets, and reported
`Done.` Audio initialization also reported success. A temporary diagnostic
build logged the following from the physical console:

```text
[issue15-hw] initial GS scanout 704x240 interlace=0 output=0 display=0
[issue15-hw] reconfigured GS scanout 384x224 interlace=0 output=0 display=4
PS2 Audio: Audio initialized successfully
[issue15-hw] input edge capture armed, sampler=1 thread=32
```

These observations confirm that loading the per-game settings actually
reconfigures the GS to 384x224 noninterlaced on hardware, and that the
gameplay frame loop enables a running EE input sampler. The last message
is emitted when the first emulated gameplay frame arms edge capture; it
does not establish that every short physical button press is delivered.

The PS2Link connection requires a *temporary, uncommitted* change to skip
`SifIopReset()` in `src/ps2/ps2_platform.c`; resetting the IOP would
disconnect the resident PS2Link host. Earlier PS2Link stashes contain
this workaround alongside unrelated profiling changes and should not be
applied wholesale. The reset and hardware-only trace changes were removed
from the source after validation. The captured console logs and test ELFs
remain in ignored `build_issue15_ps2link_cps1_240p/` and
`build_issue15_hw_nogui_cps1_240p/` directories; no ROMs, configurations,
or other files under `resources/` were changed.

**Still outstanding:** direct visual comparison of the stage-one building
scroll, overscan, and CRT image quality in Native Pixels versus Original
Aspect; manual rapid-press testing with a physical controller; and a check
that changing modes through the GUI remains stable on the target display.
PS2Link console output cannot by itself establish these visual or human
input outcomes.

## Intermittent in-menu Native Pixels switch and DMA teardown fence (2026-10-17)

A real PS2 GUI test exposed an intermittent failure when advancing the 240p
display-mode selector from `4:3` through `Fullscreen` to `Native Pixels`.
`Fullscreen` stays in the 704x240 GS framebuffer, but `Native Pixels` requests
384x224 and rebuilds the GS framebuffer, game VRAM allocations and UI.
PS2Link did not deliver a usable EE exception register dump for the reported
failure. A subsequent diagnostic build with console logging completed the
same manual transition, so the precise original fault is not proven.

The PS2 UI previously called `ui_exit()` before the video backend had
explicitly drained its outstanding command queue and GIF DMA. The GS FINISH
event does not alone guarantee that the GIF DMA channel has finished reading
CPU-side UI upload buffers. `menu_release_ps2_ui_before_mode_change()` now
calls the backend's existing `flushAndWait()` before destroying the UI. It
is used for the live video-output switch, the per-game native-scanout sync,
and both rollback paths. The behavior is PS2-only and does not change the
240p native GS geometry, other renderer implementations, or UI allocation
policy.

A temporary CPS1 GUI startup stress harness ran on the physical PS2 via
PS2Link. For 24 iterations it rendered UI text, switched alternately to
384x224 Native Pixels and 704x240 Fullscreen, reinitialized the UI, and
checked the output dimensions. The PS2Link trace contains successful geometry
assertions for iterations 0 through 22 (some console messages were dropped)
and records the final iteration's GS framebuffer being initialized, game
textures restored, and frame presented. No exception was reported. See the
ignored `build_issue15_ps2link_cps1_240p/issue15_dma_stress_hardware.log`.
The stress harness and verbose trace logging were removed from the source
after the test. The focused test is not a substitute for manual validation
through the exact settings-page transition, because console output and the
extra instrumentation can affect timing.

Host verification: `ctest --test-dir build_issue15_tests --output-on-failure
-j4` passed 43/43 tests. A PS2Link debug executable temporarily skips the
IOP reset to preserve the resident server; that workaround must not become
a production change. Existing unrelated HDD staging modifications in
`src/ps2/ps2_platform.c` belong to another task and must remain untouched.
