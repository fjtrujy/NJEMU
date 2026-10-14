# PS2 Issue #15: fork input hardening and native-pixel comparison

Date: 2026-10-12
Fork reference: https://github.com/SumavisionQ5/NJEMU-PS2/tree/cps2_improvements
Relevant fork change: https://github.com/SumavisionQ5/NJEMU-PS2/commit/5cf7066a06161f4dd9edb8a1414c31331a1abe5d

## Goals

Review two differences identified in the reporter's PS2 fork without
cherry-picking its legacy input/video implementations:

1. Reject invalid PADMAN snapshots instead of generating a false
   all-buttons-pressed sample.
2. Offer a native-pixel presentation comparison while preserving the
   existing 704x240 NTSC output timing, pixel-aspect-correct modes, and
   saved DisplayMode values 0..3.

No IOP timing, PADMAN module, scheduler, normal VSync logic, or CPS1
gameplay behavior was changed.

## PADMAN validation

The fork rejects PADMAN replies whose button word is zero (the active-low
equivalent of all buttons pressed) or whose mode byte is zero, and reuses
the preceding state. Current NJEMU retains its per-controller,
**one-missed-sample maximum** recovery, which is safer than retaining a
global last state indefinitely.

A new PS2-local `ps2_pad_packet_valid()` helper checks packet length,
mode, and active-low button word. It is used by the normal read path;
rejected packets never enter the input decoder or fresh-sample cache. Following one
cached replay, a second invalid read cannot keep a held button stuck.
A hard disconnect still invalidates the cache immediately.

PS2 already preserves an established logical controller during the
transient `PAD_STATE_EXECCMD` state. It intentionally does not
unconditionally poll every early discovery state like the old fork:
a controller that has never become ready must not displace another pad.
The configuration/mode handshake remains conservative.

Unit tests cover valid digital/analog mode bytes, incomplete packets,
mode zero, all-zero button data, one-sample recovery, repeated failures,
and release after recovery.

### PCSX2 CPS1 runtime smoke

Originally tested with PCSX2 2.9.93 and a temporary profiled CPS1 build, `dino.zip`,
NO_GUI, 240p, and `DisplayMode=4` in an **ignored, separate** runtime
directory. The game rendered successfully with the narrower centered
native-pixel presentation and reached a 60 FPS screen. Diagnostic windows
after initialization showed `invalid=0`, `read_fail=0`, `cached=0`
and `fresh=600` each.

Synthetic keyboard-to-virtual-pad tests in that run:

| Input | Attempted | Raw Square presses | Delivered |
| --- | ---: | ---: | ---: |
| 35 ms down / 65 ms up | 20 | 19 | 19 |
| 12 ms down / 32 ms up | 30 | 25 | 25 |

These are not statistically paired A/B results. They show that the
invalid-packet filter did not produce additional raw-to-delivered
losses in that historical test; they do **not** prove a new rapid-tap fix.
Short transitions remain susceptible to PCSX2/PADMAN ~60 Hz sampling.

Local ignored evidence:
`build_issue15_pcsx2_native_test/pcsx2_complete.log` and
`build_issue15_pcsx2_native_test/pcsx2_native_runtime.png`.

## Native Pixels (1:1) display mode

The fork uses a 384x224 GS framebuffer for CPS1/CPS2. That is a
different physical GS raster configuration, potentially with CRT-specific
timing and pixel aspect effects. It should **not** be copied wholesale
without physical hardware verification.

Instead, NJEMU now exposes PS2-only `Display Mode > Native Pixels (1:1)`
as **new stored option value 4**. The preexisting options retain their
numeric values and behavior: Original Size, Original Aspect, 4:3,
Fullscreen. All existing user configurations default to Original Aspect
(value 1). PSP/Desktop menus and config maxima remain unchanged.

On PS2 240p with CPS1 landscape content:

| Choice | Physical GS raster | Game presentation rectangle | Image size |
| --- | --- | --- | --- |
| Original Size / Original Aspect | 704x240 | (23,8)..(680,232) | 657x224 (aspect-correct for CRT) |
| Native Pixels | 704x240 | (160,8)..(544,232) | 384x224 (1:1 source pixels) |

The new mode centers the native game image without the fractional
horizontal stretch. It retains nearest-neighbor texture filtering on
240p and preserves existing output resolution, safe viewport, aspect
modes, menu dimensions, GS VRAM layout, and VSync implementation.

**Tradeoff:** The source-pixel 384x224 image is *not* display-aspect-
correct on a PS2 CRT with non-square output pixels. It appears narrower
than Original Aspect. This is intentional for an opt-in sharpness
comparison and is **not** the same as switching the physical GS timing
to 384x224. Rotated tall images that cannot fit 1:1 scale uniformly
down to the viewport instead of clipping or distorting.

Translations include Native Pixels for all five languages. Appending
text ID 429 changes the V2 text-pack schema hash, and new releases must
bundle newly generated `lang/*.lng` files; old packs intentionally fail
schema validation, falling back to the packaged language where supported.
Source ID values 0..428 remain stable.

## Verification

- Desktop CPS1: 39/39 CTest tests passed, including geometry,
  PADMAN packet validation and translation-pack tests.
- PS2: CPS1 normal build passed, as did CPS2, MVS and NCDZ.
- Desktop CPS2: clean build passed.
- Legacy cached Desktop/PSP directories could not be reused because
  stale options/toolchain paths cause configuration errors unrelated to
  these changes; a fresh Desktop CPS2 configuration was used.
- Physical PS2/CRT: not available, so genuine screen sharpness and
  hardware controller robustness remain unverified.

Future work: reproduce `mode==0` and `btns==0` on real DualShock
hardware, validate legacy PADMAN variants, and compare the two display
choices on the same CRT. Prior experiments with high-rate IOP/EE polling
and button replay were deliberately removed from active sources; the
historical Git commits preserve that research if it is needed again.

Never modify, stage, or commit files under `resources/`.
