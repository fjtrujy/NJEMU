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
