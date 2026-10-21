# Execution Plans

Use an execution plan only for changes that cross subsystem boundaries, modify a core contract, introduce a new persistent format/protocol, or require multiple coordinated implementation stages.

This file is a template, not a running plan. Store each concrete plan in a separate `plans/<slug>.md` file. Create the directory when the first plan is needed.

Keep plans short and executable.

## Template

### Goal
What user-visible or architectural outcome are we creating?

### Constraints
List only relevant constraints from `AGENTS.md` / `ARCHITECTURE.md`.

### Affected subsystems
- score model
- engraving/layout
- rendering
- MIDI/playback
- QML/UI
- Python bridge
- persistence/import/export
- tests/benchmarks

Remove irrelevant entries.

### Proposed design
Describe data flow and ownership. Call out thread boundaries and invalidation behavior.

### Steps
1. Small independently testable step.
2. Next step.
3. Integration step.

### Verification
Specify tests, benchmark fixture, and manual behavior to check.

### Risks / rollback
What might go wrong, and what boundary lets us back out safely?
