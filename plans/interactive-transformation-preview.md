# Interactive transformation preview

### Goal
Adjust an ordered transformation chain against a frozen selection, hear the latest
preview, and inspect automatically refreshed reports. Keep one editable scratch
result or cancel without changing the source.

### Constraints
C++ owns scores and session state. Python remains asynchronous under protocol 2;
analysis schema 2 is unchanged. Only the selected fragment is rebuilt. Playback
uses the shared transport; a preview must never interrupt another score's playback.

### Affected subsystems
- application/editor and transformation workspace
- QML controls, result presentation, keyboard transport
- Python bridge scheduling and analysis installation
- playback ownership and scratch layout invalidation
- tests and selected-region benchmarks

### Proposed design
Extend TransformWorkspace with one temporary preview session and reusable ordered
step controls. Freeze source/context when configuration opens. Coalesce adjustments
for 150 ms, flush on release, and invalidate superseded requests. Replace the same
locked scratch editor after validation on the processing thread. Stop its audition
before replacement and restart once, when visible and enabled. Analyze an immutable
matching snapshot after 500 ms idle; install only if editor/revision still match.
Retain old reports with stale labeling and disabled finding actions. Keep unlocks
the tab; Cancel invalidates jobs and removes it. Presentation unloads retain C++
parameters and suspend automatic audition.

### Steps
1. Add session lifecycle, worker priority/revision guards and editor edit lock.
2. Share transform controls between setup and the nonmodal preview pane.
3. Integrate presentation visibility, audition and report status.
4. Verify lifecycle/races, playback ownership, UI persistence, and selected work.

### Verification
Build without new warnings; run focused preview/analysis/UI tests and all CTest
suites through Poetry. Exercise actual music21 transforms and reports, cancellation,
invalid results, manual Stop and stale responses. Render and inspect the preview
UI. Measure short and larger selected-fragment updates and assert source revisions,
layout and tab count stay unchanged.

### Risks / rollback
Cancellation can discard an in-flight worker process; request IDs and editor
revision guards prevent stale installation. Large selected passages still take
worker time; debounce bounds submitted work, while reports may be cancelled by
new adjustments. Existing Submit and standalone analysis remain independent.

### Progress
- Complete: one preview editor, shared immutable baseline, ordered reusable controls,
  debounce/release flushing, Reset/Keep/Cancel, revision-guarded reports, and owned
  audition with visibility and manual-stop handling. Superseded processing jobs skip
  serialization/validation; superseded sidecar jobs are cancelled asynchronously.
- A renderer regression found during visual inspection is fixed: complete score
  replacement rebuilds scene-graph nodes so software rendering retains the viewport
  transform and clipping. Ordinary incremental invalidation is unchanged.
- Build passed without new compiler warnings. All 11 CTest suites passed through
  Poetry (final run: 51.11 s), including real music21 transformations, reports,
  source/review isolation, cancellation/failure/revision races, transport ownership,
  shutdown during audition, keyboard/pointer controls, dock/minimize restoration,
  and pixel comparison after rhythm replacement.
- The 30-staff / 400-measure fixture measures both 22- and 800-quarter selections,
  with a matching validation/layout baseline and snapshot/editor overhead. Actual
  800-note worker previews also measure cold and repeated warm updates. Source
  revision/layout/reset counts remain unchanged across updates.
- Rendered and inspected `build/ui-checks/interactive-preview.png` and
  `build/ui-checks/interactive-rhythm-preview.png`. UI checks use Qt offscreen/software
  rendering and file audio. Native desktop interaction and speaker output were not
  checked in this environment. Preview/review persistence remains deferred.
