# Continuous copy/paste across selected staves

## Goal
Select a continuous interval with Shift+Left/Right, toggle staves with Ctrl-click,
and copy/paste with Ctrl+C/V. Explicit staff lists map in score order: 1/3 to
2/4. Paste overwrites exactly the copied duration and extends the score as needed.

## Constraints
C++ owns musical data, editing, rhythm spelling, and layout. QML routes controls.
Selections and playback overlays do not invalidate engraving. Ordinary paste
uses affected measure patches, never whole-document snapshots. No Python or
new dependencies. Paste and editing remain unavailable during playback.

## Affected subsystems
Score/rhythm/undo, editor selection, Qt clipboard adapter, viewport overlays,
incremental layout, MusicXML partial tuplets, tests, and benchmarks.

## Proposed design
Use exact-tick ranges with explicit staff IDs and an independent fragment of
ordered staff rows. Copy includes notes/rests/chords/ties/tuplets, retaining the
destination's meter, clefs, tempo, and instruments. Bounded versioned private
clipboard data is decoded before domain validation. Paste prepares every changed
measure before committing; clipping and barline splits share deterministic rhythm
spelling. Partial tuplet groups have explicit extents and MusicXML boundaries.
Unsupported exact splits fail atomically. History stores all changed measures and
all appended bars, and exposes the complete affected region to layout.

## Steps
1. Add range/fragment domain contracts, partial tuplets, transactional splicing.
2. Extend local undo and batched incremental layout, including appended bars.
3. Integrate keyboard/pointer selection, clipboard codec, and visible overlays.
4. Verify persistence, domain/UI regressions, playback timing, and performance.

## Verification
Cover individual events, partial/whole measures, 1/3 to 2/4, ten staves for 5.5
measures pasted twelve bars later, ties/clipping/tuplets, destination meter changes,
overlap, append/undo/redo, malformed data, and focus/playback restrictions. Compare
warm local edits against the 30-staff/400-measure fixture; inspect range images.

## Risks / rollback
Selection edges follow existing events. Source/destination staff counts must
match and destinations share one time. Unsupported rhythm splits fail without
rounding or truncation. Other editing commands require collapsing the range.

## Results
Implemented all four steps. `cmake --build build -j 2` succeeded without new
compiler warnings. `ctest --test-dir build --output-on-failure` passed all five
suites: score (24 checks), tuplets (34), UI (10), copy/paste (16), and audio (6).

Verified explicit staff mapping, half-measure endpoints, overlap, held-note
clipping on other staves, tuplets split/tied across barlines, MusicXML round
trips, clipboard validation, new-document clipboard retention, and append/undo/
redo. UI tests exercise real Ctrl-click staff selection, keyboard shortcuts,
Escape, dialog focus, and playback guards. Rendered range and partial-tuplet
artifacts were inspected in `build/ui-checks/` and `build/copy-checks/`.

The dense-passage regression caught and fixed an upstream reflow boundary:
shrinking the first measure of a system can allow it into the preceding system.
Batched layout now matches a fresh full layout after paste, undo, and redo.

On 30 staves / 400 measures, copying ten staves for 5.5 measures took 0.50 ms;
paste plus visible layout and undo measured 5.03 ms at p95 (50 iterations).
Single-note paste plus visible layout and undo measured 1.74 ms at p95 (100
iterations). The existing orchestral edit benchmark was 1.3 ms/iteration versus
the earlier recorded approximately 1.0 ms; all remain below the 50 ms local-edit
target. Selection updates do not invalidate engraving; range geometry is cached
between selection/layout changes and visible-system cache transitions.

Native desktop clipboard transfer between separate processes was not manually
checked; automated tests use Qt's offscreen clipboard. Offline audio tests
passed; live speaker output was not checked. The private payload is bounded to
32 MiB / 250,000 events / 1,024 rows, and paste rejects extensions requiring more
than 500,000 new staff-measures. Unsupported rhythmic splits remain atomic
failures as agreed.
