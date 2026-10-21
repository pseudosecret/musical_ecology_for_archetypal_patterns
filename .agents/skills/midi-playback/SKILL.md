---
name: midi-playback
description: Implement or modify MIDI import/export, device I/O, playback scheduling, transport controls, tempo handling, or synchronization between the score and playback. Use for timing and MIDI work.
---

# MIDI and playback

Playback is independent from engraving geometry.

## Architecture

```text
canonical score timing
-> playback snapshot / event schedule
-> scheduler
-> MIDI output / synth boundary
```

A separate viewport overlay may observe playback position without rebuilding static engraving; playback must not depend on the renderer.

The first editor milestone must provide audible playback through a built-in sound source.

## Timing

- Keep canonical musical time separate from wall-clock time.
- Resolve tempo maps explicitly.
- Do not drive accurate MIDI scheduling solely from QML timers or the UI event loop.
- Avoid locks or allocations in timing-critical callbacks where practical.
- Make stopping, seeking, and restarting deterministic.

## Editing during playback

For the first editor milestone, editing is unavailable during playback. Disable editing controls in the UI and reject edit commands at the application/edit layer until playback stops. Revisit this policy deliberately if live editing is added.

## MIDI import

MIDI is performance data, not notation truth.
Do not assume imported MIDI uniquely determines:
- voices
- spelling
- beaming
- tuplets
- staff assignment
- enharmonics

Quantization/notation inference should be explicit.

## Verification

Test:
- tempo changes
- seek
- stop/restart
- overlapping notes
- repeated notes of same pitch/channel
- device loss where supported
- edit commands rejected during playback; edits after stopping refresh the schedule
