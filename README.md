# Music as Becoming: notation editor prototype

This is a keyboard-first desktop notation editor prototype. It supports
multiple parts/staves, chords, rests, ties, clef/key/meter/tempo changes, FluidSynth
playback, and save/reopen of a supported MusicXML subset. The score model,
engraving, and playback timing live in C++; QML provides the application controls.

## Build and run

On Ubuntu 22.04, install the build tools **and** the runtime plugins/modules:

```sh
sudo apt-get update
sudo apt-get install g++-12 cmake ninja-build pkg-config \
    qt6-base-dev qt6-declarative-dev qt6-qpa-plugins qt6-wayland \
    qml6-module-qtqml qml6-module-qtqml-workerscript \
    qml6-module-qtquick qml6-module-qtquick-window \
    qml6-module-qtquick-controls qml6-module-qtquick-templates \
    qml6-module-qtquick-layouts qml6-module-qtquick-dialogs \
    libfluidsynth-dev libpulse-dev fluid-soundfont-gm
```

Qt development packages alone do not provide every runtime QML module or the
Wayland platform plugin. The explicit list above includes the QML modules used
by the controls and their default style, as well as the application's imports.

An audio output device or PulseAudio server is needed for playback. The
bundled Bravura font is loaded automatically.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/g++-12
cmake --build build
ctest --test-dir build --output-on-failure
./build/music_as_becoming
```

The default SoundFont path is `/usr/share/sounds/sf2/FluidR3_GM.sf2`.
Set `MUSIC_SOUNDFONT` to a different `.sf2` file if needed. Audio setup
runs in the background; Play becomes available when it is ready.

The Volume slider controls playback and instrument/note previews together and
starts at 50%. On Linux, the PulseAudio output worker renders 256-frame chunks
at 48 kHz and requests a bounded 2,048-frame queue (about 43 ms), or 4,096 frames
(about 85 ms) for WSLg. Playback starts after the full safety buffer is filled.
PulseAudio requests refills asynchronously; cached latency reads never wait for
server timing data on the audio thread. The queue does not grow adaptively. The cursor and playback
completion follow the synthesis clock minus measured output latency. Stop and
score switches discard queued audio. Other drivers use 512-frame blocks by
default. `MUSIC_AUDIO_BUFFER` overrides the queue/block frame count (64–8,192).
The synth and output use the same sample rate; UI frame rate does not control
audio timing. The status line includes measured PulseAudio latency during Play.

`music_audio_tests` checks actual WAV samples for pitch, clipping, dropouts,
instrument differences, and volume. Its recordings are saved under
`build/audio-checks/` so they can also be listened to outside the live synth
output path. These checks do not establish that a desktop audio server or
speakers are working correctly.

`music_pulse_tests` exercises the real synth/output worker against a simulated
PulseAudio device with queue backpressure and extra device latency. It checks
rapid previews, audible cursor/completion timing, score switching, and disconnects.
Startup and post-flush tests delay timing information and check for underruns.

### Plugins and dependencies by feature

These requirements cover the current editor and the first milestone described
in [ARCHITECTURE.md](ARCHITECTURE.md).

| Feature | Plugin/module or dependency | When needed |
| --- | --- | --- |
| Desktop window on Wayland, including WSLg | `qt6-wayland` (`wayland` / `wayland-egl` platform plugins) | To display the editor in a Wayland session. Install from the same Qt distribution/version as the Qt libraries. |
| Desktop window on X11 / XWayland | `qt6-qpa-plugins` (`xcb` platform plugin) | To display the editor through an X11 server. This package also provides `offscreen` for headless tests. |
| QML engine and script support | `qml6-module-qtqml`, `qml6-module-qtqml-workerscript` | Runtime support used by the QML UI and its controls. |
| Score viewport and application window | `qml6-module-qtquick`, `qml6-module-qtquick-window` | Qt Quick scene and window types; engraving remains in C++. |
| Menus, buttons, inspectors, and dialogs | `qml6-module-qtquick-controls`, `qml6-module-qtquick-templates`, `qml6-module-qtquick-layouts`, `qml6-module-qtquick-dialogs` | Controls, their style templates, layout containers, and MusicXML open/save dialogs. |
| Audible notes, chords, and instrument playback | FluidSynth (`libfluidsynth-dev` for building), PulseAudio's client API (`libpulse-dev` for Linux builds), a General MIDI SoundFont (`fluid-soundfont-gm`), and a working audio server/device | The built-in sound source. Qt Multimedia, a VST instrument, and external MIDI hardware are not required. |
| MusicXML import/export and score save/reopen | Qt XML, supplied by `qt6-base-dev` | Implemented in C++; no separate format plugin or Python package. |
| MIDI file import/export (planned first-milestone work) | A C++ MIDI file adapter; implementation dependency is not selected yet | No runtime MIDI plugin or Python/music21 requirement has been chosen. MIDI file interchange is distinct from MIDI device I/O. |
| Passage transformations and melody analysis | Python 3.11+ and music21 10.5.0 in a separate worker process | Required for `T` transformations and analysis; ordinary editing, playback, and MusicXML continue without Python. |
| Language-model control, plugin instruments, and DSP | Providers, plugin formats, and dependencies remain undecided | Later features; no packages for these are required for the first editor milestone. |

### Diagnosing startup

If Qt reports `Could not find the Qt platform plugin "wayland"`, install the
missing runtime dependency and launch normally:

```sh
sudo apt-get install qt6-wayland
./build/music_as_becoming
```

No rebuild is needed for a system runtime plugin installation. CMake warns
when configuring in a Wayland session if the selected Qt installation's
default plugin directory lacks Wayland support. Qt still selects the display
backend; the application does not force X11 or suppress plugin errors.

If a plugin is present but **cannot load**, inspect Qt's loader diagnostics:

```sh
QT_DEBUG_PLUGINS=1 ./build/music_as_becoming
```

This distinguishes missing shared libraries or incompatible Qt plugins from a
missing plugin. A `could not connect to display` or `Failed to create wl_display`
error instead requires checking the desktop session/display server. Under WSL,
launch from a WSLg-enabled session with its display socket and environment
available. The automated tests intentionally use `offscreen`; passing them
does not verify desktop connectivity or speaker output.

References: [Ubuntu's Qt 6 Wayland package](https://packages.ubuntu.com/jammy/qt6-wayland)
and [Qt's plugin loading and diagnostics guide](https://doc.qt.io/qt-6/deployment-plugins.html).

## Editing

Click an event to select it, then use:

| Keys | Action |
| --- | --- |
| `A`–`G` | Enter the next note, using the nearest octave |
| `Shift+A`–`Shift+G` | Add a pitch to the selected note/chord |
| `Left` / `Right` | Select the previous / next event |
| `Shift+Left` / `Shift+Right` | Extend or contract one continuous time selection |
| `Ctrl+click` | Add or remove a staff from that same time selection |
| `Ctrl+C` / `Ctrl+V` | Copy / overwrite-paste the selected passage |
| `T` / `Shift+T` | Transform or analyze a single-staff passage / open tempo settings |
| `Up` / `Down` | Move the selected pitch by one staff step |
| `Shift+Up` / `Shift+Down` | Move the selected pitch by a semitone |
| `Ctrl+Up` / `Ctrl+Down` | Move the selected pitch by an octave |
| `[` / `]` | Halve / double the selected duration |
| `.` | Toggle a duration dot |
| `3` / `5` / `7` | Divide the selected note/rest into a triplet, quintuplet, or septuplet |
| `+` / `-` / `=` | Sharp / flat / natural |
| `Space` | Play from the selected measure (the first measure by default), or stop if playing |
| `Delete` / `Backspace` | Replace the selected note with a rest; remove a clicked clef change |
| `P` / `Shift+P` | Play from the beginning / selected measure; press again to stop |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+S` / `Ctrl+O` / `Ctrl+N` | Save / open / new score |

Plain note entry advances into the next rest while keeping the entered note
selected. This lets `C`, `Shift+E`, `G` enter a C–E chord followed by G.
Editing is disabled during playback.

Selected noteheads and rests are orange. Selecting a measure highlights its
region. `S` opens the clef dialog, `M` the time signature, `Shift+T` the tempo,
and `T` transforms the selected single-staff passage.
Choose **Interactive preview** to adjust that passage with sliders and exact values
beside the score and report. Steps run in order against the original captured
selection. Changes settle for 150 ms; slider release applies immediately. With
**Auto-audition** enabled, the updated passage plays once. **Stop pending audition**
or Escape cancels its pending restart. Reports refresh automatically after a pause;
previous findings stay disabled until the matching report arrives.

**Keep result** turns the same preview into an editable scratch score; save it as
MusicXML to retain it. **Reset** restores the captured selection and neutral
parameters. **Cancel** removes the preview. Docking or minimizing retains the
controls; minimizing stops audition, and the **Transform results** tab restores
the window. Source music and its report/reviews are unchanged.

To remove a clef change, click its symbol and choose **Remove change**, or press
Delete after closing the dialog. The initial clef is retained; later changes
can be removed and undone. Instrument choices in **Instrumentation** apply
immediately; **Use part instrument** clears a staff's override so it follows
the part. **Rename** applies name edits.

For copy/paste, select a note, rest, or measure and use Shift+Left/Right or
Shift-click to extend the time range. Ctrl-click another staff to include or
exclude it without changing the time range. Selections stay continuous in time,
and may skip staves. Range highlights follow the selected staves across systems.
Click or use an unmodified arrow to return to a single event; Escape clears the
range. Other pitch and duration edits require a single-event selection.

Ctrl+C takes an independent copy. Select the paste location and exactly the same
number of destination staves, then press Ctrl+V. Staff order determines mapping:
source staves 1 and 3 paste onto selected destination staves 2 and 4 as 1→2 and
3→4. All destinations share the first selection's starting time. The copied
duration determines what is overwritten, including copied rests; later music
stays at its existing time. The score gains measures when needed, using its
final meter. One undo reverses the entire paste, including added measures.

Copy/paste preserves pitches, chords, rhythms, internal ties, and tuplet timing;
destination keys, clefs, meter, tempo, and instruments are retained. Sounding
pitches are preserved; accidentals are displayed for the destination key. Notes crossing new
barlines are split and tied. Partial tuplets save and reopen through MusicXML.
Some overlaps between different tuplet rhythms require unsupported notation;
those pastes are rejected without changing the score or rounding time. The
private clipboard format supports this application's instances and scores, with
a 32 MiB payload limit; it does not provide interchange with other notation apps.

Tuplets keep the selected duration's total span. A quarter becomes three triplet
eighths, five quintuplet sixteenths, or seven septuplet sixteenths. An existing
note or chord stays in the first slot, with rests after it. Use the duration
and dot keys for mixed rhythms inside the group; edits cannot cross its edges.
Dotted durations use their full span: a dotted quarter divided into three
becomes ordinary eighths, while five and seven use 5:3 and 7:6 ratios.
Groups save and reopen with MusicXML and play with exact musical timing.
Nested groups and written values shorter than 32nd notes are not supported.
Paste can split a group into fragments at barlines; meter changes that would
cut an existing group are rejected.
Undo reverses creation.

## Current scope

MusicXML import accepts the supported multi-part/multi-staff subset this
prototype writes, with one voice per staff. Unsupported notation is rejected
with an error instead of silently changing the music. MIDI file import/export,
multiple voices per staff, broader MusicXML support, and full orchestral
engraving remain future work.

## Passage transformations and Python setup

Use Poetry from the project root to install the optional transformation dependencies:

```sh
poetry install
cmake --build build
poetry run mefap
```

After updating an existing checkout, register the launcher once with
`poetry install --only-root`. `poetry run mefap` launches `build/music_as_becoming`
and forwards any arguments. The direct executable command also remains available.
If `poetry run mefap` reports a permission error in a Windows PATH directory
(such as Muse Hub), the project launcher has not been installed in the active
Poetry environment. Run `poetry install --only-root` from this project, then
retry `poetry run mefap`.

Dependencies are declared in the root `pyproject.toml`; Poetry manages the
Python 3.11+ environment. No shell activation is needed. `poetry run` supplies
the worker's interpreter through PATH; `MUSIC_PYTHON` can explicitly override it.
The editor does not select a project-local `.venv` automatically.
This launches the same C++ audio engine as running the binary directly; Poetry
provides the Python environment, and the worker starts on the first Submit,
Analyze selection, or key-suggestion request.
CMake copies the worker beside the executable into `build/python/`. No Python
interpreter is embedded, and missing Python/music21 only disables transformations and analysis.
The editor never installs packages or writes a score automatically.

Select one note/rest, a measure, or a continuous passage on **one staff**, then
press `T`. Add and reorder transposition, inversion, exact rational rhythm
scaling, and retrograde steps; Submit creates a new result tab. Pitch operations
default to **Scale degrees**: transposition `+1` moves to the next degree and
`+7` moves an octave. Diatonic inversion uses the chosen local axis degree
(tonic by default) and tonic octave. **Exact semitones** retains chromatic
transposition and inversion around an absolute MIDI pitch. Results outside
MIDI 0–127 or the supported exact notation grid are rejected without rounding.

Results retain the starting meter, tempo, clef, effective instrument, and beat
position, plus every selected key/scale change. Later clef, meter, and tempo
changes are not copied. Leading alignment rests
and trailing bar-fill rests are outside the initially selected result. **Play
selection** auditions that exact selection; the first Play auditions the entire
transformed passage. Main-score playback continues to start at a measure.

### Keys, scales, and accidentals

Select one event or measure and click **Key / scale…**, or click a printed key
signature. Changes apply to that staff at the selected position, preserve
existing pitches, and support undo. Later changes can be removed in the dialog
or with Delete after clicking the signature. Signatures repeat at system starts;
letter-name entry follows the signature and measure accidentals. Explicit
sharp/flat/natural controls remain available.

Major and minor are explicit identities, even when their signatures match.
Minor defaults to **melodic**, with raised sixth and seventh in both directions.
Choose **harmonic (natural 6)** or **Aeolian (natural minor)** for the other
collections. All minor forms use the conventional minor key signature; raised
tones are shown with accidentals. Forms are saved with their key regions in
MusicXML using application metadata alongside standard fifths/mode values.

In the transformation dialog, **Preserve chromatic inflections** carries each
note's alteration relative to its scale degree (and reflects it during
inversion). **Constrain to scale** removes those inflections. For example, a
C-major C–E–G chord moved one scale step becomes D–F–A; moved two semitones it
becomes D–F♯–A. Held ties retain the context at their attack. Operations that
collapse distinct chord tones onto one pitch are rejected.

Retrograde keeps key regions in their existing positions and remaps reversed
material's degrees into those local keys. **Reverse key regions too** reverses
the region intervals as well. Rhythm scaling scales key-change positions with
the music. Source music remains unchanged; ordinary paste keeps destination keys.

Imported music without a declared tonic/mode needs a confirmed key for tonal
operations. **Suggest missing keys** analyzes the selected regions with music21;
choose a candidate and click **Use key**, or select one manually. Suggestions
remain unconfirmed until that action and affect only the transformation context.
Existing signatures constrain the major/minor suggestions. Rest-only regions
need manual selection. Automatic modulation boundaries and minor-form inference
are not included. Other modes, microtonal pitches, and alternative tuning are
future work in the score model and transformation controls. The melody analyzer
can already suggest modes and pentatonics; current playback remains integer-semitone MIDI.

Submitted and kept results are independent editable scratch scores with their own undo history.
Use the existing selection and Ctrl+C/Ctrl+V commands to copy any passage into
any single main-score staff; pasting overwrites and uses the destination's
instrument. Further transformations create additional result tabs. The step
summary remains visible in each result.

Use **Dock below score / Float window** to change presentation. Minimizing or
closing the floating window tucks it into a **Transform results (N)** tab at the
bottom-left of the main window. Click that tab or **Results** to restore the same
results, including edits, undo history, selection, and scroll position. **T**
opens setup for another transformation after an active preview is kept or cancelled. Closing an individual result tab
with manual edits offers explicit saving or discarding. **Save as MusicXML** is
always an explicit action; generated tabs, source snapshots, and operation settings
are temporary and are not restored on restart. MusicXML saves the resulting
music, not transformation ancestry. Playback uses one shared synth and locks
editing in all scores until it stops.

The bridge uses versioned, bounded JSON-lines requests with decimal integer tick
strings, request IDs, ordered steps, per-pitch ties and tuplet hints. Python
proposes music; C++ validates and spells it before installing a result. Requests
are cancellable and time out after 60 seconds. Errors remain in the dialog or preview controls;
existing scores and results stay usable.

Run the Python tests in the same environment used by the application:

```sh
poetry run python -m unittest discover -s python -p 'test_*.py' -v
poetry run ctest --test-dir build --output-on-failure
```

The exact-math tests use symbolic test doubles when exercising the operation
rules. Separate real music21 tests are explicitly skipped if the package is
missing. C++ bridge tests also exercise process failure, malformed responses,
invalid proposals, cancellation, and timeout with a protocol test worker.

## Melody analysis

Select a melody on one staff in the main score or a scratchpad, press **T**, and
click **Analyze selection**. Analysis opens an unchanged scratchpad copy with a
resizable report alongside the notation. The copy retains the selected passage's
meter, key, clef, and tempo changes, including its starting beat position.
Measure/beat labels in the report refer to the source measures. Alignment rests
outside the excerpt are excluded. Chords are rejected with a measure location;
ties are treated as single sustained notes. Missing keys, rests, single notes,
and excerpts clipped through ties are handled explicitly.

The **Report** explains pitch/register/range, steps and leaps, rising/falling
motion and contour changes, durations and onset intervals, rhythmic density,
metric strength, rests, declared-key fit, and tonal alternatives. Tonal candidates
include the seven diatonic modes, harmonic and fixed melodic minor, and major/minor
pentatonics. Short excerpts can fit several collections; candidate rankings keep
that ambiguity visible and do not change the score's declared keys.

Structural findings distinguish interval/rhythmic/repeated-note **cells**, coherent
**gesture candidates**, and conservative **motif candidates**. A motif proposal
requires independent recurrence, supported grouping, and recognizable identity;
ordinary repeated scale fragments remain cells. One-off gestures remain available.
Departure-and-return shapes such as A–B–A and E–A–E can share a gesture family
without being declared occurrences of the same motif. Three or more similarly
grouped abstract occurrences with rhythmic contrast on strong metric positions
provide additional motif evidence. These are inspectable heuristics, not formal
or stylistic judgments.

Signatures expose sounding pitches, chromatic and written diatonic intervals,
contour, pitch equality, exact returns, repeated-note positions, and durations with
internal rests normalized by their greatest common divisor. Exact returns retain
octave position. New short candidates span 2–12 sustained notes; existing
variable-length exact repetition remains available. Longer exact families expose
a 12-note signature prefix plus complete interval/duration evidence. Existing
boundaries, tonal alternatives, approximate variants, inversion, retrograde,
rhythmic scaling, and containment remain available. Rhythm-only matches distinguish
initial pitch offsets from actual transposition.

Evidence strength describes the stated detected pattern. Separate **salience**
ranks local prominence using grouping/metric support (35%), contour/rhythmic
emphasis (35%), and independent recurrence (30%). Correlated return/contour cues
use their strongest contribution rather than accumulating votes. Excerpt edges
alone supply no grouping evidence. Scores are heuristics, not probabilities.

In **Findings**, the default **Structures** view groups motif and gesture families,
ranked by salience within category. Choose **Cells** or **All findings** for the
supporting material. Cards explain their evidence and prominence; numeric scores
remain under **Inspect measurements**. Use **Show notes** and the occurrence
selector to navigate the notation. Accept or reject cells, gestures, motifs and
boundaries; name structural candidates with **Name** or Enter. Accepting a gesture
confirms it as a gesture and does not promote it to a motif. Automatic findings
remain available separately from those review
decisions. Review decisions are authoritative annotations for future consumers;
they do not rerun segmentation or generate new music in this version.

**Copy report** copies the readable text. **Copy structured data** copies JSON
containing analysis schema 2 / algorithm `melody-v2`, exact selection-relative tick strings
(6,720 ticks per quarter), per-note features, source event references and meter map,
candidate regions, motif occurrences, relationships, algorithm parameters, and
review decisions. Findings and note IDs are scoped to that analysis result. The
source data maps excerpt ticks to the scratchpad using its `offset` and to original
measure numbers using its measure map. Large JSON exports run off the UI thread.

Editing an analyzed scratchpad marks its report **Out of date** and disables note
navigation and review until reanalysis. The old report remains copyable. Analysis
and review are session-only; saving MusicXML saves the notation. Manual split/merge,
analysis persistence, microtones, and alternate tuning remain later work.

Analysis supports up to 20,000 sustained notes, 100,000 notation events, and 20,000
selected measures within the 32 MiB worker-message bound. Exact-repeat searches
index the full excerpt. Approximate comparisons operate on discovered motifs and
phrase candidates of at most 64 notes, with at most 4,000 comparisons. Reports
retain up to 120 motif families and 64 occurrences per family, with category limits
that preserve room for different kinds of findings. Coverage notices identify
limits reached; complete per-note measurements and the boundary-strength profile
remain in structured data. Standalone analysis is explicit and cancellable; ordinary
score editing never triggers an analysis pass. Interactive preview adjustments
schedule analysis automatically after a pause.
