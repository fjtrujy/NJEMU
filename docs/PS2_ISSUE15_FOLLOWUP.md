# PS2 CPS1 issue #15: 2026-10-10 follow-up

Tracker: https://github.com/fjtrujy/NJEMU/issues/15#issuecomment-6098377002

## Reporter-confirmed fixes

- CPS1 QSound in `dino.zip` now works.
- Random gameplay hitches are no longer observed by the reporter.

These are not reopened by this follow-up.

## PS2 240p sharpness

The CPS1 renderer works in its existing 512-wide scratch frame, with an
effective native source crop of **384x224**. It then presents that crop into
the GS framebuffer. The output modes use **704x240** for NTSC progressive,
not an intermediate 480-line render that is downsampled to 240p.

PS2 240p output pixels are not square (`10:22` presentation pixel aspect).
The shared Original Size/Original Aspect rules preserve the intended arcade
aspect ratio, which can create a **fractional horizontal destination width**.
Before this change, `ps2_transferWorkFrame()` selected linear filtering for
that case. This blended adjacent source pixels and made the 240p picture soft.

The PS2 backend now uses nearest filtering in 240p presentation, while
retaining the existing fractional-filtering behavior for 480i/480p. A
portable predicate and host test exercise the selection independently
of gsKit. No CPS1 rendering resolution or GS hardware mode is changed.

**Remaining trade-off:** nearest filtering at fractional scale removes
interpolation blur, but maps some source pixels to uneven destination
widths. It does not guarantee zero pixel crawling while scrolling. A true
1:1 384x224 output inside the 704-pixel PS2 raster would occupy a narrower
physical horizontal area and change the picture aspect ratio. Do not
silently make that compromise without choosing an explicit display mode
and validating it on a CRT.

## Short lost input samples

CPS1 already calls `update_inputport()` once per emulated frame, before CPU
execution and frame presentation. It is **not** conditional on whether the
frame will be displayed.

The PS2 backend could return no buttons for an individual PADMAN read
failure, including when `padRead()` did not provide a new sample, while
a button was still held. Its state is read again at the next emulation frame,
so an isolated empty sample creates a false release.

A PS2-local input sample cache now bridges **one** such missed sample,
including a short analog-mode transition; two consecutive failed reads
invalidate the cache. A disconnected/error controller clears the cache
immediately. The common input driver and other platforms are unchanged.
A dedicated host test covers short gaps, release transitions, disconnect,
and sharp 240p filter selection.

**Limitations:** this cannot recover a physical tap that begins and ends
between emulation-frame samples. Nor can it force `dino.zip` to perform
an attack while its own animation/recovery logic intentionally ignores
another press. If the issue persists, we need a short video with button
press timestamps or a game-state reproducer and a hardware raw PADMAN
sampling trace to distinguish missed physical samples from CPS1 input
latching/gameplay timing. We should not claim that every missed punch is
fixed from source review or emulated tests.

## Validation

- Desktop CPS1: 39/39 CTest checks passed (including `ps2_issue15_tests`).
- PS2 CPS1 (classic), CPS1 GUI preset to 240p, and PS2 MVS built.
- PCSX2 launched the 240p GUI CPS1 build and rendered startup at
  **640x224** as reported by its host display status.
- **Interactive CPS1 game/menu input and actual 240p scrolling were not
  validated:** Chrome repeatedly became the Mac's frontmost application
  during attempts to send keys to PCSX2. A separate PPSSPP testing session
  was left undisturbed.
- Physical PS2 + CRT verification is still necessary for 240p scrolling
  and the repeated attacks in `dino.zip`.

No files under `resources/` were modified or staged. No changes were
pushed or posted to the issue.
