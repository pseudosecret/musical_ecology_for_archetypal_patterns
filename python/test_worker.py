"""Exact-math tests use symbolic doubles; integration tests require real music21."""
from contextlib import contextmanager
from copy import deepcopy
import importlib.util
import io
import json
import subprocess
import sys
import types
import unittest
from unittest.mock import patch
from pathlib import Path
import worker

HAVE_MUSIC21 = importlib.util.find_spec("music21") is not None
PPQ = worker.PPQ


class PitchDouble:
    def __init__(self):
        self.step, self.octave, self._accidental = "C", 4, 0

    @property
    def accidental(self):
        return types.SimpleNamespace(alter=self._accidental)

    @accidental.setter
    def accidental(self, value):
        self._accidental = value

    @property
    def ps(self):
        return (self.octave + 1) * 12 + dict(zip("CDEFGAB", (0, 2, 4, 5, 7, 9, 11)))[self.step] + self._accidental


class NoteDouble:
    isRest, isChord = False, False

    def __init__(self, pitch):
        self.pitch = pitch
        self.duration = types.SimpleNamespace(quarterLength=0)

    @property
    def pitches(self):
        return [self.pitch]


class ChordDouble:
    isRest, isChord = False, True

    def __init__(self, pitches):
        self.pitches = pitches
        self.duration = types.SimpleNamespace(quarterLength=0)


class RestDouble:
    isRest, isChord = True, False

    def __init__(self):
        self.duration = types.SimpleNamespace(quarterLength=0)


class StreamDouble:
    def insert(self, at, obj):
        pass


@contextmanager
def symbolic_doubles():
    module = types.SimpleNamespace(
        pitch=types.SimpleNamespace(Pitch=PitchDouble),
        note=types.SimpleNamespace(Note=NoteDouble, Rest=RestDouble),
        chord=types.SimpleNamespace(Chord=ChordDouble),
        stream=types.SimpleNamespace(Stream=StreamDouble),
    )
    with patch.dict(sys.modules, {"music21": module}):
        yield


def event(ticks=PPQ, pitches=None, ties=None):
    pitches = [[0, 4, 0]] if pitches is None else pitches
    return dict(ticks=str(ticks), pitches=pitches, ties=ties if ties is not None else [False] * len(pitches), actual=1, normal=1, unit="0", group="0")


def request(events=None, steps=None):
    events = [event()] if events is None else events
    return dict(version=2, id="42", command="transform", ppq=PPQ, duration=str(sum(int(e["ticks"]) for e in events)), events=events,
                keys=[dict(tick="0", fifths=0, tonality=worker.signature(0))],
                steps=steps or [dict(kind="transpose", amount=0)])


def midis(result):
    values = []
    for e in result["events"]:
        values.append([(octave + 1) * 12 + (0, 2, 4, 5, 7, 9, 11)[step] + alter for step, octave, alter in e["pitches"]])
    return values


class ExactTransformationTests(unittest.TestCase):
    def setUp(self):
        self.symbols = symbolic_doubles()
        self.symbols.__enter__()
        self.addCleanup(self.symbols.__exit__, None, None, None)

    def test_chromatic_transpose_and_inversion(self):
        passage = [event(pitches=[[0, 4, 0], [2, 4, 0], [4, 4, 0]])]
        result = worker.transform(request(passage, [dict(kind="transpose", amount=2)]))
        self.assertEqual(midis(result), [[62, 66, 69]])
        result = worker.transform(request(passage, [dict(kind="invert", amount=60)]))
        self.assertEqual(midis(result), [[60, 56, 53]])

    def test_diatonic_and_chromatic_triads(self):
        source = [event(pitches=[[0, 4, 0], [2, 4, 0], [4, 4, 0]])]
        result = worker.transform(request(source, [dict(kind="transpose", mode="degrees", amount=1)]))
        self.assertEqual(result["events"][0]["pitches"], [[1, 4, 0], [3, 4, 0], [5, 4, 0]])
        result = worker.transform(request([event(pitches=[[2, 4, 0]])], [dict(kind="invert", mode="degrees", axisDegree=1, tonicOctave=4)]))
        self.assertEqual(result["events"][0]["pitches"], [[5, 3, 0]])

    def test_minor_forms_in_both_directions(self):
        for form, expected in (("melodic", [66, 68]), ("harmonic", [65, 68]), ("aeolian", [65, 67])):
            for amount in (1, -6):
                r = request([event(pitches=[[2, 4, 0], [3, 4, 0]])], [dict(kind="transpose", mode="degrees", amount=amount, accidentals="scale")])
                r["keys"] = [dict(tick="0", fifths=0, tonality=worker.signature(0, True, form))]
                self.assertEqual(midis(worker.transform(r)), [[n - (12 if amount == -6 else 0) for n in expected]])

    def test_inflections_reflect_and_strict_collisions_fail(self):
        r = request([event(pitches=[[1, 4, 1]])], [dict(kind="invert", mode="degrees", tonicOctave=4)])
        self.assertEqual(worker.transform(r)["events"][0]["pitches"], [[6, 3, -1]])
        r = request([event(pitches=[[0, 4, 0], [0, 4, 1]])], [dict(kind="transpose", mode="degrees", amount=0, accidentals="scale")])
        with self.assertRaisesRegex(ValueError, "collapses"):
            worker.transform(r)

    def test_key_regions_retrograde_remap_reverse_and_involution(self):
        r = request([event(pitches=[[0, 4, 0]]), event(pitches=[[4, 4, 0]])], [dict(kind="retrograde")])
        r["keys"].append(dict(tick=str(PPQ), fifths=1, tonality=worker.signature(1)))
        result = worker.transform(r)
        self.assertEqual(midis(result), [[72], [55]])
        self.assertEqual(result["keys"], r["keys"])
        r["steps"] *= 2
        self.assertEqual(worker.transform(r)["events"], r["events"])
        r["steps"] = [dict(kind="retrograde", reverseKeys=True)]
        result = worker.transform(r)
        self.assertEqual(midis(result), [[67], [60]])
        self.assertEqual([k["fifths"] for k in result["keys"]], [1, 0])

    def test_ties_keep_onset_key_and_key_ticks_scale_exactly(self):
        r = request([event(ties=[True]), event()], [dict(kind="transpose", mode="degrees", amount=3)])
        r["keys"].append(dict(tick=str(PPQ), fifths=1, tonality=worker.signature(1)))
        self.assertEqual(midis(worker.transform(r)), [[65], [65]])
        r["steps"] = [dict(kind="retrograde"), dict(kind="retrograde")]
        self.assertEqual(worker.transform(r)["events"], r["events"])
        r["steps"] = [dict(kind="scale", numerator=3, denominator=2)]
        self.assertEqual(worker.transform(r)["keys"][1]["tick"], str(3 * PPQ // 2))
        r["keys"][1]["tick"] = "1"
        with self.assertRaisesRegex(ValueError, "key change"):
            worker.transform(r)

    def test_rejects_unknown_and_invalid_key_context(self):
        r = request(steps=[dict(kind="transpose", amount=1, mode="degrees")])
        r["keys"][0]["tonality"] = None
        with self.assertRaisesRegex(ValueError, "Confirm a key"):
            worker.transform(r)
        r["steps"][0]["mode"] = "semitones"
        self.assertEqual(midis(worker.transform(r)), [[61]])
        r["keys"][0]["tick"] = "1"
        with self.assertRaisesRegex(ValueError, "start at zero"):
            worker.transform(r)

    def test_local_inversion_axes_and_unequal_reversed_regions(self):
        r = request([event(pitches=[[2, 4, 0]]), event(pitches=[[6, 4, 0]])], [dict(kind="invert", mode="degrees", axisDegree=1, tonicOctave=4)])
        r["keys"].append(dict(tick=str(PPQ), fifths=1, tonality=worker.signature(1)))
        self.assertEqual(midis(worker.transform(r)), [[57], [64]])
        r = request([event(), event(2 * PPQ, [[2, 4, 0]]), event(pitches=[[4, 4, 0]])], [dict(kind="retrograde", reverseKeys=True)])
        r["keys"].append(dict(tick=str(3 * PPQ), fifths=1, tonality=worker.signature(1)))
        result = worker.transform(r)
        self.assertEqual([(k["tick"], k["fifths"]) for k in result["keys"]], [("0", 1), (str(PPQ), 0)])
        r["steps"] *= 2
        self.assertEqual(worker.transform(r)["events"], r["events"])

    def test_order_matters(self):
        transpose, invert = dict(kind="transpose", amount=2), dict(kind="invert", amount=60)
        self.assertEqual(midis(worker.transform(request(steps=[transpose, invert]))), [[58]])
        self.assertEqual(midis(worker.transform(request(steps=[invert, transpose]))), [[62]])

    def test_retrograde_reverses_partial_chord_ties_and_rests(self):
        events = [event(ties=[True]), event(PPQ // 2, [[0, 4, 0], [2, 4, 0]]), event(PPQ // 2, [])]
        result = worker.transform(request(events, [dict(kind="retrograde")]))
        self.assertEqual([e["ticks"] for e in result["events"]], [str(PPQ // 2), str(PPQ // 2), str(PPQ)])
        self.assertEqual(midis(result), [[], [60, 64], [60]])
        self.assertEqual([e["ties"] for e in result["events"]], [[], [True, False], [False]])
        result = worker.transform(request(events, [dict(kind="retrograde"), dict(kind="retrograde")]))
        self.assertEqual(result["events"], events)

    def test_rhythm_is_exact_and_does_not_scale_alignment(self):
        result = worker.transform(request(steps=[dict(kind="scale", numerator=3, denominator=2)]))
        self.assertEqual(result["duration"], str(3 * PPQ // 2))
        result = worker.transform(request(steps=[dict(kind="scale", numerator=1, denominator=3), dict(kind="scale", numerator=3, denominator=1)]))
        self.assertEqual(result["events"], [event()])
        result = worker.transform(request(steps=[dict(kind="scale", numerator=1, denominator=3)]))
        self.assertEqual(result["events"][0]["ticks"], str(PPQ // 3))
        with self.assertRaisesRegex(ValueError, "time grid"):
            worker.transform(request(steps=[dict(kind="scale", numerator=1, denominator=11)]))

    def test_unchanged_spelling_and_tuplet_hints_survive(self):
        original = event(PPQ // 3, [[1, 4, -1]])
        original.update(actual=3, normal=2, unit=str(PPQ // 2), group="123")
        result = worker.transform(request([original]))
        self.assertEqual(result["events"], [original])
        result = worker.transform(request([original], [dict(kind="scale", numerator=2, denominator=1)]))
        self.assertEqual(result["events"][0]["group"], "123")
        self.assertEqual(result["events"][0]["unit"], str(PPQ))

    def test_large_scale_stays_exact_for_cpp_tied_spelling(self):
        result = worker.transform(request([event(4 * PPQ)], [dict(kind="scale", numerator=3, denominator=1)]))
        self.assertEqual(result["events"][0]["ticks"], str(12 * PPQ))

    def test_invalid_operations_and_pitch_limits(self):
        for steps in ([dict(kind="transpose", amount=80)], [dict(kind="scale", numerator=0, denominator=1)], [dict(kind="unknown")]):
            with self.assertRaises(ValueError):
                worker.transform(request(steps=steps))
        with self.assertRaises(ValueError):
            worker.transform(request([event(pitches=[[0, -1, -1]])]))

    def test_source_snapshot_is_not_mutated(self):
        source = request(steps=[dict(kind="transpose", amount=3), dict(kind="retrograde")])
        before = deepcopy(source)
        worker.transform(source)
        self.assertEqual(source, before)


@unittest.skipUnless(HAVE_MUSIC21, "real music21 is not installed")
class Music21IntegrationTests(unittest.TestCase):
    def test_key_suggestions_respect_signature_and_handle_rests(self):
        payload = request([event(pitches=[[0, 4, 0], [2, 4, 0], [4, 4, 0]])])
        payload["command"] = "analyze_keys"
        payload["keys"][0]["tonality"] = None
        before = deepcopy(payload)
        candidates = worker.analyze_keys(payload)["regions"][0]["candidates"]
        self.assertEqual(payload, before)
        self.assertEqual({c["form"] for c in candidates}, {0, 1})
        self.assertTrue(all(c["fifths"] == 0 for c in candidates))
        payload["keys"][0]["fifths"] = None
        candidates = worker.analyze_keys(payload)["regions"][0]["candidates"]
        self.assertEqual(len(candidates), 3)
        self.assertEqual(candidates[0]["fifths"], 0)
        self.assertEqual(candidates[0]["form"], 0)
        payload["events"][0]["pitches"], payload["events"][0]["ties"] = [], []
        self.assertEqual(worker.analyze_keys(payload)["regions"][0]["candidates"], [])

    def test_real_symbolic_pitch_chord_rest_and_ties(self):
        source = [event(ties=[True]), event(PPQ // 2, [[0, 4, 0], [2, 4, 0]]), event(PPQ // 2, [])]
        result = worker.transform(request(source, [dict(kind="transpose", amount=2), dict(kind="retrograde")]))
        self.assertEqual(midis(result), [[], [62, 66], [62]])
        self.assertEqual(result["events"][1]["ties"], [True, False])
        result = worker.transform(request([event(pitches=[[0, 4, 0], [2, 4, 0], [4, 4, 0]])], [dict(kind="invert", amount=60)]))
        self.assertEqual(midis(result), [[60, 56, 53]])

    def test_real_process_round_trip(self):
        payload = request(steps=[dict(kind="scale", numerator=3, denominator=2)])
        process = subprocess.run([sys.executable, str(Path(worker.__file__))], input=json.dumps(payload)+"\n", text=True, capture_output=True, timeout=30, check=True)
        result = json.loads(process.stdout)
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["duration"], str(3 * PPQ // 2))


if __name__ == "__main__":
    unittest.main()
