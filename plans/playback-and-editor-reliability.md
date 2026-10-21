# Playback and editor reliability

## Goal

Investigate distorted playback and previews, and correct transport, clef-change
removal, selection color, editing after dialogs, and instrument assignment.

## Constraints

C++ owns musical edits and sound scheduling. QML owns controls and focus.
Audio timing stays independent from engraving and UI timers. Selection must
reuse visible engraving snapshots without whole-score work. Changes are undoable.

## Affected subsystems

FluidSynth/audio output, editor commands, score clef timeline, viewport selection,
QML dialogs/instrument controls, regression tests, and README.

## Design and steps

1. Capture and measure current score/preview PCM independently of the WSLg sink;
   inspect driver settings and sample-clock scheduling to isolate distortion.
2. Correct the shared audio boundary and add actual waveform/timing checks.
3. Make Space toggle playback from the selected measure; preserve Delete for rests.
4. Add undoable removal of explicit clef changes, retaining the initial clef.
5. Restore score focus after popup focus restoration and verify tempo/edit workflow.
6. Color selected glyphs orange using a small overlay and cached visible snapshots.
7. Verify and correct instrument assignment through the actual QML controls.

## Verification

Build and run model, waveform, and UI regression tests; measure pitch, clipping,
duration, previews, stop/restart, and instrument changes. Run the existing large
score fixture and local-edit/layout benchmarks. Verify native output if the
environment permits; distinguish PCM correctness from unverified speaker output.

## Risks / rollback

WSLg socket access may be unavailable to the sandbox. Do not claim audible
playback is fixed from file-size tests or offline rendering alone. Audio changes
stay within Playback; overlays do not invalidate static notation textures.

## Progress

The baseline score and preview WAVs both measured 48 kHz, an A4 fundamental at
440 Hz, zero clipped samples, and low synthesis CPU use. This narrows the audio
fault to the live driver/server path; it does not prove WSLg speaker output is
correct. The sandbox cannot connect to its PulseAudio socket. Live listening
will be checked by the user after this pass.

PulseAudio now requests 2,048 frames (~43 ms) instead of 512 (~11 ms), retaining
adaptive latency. A shared volume control starts at 50%. Preview replacement
releases old notes; explicit Stop still silences them. Stop resets only used
score channels, and the start timestamp is chosen after schedule preparation.

Space toggles transport from the selected measure. Clef changes have a guarded,
undoable removal command. Dialogs take keyboard focus, block score shortcuts,
and restore score focus after closing. Entry duration is bounded by contiguous
rest space, preserving the following note. Instrument selections apply
immediately. Orange selection is a cached, small glyph overlay; selecting or
scrolling does not rebuild static notation.

Model, waveform, and UI regressions cover these changes. The viewport is rendered
through QQuickRenderControl under Qt's software backend for an actual image
check, including changing and clearing selection. Reference audio and a selection
image are written to build/audio-checks and build/ui-checks.

Final build succeeded without new compiler warnings. CTest passed all three
suites (37 QtTest cases including setup/cleanup; zero failures or skips). The
QML warning check is clean. Local-edit and layout benchmarks and the existing
30-staff/400-measure orchestral fixture passed. Native WSLg listening remains
unverified; the user will test the rebuilt app and audio from another WSL app.
