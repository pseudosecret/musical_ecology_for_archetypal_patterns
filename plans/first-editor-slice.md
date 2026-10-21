# First usable notation editor

## Goal

Build a keyboard-first single-staff editor with chord entry, audible playback, and MusicXML save/reopen.

## Constraints

- C++23/Qt 6/QML; C++ owns score, engraving, and playback timing.
- Local edits invalidate only the affected measure/system; visible systems are rendered.
- Playback never uses engraved coordinates and rejects edits while running.
- Python is not needed.

## Affected subsystems

- score model and undoable edit layer
- engraving/layout and viewport
- QML/UI
- playback
- MusicXML persistence
- tests and benchmark

## Design

The score contains fixed-length measures tiled by rest or note events. A note event can have multiple pitches. An edit command changes one measure, records its before/after value for undo, and invalidates that measure's system. A custom Qt Quick item displays C++-rendered system snapshots. A separate overlay follows playback position. A worker schedules FluidSynth events from score timing. MusicXML is an adapter and not the in-memory model.

## Steps

1. Add CMake build, score model, edit commands, and domain tests.
2. Add MusicXML save/reopen with rejection of unsupported content.
3. Add engraving, custom viewport, QML shell, and keyboard commands.
4. Add FluidSynth playback and transport, then integration tests and a long-score benchmark.

## Verification

Build and run CTest. Check keyboard entry, chords, rhythm/rest invariants, undo/redo, file round-trip, and playback manually. Record local edit and cold layout timings on a generated long score.

## Implemented and checked

The score model, MusicXML adapter, Bravura engraving, visible-system viewport,
QML controls, FluidSynth transport, and tests are in place. CTest passes,
including keyboard chord entry through QML and stop/restart through FluidSynth's
file audio driver. On a synthetic 2,000-measure single-staff score, the local
edit benchmark measured about 0.28 microseconds per iteration and cold layout
of one visible system about 1.3 microseconds per iteration on this machine.
Actual speaker output and orchestral-scale performance still need checking in
the user's desktop environment.

## Risks

The first build supports one voice and a narrow MusicXML subset. The user supplied Bravura, which is bundled with its license; the renderer retains a fallback for missing fonts. MIDI file import/export and orchestral-scale engraving are later milestones.
