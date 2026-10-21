# Tuplets for notation entry

## Goal
Split a selected event with 3/5/7, retaining its total span and its first pitch
or chord, with rests in the other slots. Support mixed written durations,
engraving, exact playback, undo/redo, and MusicXML save/reopen.

## Constraints
C++ owns score semantics, commands, timing, and engraving. QML routes input and
shows controls. Local edits retain local history and incremental layout.
Editing remains unavailable during playback. No Python or new dependencies.

## Affected subsystems
Score/edit history, editor/QML, engraving/render snapshots, MusicXML, playback
timing, domain/UI tests, and the orchestral benchmark.

## Proposed design
Use 6,720 ticks per quarter and explicit written durations for tuplet members.
Measure-owned groups carry stable IDs, start offsets, ratios, and reference note
values. Undotted spans use 3:2, 5:4, 7:4; dotted spans use ordinary division for
3, then 5:3 and 7:6. First-slot identity and incoming ties survive creation;
outgoing ties interrupted by the inserted rests are removed. Entry fills group
rests without carrying scaled durations into ordinary music. Duration edits
cannot cross group boundaries. Existing overwrite/conservative behavior applies
inside groups. Meter changes preserve whole groups or fail before mutation.
Snapshots derive beams, numbers, and brackets from semantics. Playback remains
independent. MusicXML stores both time modification and start/stop notation.

## Steps
1. Migrate internal duration constants and implement score/group invariants.
2. Integrate local commands, entry, duration edits, and meter protection.
3. Implement MusicXML round trips and tuplet engraving.
4. Add domain, playback, UI, persistence, and performance regressions.
5. Build, run all suites, inspect visual artifacts, and record results below.

## Verification
Test notes/chords/rests and dotted spans for all three counts, mixed rhythms,
boundaries, ties, undo, legacy XML divisions, malformed imports, and exact
playback endpoints. Inspect all-rest/mixed/beamed groups and focus behavior.
Benchmark warm local edits and cold layout on 30 staves / 400 measures.

## Risks / rollback
Only single-measure, non-nested tuplets and written values through 32nd notes
are supported. Unsupported edits fail atomically. MusicXML divisions preserve
old files independently of the internal tick grid. Keep timing literals out of
UI logic. Future arbitrary or nested tuplets must revisit the timing grid.

## Results
Implemented all five steps. `cmake --build build -j 2` succeeded without new
compiler warnings. `ctest --test-dir build --output-on-failure` passed all four
suites: score (24 checks), tuplets (34), UI (8), and offline audio (6).
Rendered artifacts in `build/tuplet-checks/` cover all-rest, beamed, and mixed
3/5/7 groups; representative images were visually inspected. UI tests cover
numeric shortcuts, chord retention, dialog focus, and playback edit rejection.

On the 30-staff / 400-measure fixture, cold layout was 14.73 ms and the p95 for
tuplet creation, pitch editing, local layout, and undo was 1.37 ms (100 edits).
The existing orchestral edit benchmark remained 1.0 ms/iteration, matching its
baseline; cold layout remained within the previously observed 11.5–17.1 ms
range. Local commands use measure patches and do not copy the whole score.

Live speaker output was not checked: the environment cannot connect to
PulseAudio. Offline sound-rendering tests passed. Scope limits above remain.
