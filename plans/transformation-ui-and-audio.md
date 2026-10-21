# Transformation controls and live audio regression

## Goal

Apply the transformation chosen in the dialog and investigate distorted live
audio when the editor is launched through Poetry.

## Constraints

Keep musical transformations in the Python worker and validation in C++. Keep
audio in the shared C++ transport, independent of Python and UI timing. Do not
change score ownership, worker protocol, or layout invalidation.

## Affected subsystems

QML transformation controls, C++ audio configuration, UI/audio
regression tests, and launch documentation.

## Proposed design

Use the delegate's row index explicitly when changing operation settings; the
ComboBox activation parameter belongs to its options, not the step list. Test
real control input through the actual Python worker. Compare native libraries
and inherited audio configuration across launch methods before changing audio.
Any audio adjustment stays in Playback's asynchronous initialization.

## Steps

1. Reproduce option-index collisions with UI tests and verify changed pitches,
   durations, and operation order through music21.
2. Correct row targeting and preserve control focus and editable parameters.
3. Investigate live PulseAudio buffering, make a bounded configuration correction
   if supported by evidence, and document a reproducible listening check.
4. Rebuild and run focused UI, audio, Python, and bridge regression tests.

## Verification

Exercise all four transformations from the UI, including row reordering and
editing a later step. Check for QML warnings. Run waveform checks for pitch,
clipping, sustained notes, volume, and sample-clock independence. Compare direct
and Poetry launches. Native WSLg listening requires access to the user's sink;
offline WAV tests alone cannot establish that speaker distortion is resolved.

## Risks / rollback

More audio buffering increases audition latency. Keep explicit buffer overrides.
The sandbox currently cannot connect to the WSLg PulseAudio socket. Audio findings
must distinguish verified configuration and synthesis from unverified output.

## Progress

Confirmed ComboBox's injected `index` shadows the transformation row index.
Direct and Poetry launches resolve identical native libraries and inherit the
same audio environment. The user reports distortion before any transformation.

Corrected the handler to name the option parameter and use the delegate's row
index. Regression tests reproduced the original identity results and out-of-range
warnings, then passed for all four operations and second-row editing/reordering
through real music21. Typed transposition values commit when Submit is clicked.

The user confirmed that `MUSIC_AUDIO_BUFFER=4096` produces clean live audio.
WSLg now defaults to 4,096 frames (~85 ms requested latency); native PulseAudio
retains 2,048, other drivers retain 512, and valid explicit overrides win.

Built all targets without compiler warnings. `poetry run ctest --test-dir build
--output-on-failure` passed all seven suites, including waveform, UI, real-worker,
and Python integration checks, with no skipped cases. An external initialization
probe inspected the actual FluidSynth settings in the rebuilt executable: direct
and Poetry WSLg launches both select 4,096 frames at 48 kHz, native PulseAudio
selects 2,048, file output selects 512, and valid/invalid overrides behave as
expected. The probe redirected the driver to a file after inspecting settings;
it did not establish live speaker quality. Live listening evidence is the user's
successful 4,096-frame check. Wayland activation and FluidSynth thread-priority
warnings are separate platform limitations and remain unchanged.
