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

## Normal-frame queue submission (follow-up)

The PS2 backend now submits gsKit's GIF queues through the PS2-local
`ps2_execute_gs_queue()` rather than calling `gsKit_queue_exec()` during
emulation. It preserves gsKit's one-shot/persistent queue bookkeeping and
appends the same GS FINISH packet. Before each new DMA transfer it sleeps on
the prior queue's GS FINISH semaphore, which also proves that the previous
GIF chain has completed. Even on the initial frame, a second nonempty queue
must wait for the first queue's completion. Persistent queue memory is not
reused before FINISH.

Framebuffer FRAME/SCISSOR updates are enqueued into the next GIF batch
instead of being submitted immediately by `gsKit_setactive()`, whose DMA
wait spins. UI font-ring and scratch-buffer flushes likewise submit the GIF
queue and sleep until FINISH before reusing CPU-backed upload data. VBlank
continues to use its independent interrupt/semaphore.

The linked MVS ELF still contains gsKit's blocking queue executor, but an
objdump caller audit found references only from `gsKit_init_screen()`:
initialization/output-mode reinitialization, not normal frame rendering.
No direct `gsKit_queue_exec()`, `gsKit_setactive()`, or
`dmaKit_wait_fast()` calls remain in the PS2 video/UI frame paths.

PCSX2 2.9.93 follow-up on `ps2_vsync_improvements`: MVS Puzzle Bobble 2
and NCDZ Metal Slug 2 ran with VSync and sound enabled for approximately
84 and 86 seconds respectively, at about 59.94 FPS on the overlay, with
no observed graphics stall or application errors. Both use the new
interrupt-driven queue path. These are emulator observations, not physical
EE thread-scheduling measurements.
After correcting the two-nonempty-queues first-frame ordering, the final MVS
build completed an additional 50-second VSync-on PCSX2 run. The GUI-enabled
MVS build also displayed its text/font introduction screen correctly.

## Remaining work and real-hardware checks

**Startup, output-mode reinitialization, and screen readback still use
gsKit/PS2SDK code which can busy-wait.** Only the normal frame and UI
submission paths have been migrated. The internal blocking helpers remain
linked for gsKit initialization and may be used by infrequent operations.
The GPU's FINISH interrupt is currently also the DMA-completion fence;
verify this ordering and any throughput/audio tradeoffs on real hardware.

On a real PS2, verify audio continuity under frame pressure, repeated VSync with the FPS overlay, graphical integrity during mode switches (240p, 480i, 480p), menu texture updates and screen capture. PCSX2 runs exercised boot-time mode selection, not live switching between modes. Do not regard emulator FPS as a real-hardware performance measurement.
