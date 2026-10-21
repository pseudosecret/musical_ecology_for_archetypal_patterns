# Audible playback clock and responsive previews

## Goal

Remove delayed/stuttering note previews, align the cursor and completion with
audible playback, and prevent stale audio when switching to scratch results.

## Constraints

C++ owns playback; Python and engraving remain independent. All PulseAudio I/O
runs off the UI thread. No score traversal, allocations, or network waits on the
cursor path. Retain the user's clean 4,096-frame WSLg buffer and explicit overrides.

## Affected subsystems

C++ playback/output, build dependencies, audio/transport tests and documentation.

## Proposed design

FluidSynth's PulseAudio driver exposes neither queued latency nor flushing, and
adaptive buffering has no application-level bound. Use the installed libpulse
asynchronous API behind a small Linux output class. Its worker renders 256-frame
blocks directly into PulseAudio buffers, fills an explicitly bounded startup
queue, and publishes the synthesis clock minus cached output latency. Playback's
cursor and completion use that audible clock. Start/stop asynchronously flush
obsolete audio. Other FluidSynth drivers, including WAV tests, remain available.

No musical contract or model changes. The synth and sequencer outlive output;
UI commands remain thread-safe synth/sequencer requests. Layout is untouched.

## Steps

1. Add a bounded PulseAudio output and deterministic simulated-device tests.
2. Use the audible clock for cursor/completion and discard stale transport audio.
3. Measure rapid previews and transformed result playback; run regression suites
   and existing orchestral/local-edit benchmarks.

## Verification

Simulate output latency, queue backpressure, rapid previews, stop/restart, main to
scratch switching, and device failure without a live server. Check output pitch,
clipping, note boundaries and requested buffer bounds. Run existing WAV, model,
UI, real music21, and timing tests. Live WSLg listening remains a user check.

## Risks / rollback

libpulse development headers become a Linux build dependency. Connection setup
waits only on the initialization worker; input/cursor/render operations never
wait for server timing. The mainloop can be woken to stop without waiting for I/O.
Explicit alternative FluidSynth output drivers retain their existing behavior.
Hardware beyond the PulseAudio server may add latency it cannot report.

## Progress

Implemented the Linux PulseOutput worker with a 256-frame preallocated rendering
buffer, explicitly bounded server queue/prebuffer/minimum request, measured
latency, and asynchronous output discard on transport changes. Both cursor and
completion consume its audible clock. Device loss stops transport and disables
playback with an error. The existing file/alternative drivers remain available.

All targets built without compiler warnings. `poetry run ctest --test-dir build
--output-on-failure` passed all eight suites in 43.70 s, with no skipped cases.
The new simulated PulseAudio device exercises queue backpressure, rapid preview
input, an additional 200 ms device delay, output-clock alignment, completion,
stop/restart, main/scratch switching, and device loss using the real synth.

A real music21 transpose +2 followed by retrograde was rendered to
`build/audio-checks/music21-transpose-retrograde.wav`. Tests found the expected
A4/F-sharp4/D4 pitches, no sustained-note dropouts, zero clipped samples, and
peak amplitude 0.1596. This verifies generated notes and PCM separately from
live WSLg output. Existing 30-staff/400-measure and local-edit benchmarks passed;
tuplet edit/layout/undo p95 was 1.65 ms, single-note paste/layout/undo p95 1.40 ms.

The sandbox cannot listen through the WSLg sink. A live check of previews, cursor
alignment, and the user's transformed passage was requested against the rebuilt
executable; its result is pending.

## Live regression follow-up

The user reports lag and distortion in both scores with the simple-API worker.
The installed FluidSynth driver's clean configuration uses the full queue as
its startup prebuffer. The replacement instead starts after only 256 frames
(5.3 ms), then calls synchronous latency retrieval on the audio producer. The
test double returned timing immediately and did not model startup underruns.

Add a delayed-timing/startup-buffer regression before changing production code.
Replace the simple-API connection with PulseAudio's asynchronous stream API on
one dedicated mainloop thread. Fill the startup buffer before playback, retain
the bounded 4,096-frame WSLg target, and use cached timing with automatic updates;
missing timing must never stop the producer. Flush asynchronously while pausing
new writes until the flush acknowledgement. No new dependency, musical contract,
layout work, or QML timing logic is needed. Recheck delayed timing, device loss,
rapid entry, stop/restart, waveform quality, and real WSLg listening when possible.

The delayed-timing test reproduced one underrun with the old simple-API output.
After switching to asynchronous callbacks, it passes with zero underruns while
timing is unavailable for 180 ms, both on startup and after a transport flush.
The full safety prebuffer and automatic/interpolated timing flags are checked.
All eight CTest suites passed in 46.56 s, with no skipped cases; the actual
music21 transformation waveform and orchestral benchmarks continue to pass.
The executable is rebuilt. A new live listening check was requested; its result
remains pending.
