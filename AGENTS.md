# Music as Becoming — Agent Guide

**Music as Becoming** begins as a high-performance staff notation editor. The first milestone imports and exports MIDI and MusicXML, saves and reopens scores with MusicXML, and provides audible playback through a built-in sound source. Later stages add musical transformations, ancestry, optional language-model control, and audio tools.

Keep this file short. Use it as a map and a set of hard architectural constraints. Read only the deeper docs relevant to the task.

## Core architecture

- Desktop stack: **C++23 + Qt 6 + Qt Quick/QML**.
- The canonical musical document model lives in **C++** and must not depend on QML, Python, music21, or a specific renderer.
- QtCore value types may be used in the model when they are stable and efficient; Qt convenience must not define musical semantics.
- QML is for application UI and lightweight view state. Do not put canonical musical state or engraving logic in QML.
- Engraving/layout is a C++ subsystem that produces renderable system/page snapshots.
- MIDI playback is independent from engraving and visual state.
- The first editor milestone, including MIDI and MusicXML import/export, must work without Python/music21. Future analysis and transformation tools may use a Python worker.
- Python work must never block the UI/render thread.
- MusicXML is also the first milestone's score save/reopen format. Neither MusicXML nor MIDI is the canonical in-memory model.

See `ARCHITECTURE.md` for boundaries.

## Performance is a product requirement

The reference stress case is an orchestral score of roughly:
- 30 staves
- about 13 minutes
- hundreds of measures
- tens of thousands of note/events

Ordinary local edits must not trigger full-score layout or rendering.

Rules:
- Prefer incremental invalidation.
- Re-layout the smallest affected region that preserves correctness.
- Render only visible systems/pages plus a small prefetch margin.
- Never represent each note as a heavyweight QML/Qt widget.
- Keep playback cursor movement independent from engraving/layout.
- Avoid per-frame allocations in hot rendering paths.
- Do not add an O(total score) operation to a common edit path without documenting and benchmarking it.

Use `$engraving-layout` for score layout/rendering work.
Use `$performance-guardrails` for changes that can affect responsiveness.

## Ownership and C++

- Use RAII.
- No manual owning `new`/`delete`.
- Prefer values and `std::unique_ptr`; use shared ownership only when the model genuinely requires it.
- Raw pointers/references may be non-owning and should be obvious from context.
- Prefer deterministic, testable functions for layout calculations.
- Avoid clever template machinery unless it materially improves the design.

## Threading

- UI and scene-graph rules must follow Qt's threading model.
- Expensive analysis/layout that can be safely detached from UI state should be movable off the UI thread.
- Share immutable snapshots or explicit message/data objects across threads.
- Do not expose mutable score internals across threads.
- Python workers communicate through explicit messages and never mutate C++ score state directly.

## Python/music21 boundary

Use `$python-music21-bridge` whenever adding Python or music21 integration.

For features that use Python, the default approach is:
- Python runs as a worker/sidecar process.
- Exchange explicit versioned data contracts.
- Send only the score region needed for the task when practical.
- Return proposed changes/results to C++; C++ validates and applies them.
- Do not make `music21.stream.Score` the application's source of truth.

The implementation language for future transformation tools is undecided; their operations and results must remain inspectable and editable.

## GUI

Use `$qt-qml-ui` for UI work.

- QML owns presentation, controls, menus, inspectors, dialogs, and local visual state.
- C++ exposes intentional APIs/models to QML.
- Keep score drawing in a custom high-performance viewport, not thousands of QML note objects.
- Do not let UI convenience leak into the score model.

## MIDI/playback

Use `$midi-playback` for playback and device work.

- Playback consumes musical timing data from the C++ score model.
- Playback must not depend on rendered geometry.
- Static engraving must not depend on active playback. A separate overlay may follow playback position without triggering layout.
- Timing-sensitive work must not rely on the UI event loop.
- During playback, disable editing controls and reject edit commands in the application/edit layer until playback stops.
- The first milestone must produce audible playback without external MIDI hardware or another application.

## Tests

Use `$change-verification` before finishing nontrivial code changes.

At minimum:
- domain/model tests for musical data mutations
- deterministic layout tests where possible
- regression tests for bugs
- performance benchmarks for hot-path or large-score changes

## Planning

For a change that crosses multiple subsystems, changes a core data contract, or is likely to require several implementation stages, create/update a separate execution plan using the template in `PLANS.md`.

For small isolated tasks, do not create ceremony.

## Do not silently change these decisions

Do not replace C++/Qt, move the canonical model to Python, couple music21 directly to the renderer, or switch the score viewport to a webview without explicitly surfacing the architectural change and its tradeoffs.
