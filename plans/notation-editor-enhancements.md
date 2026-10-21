# Staff management, rhythm, and reliable playback

## Goal

Implement the agreed multi-staff notation pass: overwrite duration edits, ties,
meter and tempo changes, mid-measure clefs, instrument/staff/group management,
audible chord entry, improved beaming/spacing, and synthesis-clock playback.

## Constraints

C++ owns the score, edit commands, timing, and geometry. QML provides dialogs
and controls. Ordinary edits copy only a small measure neighborhood; structural
meter/staff edits may copy the affected document. Render only visible systems.
Playback and audition use immutable schedules and FluidSynth, without UI timers.

## Affected subsystems

Score/undo, engraving/cache/viewport, QML, FluidSynth transport, MusicXML, tests.

## Proposed design

Use a shared bar timeline and independent staff event streams, stable IDs for
parts/staves/events/groups, per-pitch outgoing ties, and per-staff clef timelines.
Local history stores affected measures; structural history stores before/after
documents. Layout caches widths and system breaks; snapshots contain hit regions.
FluidSynth's sample-clock sequencer handles score and preview events on separate
channels. MIDI program assignments remain runtime-independent score properties.

## Steps

1. Score model, local/structural undo, re-barring and musical regression tests.
2. MusicXML round trips and synthesis-clock playback/audition.
3. Multi-staff engraving, beam groups, incremental reflow and viewport overlays.
4. Staff/meter/tempo/instrumentation dialogs and keyboard/pointer integration.
5. Build, regressions, audio rendering, visual inspection, orchestral fixture.

## Verification

Run CTest and benchmarks for 30 staves/400 measures. Check local invalidation,
round trips, ties, octave clefs, channel isolation, entry preview and stop/restart.
Compare offline audio and live WSLg output; report live audio as unverified if
the sandbox cannot connect to PulseAudio. User speaker confirmation is needed
before declaring the reported crackling fixed.

## Risks / rollback

Meter changes deliberately re-bar music and move tempo marks to bar starts;
undo restores the complete prior document. Rendering and audio remain separate
boundaries. Existing prototype MusicXML is retained as a compatibility fixture.
