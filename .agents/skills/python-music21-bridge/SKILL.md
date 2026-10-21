---
name: python-music21-bridge
description: Add or modify Python, music21, IPC, analysis/composition workers, score serialization for Python, or results returned from Python to the C++ application. Use whenever Python crosses the application boundary.
---

# Python / music21 bridge

The first editor milestone, including MIDI and MusicXML interchange, must work without Python. Future analysis and transformation tools may use a Python worker, but the C++ score model remains canonical. The runtime for future built-in transformations is undecided.

## Default topology

Start with a separate Python worker process.

Benefits:
- Python failure does not kill the editor
- interpreter/GIL work stays off the UI thread
- dependencies remain isolated
- protocol boundaries stay explicit

Do not embed CPython/pybind11 until a measured requirement justifies tighter coupling.

## Data contracts

Use explicit versioned request/response DTOs.

Every request should have:
- protocol version
- request ID
- command/type
- relevant score fragment or reference data
- options

Every response should have:
- request ID
- success/error status
- structured result or structured error

Prefer sending only the necessary score region when practical.

## Applying results

Python proposes analysis/results/edits.
C++ validates and applies mutations through the normal edit layer.

Python must not directly mutate live C++ model internals.

## music21

Use music21 where its strengths matter:
- harmonic analysis
- symbolic transformations
- corpus/musicological utilities
- prototype generative algorithms
- notation-oriented symbolic reasoning

Do not contort the canonical C++ model to mirror music21 classes one-for-one.

## Failure behavior

Handle:
- worker unavailable
- dependency/import failure
- malformed response
- incompatible protocol version
- timeout/cancellation
- Python exception

The notation editor should remain usable when Python is unavailable.
