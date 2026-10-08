# PS2 GPU completion and VBlank synchronization

## Design

The PS2 video backend uses two independent EE interrupt/semaphore pairs:

- **GS FINISH**: `gsKit_add_finish_handler()` signals `finish_sema_id` when the GS finishes its submitted commands. `gsKit_wait_finish()` sleeps using `WaitSema()` if completion is still pending and checks the GS FINISH status again after waking. A stale semaphore notification must not count as completion of a newer batch.
- **Vertical blank**: `gsKit_add_vsync_handler()` signals `vsync_sema_id` on VBlank. `ps2_wait_for_vblank()` discards one stale notification and sleeps using `WaitSema()` for a subsequent VBlank rather than spinning on the GS CSR register.

Presentation waits for GS completion before swapping the display buffer. Screenshot readback and output-mode changes wait for pending GS work before accessing or resetting the frame buffers. Both handlers are unregistered before their semaphores are deleted and are reinstalled following output-mode reinitialization.

## PCSX2 validation (2026-10-08)

Tested with PCSX2 v2.9.93 on an Apple M1 Max, using an isolated PS2-toolchain build of the patch against `master` at `43f36b0d`. All of the following ran with the PS2 backend and audio enabled:

| Target | Test | Result |
| --- | --- | --- |
| MVS | Puzzle Bobble 2, VSync ON, 480i | ~150 s, >8,400 VBlank wait entries, no observed freeze; 59.94 FPS overlay |
| NCDZ | Metal Slug 2, VSync ON, 480i | ~89 s, >5,100 VBlank wait entries, no observed freeze; 59.94 FPS overlay and MP3 mixer initialized |
| MVS | Puzzle Bobble 2, VSync ON, 240p and 480p | Both physical output modes selected successfully; video and VBlank wait counters advanced |
| MVS and NCDZ | GUI ON/OFF | All four configurations compiled using the PS2 toolchain |

Temporary VBlank diagnostic logging was kept outside the source patch, then removed; clean, uninstrumented MVS and NCDZ binaries were rebuilt and launched successfully in PCSX2.

## Remaining work and real-hardware checks

**This does not yet guarantee zero busy-waiting in the PS2 graphics stack.** The linked gsKit implementation still polls inside `gsKit_queue_exec_real()` via `gsKit_finish()` and `dmaKit_wait_fast()`. Additional calls to `dmaKit_wait_fast()` exist in `src/ps2/ps2_ui_draw.c`. Eliminating all busy-waits requires a separate review of gsKit GIF DMA submission and completion, including buffer-lifetime guarantees; it cannot be achieved solely by changing VBlank waits.

On a real PS2, verify audio continuity under frame pressure, repeated VSync with the FPS overlay, graphical integrity during mode switches (240p, 480i, 480p), menu texture updates and screen capture. PCSX2 runs exercised boot-time mode selection, not live switching between modes. Do not regard emulator FPS as a real-hardware performance measurement.
