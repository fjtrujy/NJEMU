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

## PCSX2 A/B regression test (2026-10-26)

PCSX2 2.9.142 on macOS successfully booted a GCC-compiled PS2 NCDZ Release
`GUI=OFF` build from the local `fix/ps2-ncdz-cdda-transitions` branch.
The test used a **private** PCSX2 `-datapath` and copies of existing NCDZ
game media under the ignored `build_issue17_pcsx2_runtime_20261026/`
directory; nothing in `resources/` or the user's normal PCSX2 profile was
modified. Metal Slug 2 and Art of Fighting 3 both reached their title menus.

The test-only `tests/ps2_ncdz_cdda_pcsx2_stress.c` uses the linker option
`-Wl,--wrap=update_screen` to inject calls into the normal emulated-frame
loop. Starting at frame 120, it plays tracks 02-06 every 30 frames, issues
a stop every 150 frames, and intermittently pauses/resumes playback. It
prints each play/stop result and a completion marker at frame 7200.
The harness is **not** part of shipping binaries or regular CTest; use it
only in an isolated PS2 NCDZ `GUI=OFF` debug executable.

To preserve an external `update_screen` call that can be wrapped at link
time, compile `src/ncdz/ncdz.c` as one non-LTO object, replace its object
in the CMake-generated link invocation, add the hook object, and pass
`-Wl,--wrap=update_screen`. With the whole target under LTO, the wrapper
can be eliminated by cross-translation-unit optimization.

The pre-fix MP3, CDDA, and PS2 mixer files were extracted from `origin/master`
at `4f3898cf` and compiled separately for the A/B test. The fixed version
uses the same source tree except for those three updated files and the new
optional audio cancellation hook.

| Game | CDDA implementation | Compiler mode of the three audio files | Result |
| --- | --- | --- | --- |
| Metal Slug 2 | pre-fix (`4f3898cf`) | no LTO | **Hung at frame 360**, 9 logged play/stop events; no progress for over a minute |
| Metal Slug 2 | fixed (`28e0b0bf`) | no LTO | **Completed 7200 frames** with the matching non-LTO audio build; 237 logged play/stop operations |
| Metal Slug 2 | pre-fix (`4f3898cf`) | LTO | Completed 7200 frames: timing-sensitive failure, not unconditional |
| Metal Slug 2 | fixed (`28e0b0bf`) | LTO | Completed 7200 frames; 237 logged play/stop operations and completion marker |
| Art of Fighting 3 | fixed (`28e0b0bf`) | LTO | Completed 7200 frames; 237 logged play/stop operations and completion marker |

All runs used the same real NCDZ game data and MP3 track files. The
non-LTO comparison is particularly useful because the old and patched audio
files were compiled with **matching** compiler flags: the old version stalled
on this run, while the fixed version completed. Compiler scheduling also
affects the result: the LTO pre-fix build completed all 7200 frames in a
separate run, so a single successful baseline run cannot disprove the bug.
In the completed fixed tests, PCSX2 continued reporting active
video output and the CDDA state alternated between `MP3_PLAY` and `MP3_STOP`
as expected. The screenshot and detailed host log for each run are retained
in the ignored `build_issue17_pcsx2_runtime_20261026/` directory:

- `pcsx2-baseline-stress-emulog.txt`: the stalled no-LTO pre-fix run.
- `pcsx2-fixed-nolto-stress-emulog.txt`: the successful matching no-LTO fix.
- `pcsx2-baseline-lto-stress-emulog.txt`: the LTO pre-fix run which completed.
- `pcsx2-fixed-stress-emulog.txt`: the successful fixed LTO run.
- `pcsx2-aof3-fixed-stress-emulog.txt`: the successful fixed LTO second game.

This is an actual PCSX2 runtime reproduction of a **track-transition stall**
in the previous implementation and a successful extended stress test of
the proposed synchronization fix, not merely compilation or mock tests.
The reporter's exact Viewpoint and KOF '98 transitions were not available
locally, and PCSX2 does not emulate the same physical USB I/O latency or
SPU2 timing as a PS2 Fat.

## Optional physical-release verification

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

The PCSX2 A/B result validates the reproduced synchronization stall.
Physical hardware would still be useful to rule out USB-specific stalls and
verify normal CDDA sound, volume, track completion and silence on stop.
PSP and Vita audio-regression checks are also recommended before a release.
