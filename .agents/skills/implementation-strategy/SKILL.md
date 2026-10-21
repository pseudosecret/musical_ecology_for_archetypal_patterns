---
name: implementation-strategy
description: Choose an implementation boundary before a change that spans multiple subsystems, changes a core score/layout/playback/Python contract, or introduces a new architectural dependency. Use before coding broad or high-impact features.
---

# Implementation strategy

Before coding a broad change, identify:

1. Which subsystem owns the behavior?
2. What data crosses subsystem boundaries?
3. Which thread/process performs the work?
4. What is the invalidation scope?
5. How will it be tested?
6. Does the change introduce whole-score work into a common interaction?
7. Can this be implemented behind an interface so it can be replaced?

Prefer the smallest boundary-preserving design.

Do not:
- move logic into QML because it is convenient
- move canonical state into Python because music21 already has an object for it
- bind playback to engraving geometry
- add a cross-thread mutable object graph
- introduce a major dependency without stating why existing layers cannot solve the problem

If the task changes a decision in `ARCHITECTURE.md`, surface that explicitly before implementing it.
