---
name: change-verification
description: Verify a completed code change in the Music as Becoming notation project. Use after nontrivial implementation work or whenever model, layout, rendering, playback, persistence, or Python integration changes.
---

# Change verification

Before handing off a nontrivial change:

1. Build the affected targets.
2. Run focused tests for the changed subsystem.
3. Run broader regression tests if a shared/core contract changed.
4. Check compiler warnings introduced by the change.
5. If performance-sensitive code changed, run the relevant benchmark or stress fixture.
6. Inspect the diff for architectural boundary violations.

## Specific checks

### Score model
- no QML/Python dependency leaked into domain code
- IDs, ownership, undo/invalidation behavior remain coherent

### Layout/rendering
- no accidental full-score redraw/relayout on a routine local edit
- hidden pages/systems are not unnecessarily materialized
- selection/playhead overlays remain independent where intended

### Playback
- timing is not delegated to UI timers
- transport actions remain deterministic

### Python bridge
- UI thread never waits on Python synchronously
- malformed/failed worker responses are handled

## Report

Summarize:
- what was tested
- what passed
- what was not tested
- any known performance or architectural risk

Do not claim a test was run if it was not.
