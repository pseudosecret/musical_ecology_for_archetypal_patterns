---
name: performance-guardrails
description: Review or implement changes that may affect large-score responsiveness, memory use, scrolling, editing latency, layout complexity, rendering, or hot loops. Use for performance-sensitive code and before accepting architecture that adds whole-score work to common interactions.
---

# Performance guardrails

The reference workload is approximately:
- 30 staves
- 13 minutes
- hundreds of measures
- tens of thousands of events

## Product-level targets

Treat these as targets to measure, not claims that already hold:

- scrolling/zooming should visually target 60 FPS on a representative development machine
- a simple local edit should visibly respond within roughly 50 ms at p95 on the reference score
- playhead movement must not trigger engraving/layout
- ordinary local edits should not perform O(total score) work
- memory growth should be bounded while scrolling through a long score

If a target proves unrealistic, report benchmark data and revise it deliberately.

## Before optimizing

Measure:
- layout time by stage
- render preparation time
- frame time
- allocations
- invalidation scope
- number of rendered systems/pages

## Reject these patterns unless justified

- full document relayout after every edit
- full document redraw after every edit
- visual objects for every note in the full score
- copying the entire score for a small mutation
- synchronous Python calls from UI interaction
- rebuilding MIDI schedules every animation frame
- hidden O(N) scans in pointer-move or key-repeat handlers

## Benchmark fixture

Maintain or generate a deterministic stress score approximating the reference workload.

Benchmark both:
1. cold/open layout
2. repeated local edits after caches are warm

Performance regressions should be compared against a recorded baseline, not vibes.
