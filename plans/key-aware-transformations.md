# Key-aware pitch transformations

## Goal
Carry explicit major/minor context and key changes from the C++ score into editable scratchpad transformations. Keep signature, scale identity, and future tuning distinct.

## Constraints
C++ owns musical state; Python is an asynchronous, versioned worker. Exact ticks, bounded selected passages, incremental layout, and targeted undo are required. Existing pitches keep their sounding values when a key is edited or music is pasted.

## Affected subsystems
Score model/history, MusicXML, engraving/layout, editor/QML, transformation workspace, Python worker, tests and benchmarks.

## Proposed design
Per-staff key timelines contain optional signatures and optional tonal identity. New scores start in C major; missing imported identity stays unresolved. Minor defaults to a fixed melodic collection, with harmonic and Aeolian variants. Worker v2 carries selection-relative regions and typed transformation settings. Key analysis supplies suggestions requiring confirmation, without changing the source. Scratchpad scores preserve the resulting regions.

## Steps
1. Add key types/timelines, local undo, key-aware entry and MusicXML round trips.
2. Add key signatures, cancellation accidentals, spacing and key controls.
3. Upgrade worker/bridge for tonal operations, key suggestions and scratchpad contexts.
4. Add regression coverage, run the full suite and reference-score performance checks; document usage.

## Verification
Scale-step versus semitone transposition; all minor forms; local inversion; fixed/reversed key regions; exact rhythm scaling; tied notes; invalid/collapsed pitches; missing keys and analysis confirmation; scratchpad and cross-key paste; XML round trips; undo and incremental invalidation. Build with warnings and run existing audio/UI/domain suites and large-score benchmark.

## Risks / rollback
Spelling and accidental state must not change sounding pitches. Validate worker and XML boundaries. Keep chromatic operations usable without tonal identity. Other modes, automatic modulation detection, microtones and tuning are follow-up milestones.

## Progress
- Completed the C++ timeline/history, MusicXML round trips, signature engraving, key-aware entry, and main/scratch key controls.
- Completed worker v2, explicit minor collections, tonal/chromatic operations, fixed/reversed key-region retrograde, exact rhythm/key scaling, and confirmed asynchronous key suggestions.
- Verification: build succeeded; all nine CTest suites passed through Poetry (31.10 seconds), including 18 Python cases, real music21 integration, UI, model, persistence, and existing audio tests.
- Reference fixture: 30 staves / 400 measures; key edit plus visible engraving p95 5.461 ms over 30 edits. Local note edit plus visible engraving averaged 1.8 ms over 20 benchmark iterations; cold layout index measured 87–102 ms.
- Added rendered notation artifact under `build/tonal-checks/`; checked cancellation, all supported clefs, accidental registration, and tied-note behavior. UI regression tests cover selector synchronization and explicit suggestion confirmation.
- Live desktop/speaker output was not manually checked. Other modes, automatic modulation boundaries, microtones, and alternate tuning remain deferred as planned.
