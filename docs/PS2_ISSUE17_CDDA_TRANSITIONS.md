# PS2 NCDZ issue #17: CDDA track-transition synchronization

Tracker: https://github.com/fjtrujy/NJEMU/issues/17

## Scope and evidence

The reporter observed hangs in PS2 Fat NCDZ v3.0.4 during Viewpoint deaths
and KOF '98 team/continue transitions. Disabling CD Audio prevented the
reported hangs. Constant-bitrate MP3 files made them less frequent than
variable-bitrate files but did not eliminate them. This implicates the CDDA
path; it does **not** isolate USB latency as the root cause.

Source inspection found several independently reachable indefinite waits:

- `mp3_stop()` waits for `mp3_running` to clear. On PS2, the decoder may
  be blocked in `ps2_outputPannedBlocking()` waiting for ring-buffer space.
  A track transition previously did not wake this semaphore waiter.
- Save-state seeking can put the decoder into `MP3_SLEEP` from inside
  `MP3Update()`, leaving `mp3_running` true. A later stop previously
  waited without waking the sleeping decoder.
- During the global emulator sleep path, the decoder previously waited for
  `Sleep` to clear even after playback was stopped.
- CDDA commands with the synchronous acknowledgement flag previously waited
  without a timeout. Missing or failed MP3 opens could leave the
  acknowledgement unset.
- Failed MP3 reads were converted directly to an unsigned byte count before
  being passed to libmad; zero-progress reads could also loop.

These are concrete source-level failure modes. No physical PS2 blocked-thread
trace has yet matched one of them to the reporter's specific game transition.

## Implementation

- `audio_driver_t::cancelPannedOutput` is an optional hook called before
  the common decoder waits for a stop to finish. On PS2, it invalidates the
  current ring-write generation, drops queued CDDA, and signals the producer
  semaphore. The producer checks the generation around its wait and drops
  an invalidated buffer. The common decoder clears the ring a second time
  after the worker has quiesced so a late block cannot leak into a new track.
  The existing zero-volume pause path invalidates pending writes as well.
  PSP, Vita, and Desktop keep their existing output implementations.
- `mp3_stop()` wakes a sleeping decoder and resumes a paused decoder before
  waiting for it to exit. A stopped decoder can now also leave the global
  menu-sleep loop rather than waiting for menu exit.
- The MP3 filename is replaced only after the preceding decoder has been
  stopped. Read failures and zero-progress reads terminate the current
  decode rather than propagating invalid lengths.
- Failed CDDA opens acknowledge the command as handled. Synchronous CDDA
  acknowledgement waits have a 5-second upper bound, avoiding an unbounded
  main-emulation-thread wait if startup never signals.

The bounded acknowledgement is a guard, not a substitute for fixing storage
errors. A truly blocked synchronous filesystem read can still delay decoder
shutdown because the current POSIX/fileXio API does not offer a safe generic
way to cancel an in-flight read. Do not detach a decoder that still owns an
open descriptor, output buffer, or shared ring.

## Automated validation

- `tests/ps2_cdda_ring_tests.c` compiles the actual PS2 audio driver against
  bounded host mocks and verifies normal backpressure, stop with a full ring,
  stale-sample rejection, pause with a full ring, and a fresh track after
  cancellation.
- `tests/mp3_stop_tests.c` tests the actual MP3 lifecycle state machine with
  mock thread/audio drivers: blocked output, sleeping seek, paused worker,
  menu sleep, failed read, and zero-progress read.
- Both are part of Desktop NCDZ CTest; they do not require a ROM or alter
  `resources/`.

## Physical validation still required

Use a GCC-based PS2 NCDZ build, not experimental LLVM, and compare against the
same v3.0.4 baseline. With CD Audio enabled, repeatedly perform:

1. Viewpoint life loss, game-over countdown and Continue.
2. KOF '98 character/team loss, stage transition and Continue.
3. MP3 VBR versus 192 kbps CBR for identical source tracks, keeping video
   settings and storage fixed.
4. Rapid game exits, MP3 pause/resume and save-state seek after a track change.

Record at least the current track, decoder state, `mp3_running`, ring
occupancy, cancellation generation, time spent waiting for ring space,
filesystem read latency/errors, and the last successful CDDA acknowledgement.
Keep counters aggregate and logging opt-in to avoid creating USB latency.
Compare USB and internal HDD only after the base fault is reproducible.

If using PS2Link, do **not** reset the IOP: it disconnects the resident server.
Keep any PS2Link-specific IOP-reset skip confined to a temporary test build.
The production IOP/driver initialization must remain unchanged.

Also verify normal NCDZ CDDA sound, volume, track completion and silence on
stop, including PSP and Vita. Neither source review nor host tests establish
the final real-hardware outcome.
