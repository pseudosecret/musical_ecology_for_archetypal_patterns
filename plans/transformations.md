# Passage transformations

## Goal
Single-staff T transforms through a Python/music21 worker into independent editable result tabs. Results float or dock below the score; clipboard transfers music back. Save only on explicit request.

## Constraints
C++ remains canonical; Python proposes exact selected-region results asynchronously. Reuse notation, incremental layout, clipboard, and a shared audio engine. Editing is locked globally during playback. No ancestry or workspace persistence.

## Design
Versioned bounded JSON-lines IPC carries integer ticks as strings, pitches, per-tone ties, tuplet hints and ordered operations. Python uses music21 symbolic objects and Fraction timing; C++ validates and spells returned fragments before constructing scratch scores. Preserve starting meter/tempo/clef/program and beat offset. Each result owns its score/history; one shared transport owns audio.

## Steps
1. Add exact transformation DTO, rhythm normalization and Python worker with tests.
2. Add asynchronous worker/workspace controller; handle failure, cancellation and dependencies.
3. Share transport, bound preview playback, add scratch editor initialization and T/Shift+T.
4. Add ordered-step dialog, result tabs, dock/float, focused editing, saving and close protection.
5. Run musical, bridge, UI/audio and orchestral regression checks; document setup/results.

## Verification
Exact musical examples; order, ties, tuplets, rhythm fractions and overflow rejection. Real worker and hostile worker responses/cancellation. Keyboard, paste to other staves, independent undo, docking and explicit MusicXML save. Shared playback and edit locks. Existing CTest suites and selected-region benchmarks.

## Risks / rollback
Python setup failures must leave the editor usable. Never install invalid proposals or overwrite existing results. Scratch tabs are temporary unless explicitly saved. The worker boundary and independent results isolate this feature from source edits.

## Progress
Implementation complete. Added the versioned worker, exact operation pipeline, C++ normalization/validation, independent editable result tabs, dock/float presentation, explicit MusicXML save, clipboard transfer and shared transport. T transforms one staff; Shift+T edits tempo.

## Verification results
- `cmake --build build -j 2` passed with no new compiler warnings. CMake's existing optional Vulkan-header message remains.
- `ctest --test-dir build --output-on-failure`: all seven suites passed in 23.42 seconds. Model, tuplet, clipboard, WAV/audio, UI and bridge regression checks passed.
- Python exact-math tests: eight passed with symbolic test doubles. Two real music21 tests and one real C++/music21 bridge test explicitly skipped because music21 is unavailable.
- Dependency installation attempted in `.venv`; package access failed under the environment's network restrictions. The root `pyproject.toml` now pins music21 10.5.0 under Poetry; README documents setup and interpreter selection. No fallback silently changes transformation semantics.
- UI tests exercised keyboard entry, partial copy to another staff, separate undo, multiple tabs, scroll preservation through tab/dock/float changes, dialogs in the floating window, edited-tab close protection and Escape cancellation. Inspected `build/ui-checks/transformed-result.png`.
- On the 30-staff / 400-bar fixture: selected-region capture + result validation/layout p95 0.23 ms (Python execution excluded); ten-staff paste + visible layout + undo p95 4.46 ms; single-note paste + layout + undo p95 1.37 ms; tuplets p95 1.17 ms. No main-score layout is required to create/edit a result.
- Reviewed model ownership, exact timing, playback locks, asynchronous IPC and source isolation. Native desktop window/clipboard behavior and live speaker output were not manually checked; offscreen interaction and offline audio checks passed.


## Poetry setup correction
Replaced the manually created `.venv` and pip requirements with a root Poetry
project in dependency-only mode. The previous empty environment was moved out
of the project to `/tmp` for recovery. The worker uses the PATH inherited from
`poetry run` (or an explicit `MUSIC_PYTHON`), and no longer auto-selects `.venv`.
The project does not add shell activation hooks. An already activated terminal
must run `deactivate` once; subsequent commands use `poetry run`.
