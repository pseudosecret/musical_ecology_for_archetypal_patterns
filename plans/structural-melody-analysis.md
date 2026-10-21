# Structural melody analysis

## Goal
Propose useful cells, coherent gestures and conservative motif candidates in a selected monophonic excerpt, with abstract pattern signatures and explained structural salience.

## Constraints
C++ retains score ownership and validates worker data. Python remains asynchronous. Analysis and review are session-only; edits only mark results stale. Exact repetition, boundary and tonal analysis remain available. No score model, engraving or playback changes.

## Affected subsystems

- Python analysis and versioned bridge
- C++ analysis validation and scratchpad review
- QML findings UI
- Tests, performance fixtures and documentation

## Proposed design
Analysis schema 2 / melody-v2 retains protocol v2. Reuse sustained-note extraction and indexed exact repetition. Add bounded 2–12-note signatures and coherent gesture candidates, grouping related occurrences without promoting abstract similarity alone to motif status. Expose separate evidence and salience with documented components. Focus findings on motif/gesture families, with cells and complete findings available on demand. Review all structural candidates without changing their automatic category.

## Steps
1. Record v1 timing/memory/output baselines and add signatures, gesture detection, conservative classification and salience with positive/negative fixtures.
2. Upgrade the analysis contract, validation, report/export and review APIs.
3. Add focused/category-filtered findings, reason-first cards and review controls.
4. Run focused and broader regressions, inspect UI artifacts, compare stress fixtures and document results.

## Verification
Abstract shape/equality, spelling and octave distinctions, normalized rhythm with rests, ties/clipping; routine-scale false positives; independent repetitions and one-off gestures; motif support; deterministic ranking and evidence/salience independence. Reject malformed signatures/scores/occurrences. Verify selection, review, names, exports and staleness. Benchmark 20,000-event periodic and uniform excerpts against v1, including response bytes and peak RSS.

## Risks / rollback
Heuristic roles need musical counterexamples, and generic shapes can flood discovery. Bound windows, occurrence counts, comparisons and findings; disclose truncation. Keep the worker/C++ contract synchronized. The upgrade does not alter saved MusicXML or canonical musical data. Persistent region objects, manual split/merge and transformation commands remain deferred.

## Progress
- Implemented schema 2 / melody-v2, bounded signatures and gesture families, conservative motif gates, and separate explained salience. Preserved variable-length exact matches, existing boundary/tonal behavior and protocol v2.
- Added Structures / Cells / All findings, numeric details under Inspect measurements, and accept/reject/name for all structural categories. Accepting a gesture preserves its category. Selection, exports and stale-result restrictions remain intact.
- Added positive and negative musical fixtures, 27 malformed-response cases, cross-occurrence signature validation, cell/gesture review checks and UI filtering/naming/navigation checks. Packaged worker analysis also passed.
- Full build succeeded without new compiler warnings. All 10 CTest suites passed through Poetry, including 36 Python tests. Selected snapshot capture averaged 0.013 ms on the 30-staff / 400-measure fixture.
- Rendered and inspected `build/ui-checks/melody-structures.png`. Desktop interaction remains unverified; UI checks used Qt's offscreen software backend.

### Stress measurements

Single-process, unprofiled runs on the same machine; these timings are comparison samples, not guaranteed budgets. Periodic material repeats `[60, 62, 67, rest]` 5,000 times. Uniform material has 20,000 C4 attacks. Varied material has 20,000 pitches `48 + ((i * 17 + i * i // 11) % 36)`. All events last one quarter note.

| Material | v1 seconds | v2 seconds | v1 / v2 peak KiB | v1 / v2 response MB |
| --- | ---: | ---: | ---: | ---: |
| Periodic, 15,000 sustained notes | 2.873 | 2.847 | 154428 / 154632 | 8.93 / 8.91 |
| Uniform, 20,000 notes | 2.628 | 2.283 | 144912 / 144952 | 9.17 / 9.18 |
| Varied, 20,000 notes | 2.883 | 3.635 | 169344 / 175668 | 10.08 / 11.37 |

Profiling the initial varied-material implementation identified repeated descriptor calculations in the short-window scan. Incremental window calculations reduced its time from 5.152 to 3.635 seconds; a 2,000-note comparison confirmed byte-identical structured results before and after optimization. The varied stress case still adds approximately 0.75 seconds, 6 MiB peak memory and 1.3 MB of output for the extra structural proposals. Analysis remains asynchronous and selected-excerpt only; ordinary edits do not rerun it.
