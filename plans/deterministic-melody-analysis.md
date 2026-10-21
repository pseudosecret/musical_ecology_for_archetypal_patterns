# Deterministic melody analysis

## Goal
Select a monophonic passage, press T, and Analyze selection to open an unchanged scratchpad copy with a comprehensive, copyable report and inspectable musical findings. Expose features, candidate regions, motifs, evidence and relationships for later transformations.

## Constraints
C++ owns the musical document and validated analysis state. Python runs asynchronously on bounded selected snapshots. Exact timing and complete meter/key context survive copying. No musical changes, sliders, analysis persistence, or whole-score analysis on ordinary edits.

## Affected subsystems
Application/scratchpad state, selected-score copying, Python bridge, QML UI, tests/benchmarks, build packaging and documentation.

## Proposed design
An analysis snapshot carries selected notation plus measure/context maps and source references. Worker v2 gains an analyze_melody command and versioned deterministic analysis module. The application validates feature/region/reference data before creating a scratchpad tab. Report prose derives from measurements. Review decisions remain separate from automatic evidence. Document revisions mark results stale in constant time; no automatic recomputation. Existing selection overlays navigate findings.

## Steps
1. Implement deterministic features, tonal candidates (major/minor/modes/pentatonics), boundary evidence and indexed repetition/variant detection with musical fixtures.
2. Add snapshot/copy/result validation and async workspace integration with recovery and revision checks.
3. Add Analyze selection, report/findings, copy/review/navigation UI and integration tests.
4. Benchmark long/repetitive passages, run regression suites, and document usage/limits.

## Verification
Known motifs/transpositions/variants; ties, rests, tuplets, partial bars and changing/additive meters; ambiguous tonal collections; deterministic output; exact scratch copy; review/copy/navigation/staleness; malformed worker data and cancellation; bounded worst-case matching; existing editor/playback regressions.

## Risks / rollback
Analysis ranks evidence rather than asserting objective musical structure. Matching must bound repetitive inputs and disclose limits. Unsupported chords are rejected with a measure location. Worker failure must leave editing/results usable. The new command and optional report can be removed without changing saved MusicXML or canonical score semantics.

## Progress
- Completed deterministic features, declared-key fit and scale degrees, modes/minor forms/pentatonics, boundary profiles, variable-length motif families, and bounded variant/inversion/retrograde/rhythmic-scaling relationships. Rhythm-only pitch offsets remain distinct from true transpositions.
- Completed immutable selected snapshots, full-meter/context scratchpad copies, strict result/reference validation, asynchronous dispatch/export, recovery/cancellation, and revision-based staleness.
- Completed Analyze selection, resizable report/findings, note navigation, evidence inspection, accept/reject/naming, report/JSON copying, dock/float preservation, and usage/architecture documentation.
- Build succeeded without new compiler warnings. All 10 CTest suites passed through Poetry (25.37 seconds), including 29 Python cases, real worker/bridge tests, offscreen UI, malformed-response coverage, clipped ties, and existing editor/audio regressions.
- Stress measurements: 20,000-event periodic material (15,000 sustained notes) analyzed in 4.90 seconds; 20,000 uniform notes in 4.03 seconds. Structured analysis was approximately 8.5–8.7 MiB, with reported process peak RSS 150–175 MiB. Limits preserve motif/tonal cards even with many boundary candidates.
- Reference-score checks: four-measure analysis snapshot capture averaged about 0.02 ms on 30 staves / 400 measures; key-edit plus visible engraving p95 2.71 ms. Existing local-edit/layout performance tests passed.
- Rendered and inspected `build/ui-checks/melody-analysis.png`. UI verification used the offscreen Qt backend; live desktop interaction was not manually exercised.
- Analysis/review remain session-only. Melody-changing controls, manual split/merge, persistence, microtones and alternate tuning remain deferred as agreed.
