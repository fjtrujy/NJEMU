# PS2 GPU completion and VBlank synchronization

## Design

The PS2 video backend uses three independent EE interrupt/semaphore pairs:

- **GS FINISH**: `gsKit_add_finish_handler()` signals `finish_sema_id` when the GS finishes its submitted commands. `gsKit_wait_finish()` sleeps using `WaitSema()` if completion is still pending and checks the GS FINISH status again after waking. A stale semaphore notification must not count as completion of a newer batch.
- **Vertical blank**: `gsKit_add_vsync_handler()` signals `vsync_sema_id` on VBlank. `ps2_wait_for_vblank()` discards one stale notification and sleeps using `WaitSema()` for a subsequent VBlank rather than spinning on the GS CSR register.
- **GIF DMA**: `AddDmacHandler(DMA_CHANNEL_GIF)` signals `gif_dma_sema_id` when the GIF channel completes a transfer. Before submitting a new GIF chain, `ps2_wait_gif_dma()` sleeps on that semaphore if the channel's CHCR STR bit remains set. This is a separate condition from GS FINISH.

Presentation waits for GS completion before swapping the display buffer. Screenshot readback and output-mode changes wait for pending GS/GIF work before accessing or resetting the frame buffers. All three handlers are unregistered before their semaphores are deleted and are reinstalled following output-mode reinitialization.

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
the prior queue's GS FINISH semaphore, **and independently waits for GIF DMA
completion**. GS FINISH alone is not a reliable DMA fence, as confirmed by
the GUI-to-game regression below. Even on the initial frame, a second
nonempty queue waits for both conditions. Persistent queue memory is not
reused before FINISH.

Framebuffer FRAME/SCISSOR updates are enqueued into the next GIF batch
instead of being submitted immediately by `gsKit_setactive()`, whose DMA
wait spins. UI font-ring and scratch-buffer flushes likewise submit the GIF
queue and sleep until FINISH before reusing CPU-backed upload data. VBlank
continues to use its independent interrupt/semaphore.

UI synchronization is exposed through the optional, backend-neutral
`video_driver_t::flushAndWait` contract. The PS2 UI adapter uses this
callback when reusing mutable font-upload buffers. The implementation
`ps2_flushAndWait()` remains private to the PS2 video backend; other video
drivers leave the callback unimplemented. This does not change frame
presentation or the `beginFrame`/`endFrame` contract.
Likewise, the PS2 UI adapter obtains its gsKit `GSGLOBAL` through the
optional `video_driver_t::getNativeContext` callback, rather than a
PS2-specific public accessor. Screenshot readback already uses the common
`video_driver_t::readFrame` callback. Both implementations are private to
the PS2 video backend, so `ps2_video.h` is no longer needed.

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

### GUI-to-game regression (2026-10-08)

Startup-only GUI tests missed a regression in the custom GIF submitter:
with GUI enabled, selecting Puzzle Bobble 2 could load ROM data and
initialize sound, but gameplay did not appear. A controlled PCSX2 comparison
using the same isolated ROM and BIOS showed that the original gsKit submitter
booted the game, while the custom submitter did not. Restoring only
`dmaKit_wait_fast()` before each custom submission also restored gameplay.
That wait was **diagnostic only** and is not retained in production.

The replacement uses a GIF DMAC interrupt and `WaitSema()`, rechecking
`R_EE_D2_CHCR` after every wake to guard against stale notifications.
With the replacement, GUI ROM browsing, launch confirmation, BIOS selection,
ROM loading, audio initialization and Puzzle Bobble 2's animated title
sequence all worked in PCSX2. The GS FINISH and VBlank waits remain separate.

## Remaining work and real-hardware checks

**Startup, output-mode reinitialization, and screen readback still use
gsKit/PS2SDK code which can busy-wait.** Only the normal frame and UI
submission paths have been migrated. The internal blocking helpers remain
linked for gsKit initialization and may be used by infrequent operations.
The GIF DMAC completion interrupt has not yet been validated on a real PS2.
Verify its handler lifecycle across resolution changes and any throughput or
audio-scheduling tradeoffs on physical hardware.

On a real PS2, verify audio continuity under frame pressure, repeated VSync with the FPS overlay, graphical integrity during mode switches (240p, 480i, 480p), menu texture updates and screen capture. PCSX2 runs exercised boot-time mode selection, not live switching between modes. Do not regard emulator FPS as a real-hardware performance measurement.
