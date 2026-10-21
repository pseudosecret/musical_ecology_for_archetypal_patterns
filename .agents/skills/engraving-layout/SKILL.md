---
name: engraving-layout
description: Implement or change notation engraving, measure/system/page layout, glyph placement, score viewport rendering, caching, or layout invalidation. Use whenever a score edit could affect geometry or rendering performance.
---

# Engraving and layout

Treat engraving as a pipeline that converts musical semantics into render geometry.

## Primary rule

Do not recompute or redraw more of the score than correctness requires.

## Preferred stages

```text
score model
-> measure engraving
-> horizontal spacing
-> system composition
-> vertical staff/system spacing
-> page/viewport placement
-> render snapshot
```

Make stage boundaries cacheable where useful.

## Invalidation

For every layout-affecting edit, explicitly identify its invalidation scope.

Examples:
- pitch-only edit may affect accidental spacing, stem direction, ledger lines, and local vertical extents
- duration edit may affect beams, spacing, and measure width
- clef/key/time signature edit may affect later notation semantics
- measure width change may cascade into system reflow

Never assume "local edit" means "local geometry", but never use that uncertainty as an excuse for unconditional full-score relayout.

## Rendering

- Render visible systems/pages plus a small prefetch margin.
- Avoid one QObject/QML item per notation glyph.
- Prefer batched/custom scene-graph geometry.
- Cache reusable glyph outlines/geometry.
- Keep selection, caret, hover, and playback overlays separable from static score geometry where practical.
- Playback position updates must not invalidate static engraving.
- Avoid allocations in per-frame drawing paths.

## Correctness before micro-optimization

Keep layout code deterministic and testable.
Optimize from profiling evidence, except for obvious architectural hazards such as full-score redraw on every edit.

## Verification

For layout changes test:
- deterministic expected positions or relationships
- boundary/reflow behavior
- zoom/scroll correctness
- a large-score benchmark when hot paths are touched
