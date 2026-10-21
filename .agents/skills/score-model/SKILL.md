---
name: score-model
description: Design or modify the canonical C++ musical score model, editing commands, IDs, timing, ownership, or undoable score mutations. Use for domain-model work; do not use for purely visual QML changes.
---

# Score model rules

The C++ score model is the application's source of truth.

## Preserve these invariants

- No dependency on QML.
- No dependency on Python/music21.
- No renderer coordinates in canonical musical objects.
- QtCore value types are allowed where they remain stable and efficient; Qt convenience must not define musical semantics.
- Musical objects that need cross-system references use stable IDs.
- Time representation must be exact enough for notation; do not use floating-point seconds as canonical musical time.
- Separate musical semantics from display geometry.
- Prefer deterministic edits that can be tested without launching Qt.

## Editing

Represent meaningful mutations through an application/edit layer rather than scattering direct field writes through UI code.

An edit should make it possible to determine:
- which musical objects changed
- which measures/systems may require layout invalidation
- whether playback scheduling must refresh
- what is needed for undo/redo

Avoid prematurely building an elaborate event-sourcing framework.

## Data design

Prefer boring, explicit data structures.

Before adding a field, decide whether it belongs to:
1. musical semantics
2. engraving/layout state
3. playback state
4. UI/session state
5. external-format metadata

Put it in the correct layer.

## Definition of done

- domain tests cover the changed behavior
- ownership is clear
- invalidation consequences are exposed intentionally
- no new UI/Python coupling entered the domain model
