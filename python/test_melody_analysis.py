from copy import deepcopy
import json
import unittest
import worker
import melody_analysis as analysis
from musical_patterns import signature
from test_worker import event, request, PPQ, HAVE_MUSIC21


def melody(pitches, lengths=None):
    # Fixture pitches are sounding MIDI; explicit spellings are tested separately.
    spellings = ((0, 0), (0, 1), (1, 0), (2, -1), (2, 0), (3, 0), (3, 1), (4, 0), (5, -1), (5, 0), (6, -1), (6, 0))
    lengths = lengths or [PPQ] * len(pitches)
    events = []
    for p, length in zip(pitches, lengths):
        step, alter = spellings[p % 12] if p is not None else (0, 0)
        events.append(event(length, [] if p is None else [[step, p // 12 - 1, alter]]))
    r = request(events)
    r.update(command="analyze_melody", analysisSchema=analysis.SCHEMA, offset="0",
             notation=[dict(sourceId=str(i + 1), written=e["ticks"], clippedStart=False, clippedEnd=False) for i, e in enumerate(events)])
    total = sum(lengths)
    r["measures"] = [dict(start=str(t), ticks=str(4 * PPQ), sourceMeasure=i + 1, numerator=4, denominator=4, groups=[1, 1, 1, 1]) for i, t in enumerate(range(0, total, 4 * PPQ))]
    return r


@unittest.skipUnless(HAVE_MUSIC21, "music21 required")
class MelodyAnalysisTests(unittest.TestCase):
    def run_analysis(self, r):
        return analysis.analyze(r, worker)["analysis"]

    def test_pitch_rhythm_and_repeat(self):
        a = self.run_analysis(melody([60, 62, 67, None, 67, 69, 74, None]))
        self.assertEqual(a["features"]["noteCount"], 6)
        self.assertEqual(a["features"]["rangeSemitones"], 14)
        self.assertEqual(a["features"]["silenceProportion"], 0.25)
        transposed = [f for f in a["findings"] if f["kind"] == "motif" and f["evidence"]["matchType"] == "transposed"]
        self.assertTrue(any(f["evidence"]["intervalPattern"] == [2, 5] and f["evidence"]["occurrences"][1]["transposition"] == 7 for f in transposed))
        self.assertTrue(any(f["kind"] == "boundary" and f["evidence"]["tick"] == str(4 * PPQ) for f in a["findings"]))

    def test_ties_and_partial_notes(self):
        r = melody([60, 60, 62, 60])
        r["events"][0]["ties"] = [True]
        r["notation"][0]["clippedStart"] = True
        a = self.run_analysis(r)
        self.assertEqual(a["features"]["noteCount"], 3)
        self.assertEqual(a["features"]["attackCount"], 2)
        self.assertEqual(a["notes"][0]["sources"], [0, 1])
        self.assertEqual(a["notes"][0]["end"], str(2 * PPQ))
        self.assertTrue(any("clips" in n for n in a["notices"]))

    def test_rest_and_single_note(self):
        a = self.run_analysis(melody([None, None]))
        self.assertEqual(a["features"]["noteCount"], 0)
        self.assertEqual(a["features"]["restCount"], 1)
        self.assertFalse(a["tonalRegions"])
        b = self.run_analysis(melody([60]))
        self.assertFalse(any(f["kind"] == "motif" for f in b["findings"]))
        self.assertIn("too few", " ".join(b["overview"]).lower())

    def test_chord_rejection_and_map_validation(self):
        r = melody([60, 62])
        r["events"][1]["pitches"].append([2, 4, 0])
        r["events"][1]["ties"].append(False)
        with self.assertRaisesRegex(ValueError, "stacked notes at m1"):
            self.run_analysis(r)
        r = melody([60, 62])
        r["measures"][0]["groups"] = [3]
        with self.assertRaisesRegex(ValueError, "grouping"):
            self.run_analysis(r)

    def test_meter_changes_and_exact_tuplet_positions(self):
        r = melody([60] * 8, [PPQ // 3] * 3 + [PPQ // 2] * 5)
        r["offset"] = str(PPQ)
        r["measures"] = [dict(start="0", ticks=str(2 * PPQ), sourceMeasure=7, numerator=2, denominator=4, groups=[1, 1]),
                         dict(start=str(2 * PPQ), ticks=str(5 * PPQ // 2), sourceMeasure=8, numerator=5, denominator=8, groups=[2, 3])]
        a = self.run_analysis(r)
        self.assertEqual(a["notes"][0]["location"], "m7 beat 2")
        self.assertEqual(a["notes"][1]["location"], "m7 beat 7/3")
        self.assertEqual(a["notes"][3]["location"], "m8 beat 1")
        self.assertEqual(a["notes"][5]["location"], "m8 beat 2")
        self.assertGreater(a["notes"][5]["metricStrength"], a["notes"][4]["metricStrength"])

    def test_tonal_collections_and_ambiguity(self):
        for scale, steps in analysis.SCALES.items():
            with self.subTest(scale=scale):
                # Strong tonic duration/ending makes modal tonic evidence explicit.
                ps = [60] + [60 + v for v in steps[1:]] + [72, 60]
                a = self.run_analysis(melody(ps, [4 * PPQ] + [PPQ] * (len(ps) - 2) + [4 * PPQ]))
                candidates = a["tonalRegions"][0]["candidates"]
                self.assertTrue(any(c["tonic"] == 0 and c["scale"] == scale and c["fit"] == 1.0 for c in candidates))
        a = self.run_analysis(melody([60, 67]))
        self.assertTrue(any("Too few distinct" in f["text"] for f in a["findings"]))

    def test_determinism_and_variable_lengths(self):
        r = melody([60, 62, 64, 65, 67, 69, 71, None] * 3)
        first = self.run_analysis(r)
        self.assertEqual(json.dumps(first, sort_keys=True), json.dumps(self.run_analysis(deepcopy(r)), sort_keys=True))
        motifs = [f for f in first["findings"] if f["kind"] == "motif"]
        self.assertTrue(any(f["evidence"]["noteCount"] >= 7 for f in motifs))
        self.assertTrue(all(0 <= f["score"] <= 1 for f in first["findings"]))

    def test_uniform_material_is_weak(self):
        a = self.run_analysis(melody([60] * 40))
        motifs = [f for f in a["findings"] if f["kind"] == "motif"]
        self.assertTrue(any(f["kind"] == "contour" for f in a["findings"]))
        self.assertTrue(all(f["score"] < 0.5 for f in motifs))
        self.assertTrue(any(f["kind"] == "cell" and f["evidence"]["cellType"] == "repeated-note cell" for f in a["findings"]))

    def test_ordinary_scale_is_cells_not_motifs(self):
        a = self.run_analysis(melody([60, 62, 64, 65, 67, 69, 71, 72]))
        structures = [f for f in a["findings"] if f["kind"] in ("cell", "gesture", "motif")]
        self.assertTrue(structures)
        self.assertTrue(all(f["kind"] == "cell" for f in structures))
        self.assertTrue(all("recurrence alone" in " ".join(f["evidence"]["limitations"]) for f in structures))

    def test_abstract_return_family_needs_more_motif_evidence(self):
        a = self.run_analysis(melody([69, 71, 69, None, 64, 69, 64, None]))
        returns = [f for f in a["findings"] if f["kind"] == "gesture" and "departure-return" in f["evidence"]["descriptors"]]
        self.assertEqual(len(returns), 1)
        f = returns[0]
        self.assertEqual(len(f["ranges"]), 2)
        self.assertEqual(f["evidence"]["pattern"]["pitchEquality"], [0, 1, 0])
        self.assertEqual(f["evidence"]["pattern"]["contour"], [1, -1])
        self.assertEqual(f["evidence"]["pattern"]["returns"], [True, False, True])
        self.assertNotIn("chromaticIntervals", f["evidence"]["matchedDimensions"])
        self.assertNotIn("diatonicIntervals", f["evidence"]["matchedDimensions"])
        self.assertFalse(any(f["kind"] == "motif" for f in a["findings"]))

    def test_short_motif_and_one_off_gesture_have_independent_salience(self):
        repeated = self.run_analysis(melody([60, 67, None, 65, 72, None]))
        motifs = [f for f in repeated["findings"] if f["kind"] == "motif"]
        self.assertTrue(any(f["evidence"]["noteCount"] == 2 for f in motifs))
        single = self.run_analysis(melody([60, 67, 60, None]))
        gesture = next(f for f in single["findings"] if f["kind"] == "gesture")
        self.assertIn("leap-return", gesture["evidence"]["descriptors"])
        self.assertFalse(any(f["kind"] == "motif" for f in single["findings"]))
        ordinary = self.run_analysis(melody([60, 62, 64, 65, 67, 69, 71, 72]))
        cell = next(f for f in ordinary["findings"] if f["kind"] == "cell")
        self.assertEqual(cell["score"], gesture["score"])
        self.assertGreater(gesture["salience"], cell["salience"])
        self.assertTrue(gesture["salienceReasons"])

    def test_abstract_motif_requires_additional_treatment(self):
        pitches = [60, 62, 60, None, 64, 69, 64, None, 65, 72, 65, None]
        durations = [PPQ // 2, PPQ // 2, 2 * PPQ, PPQ] * 3
        two = self.run_analysis(melody(pitches[:8], durations[:8]))
        three = self.run_analysis(melody(pitches, durations))
        def abstract_motifs(result):
            return [f for f in result["findings"] if f["kind"] == "motif" and f["evidence"]["matchType"] == "abstract gesture"]
        self.assertFalse(abstract_motifs(two))
        self.assertTrue(abstract_motifs(three))
        self.assertTrue(any("three times" in s for f in abstract_motifs(three) for s in f["evidence"]["support"]))

    def test_signature_dimensions_spelling_octaves_and_internal_rests(self):
        def sig(pitches, lengths=None, gaps=None):
            r = melody(pitches, lengths)
            ns = [dict(midi=worker.midi(e["pitches"][0]), pitch=e["pitches"][0], length=int(e["ticks"])) for e in r["events"]]
            return signature(ns, gaps or [0] * (len(ns) - 1))
        home = sig([60, 64, 60])
        arch = sig([60, 64, 62])
        octave = sig([60, 64, 72])
        self.assertEqual(home["contour"], arch["contour"])
        self.assertNotEqual(home["pitchEquality"], arch["pitchEquality"])
        self.assertFalse(octave["returns"][-1])
        self.assertEqual(sig([60, 60, 62])["repeatedNotes"], [False, True, False])
        first = sig([60, 64, 60], [PPQ, PPQ, 2 * PPQ], [PPQ // 2, 0])
        scaled = sig([67, 71, 67], [2 * PPQ, 2 * PPQ, 4 * PPQ], [PPQ, 0])
        self.assertEqual(first["rhythm"], scaled["rhythm"])
        self.assertNotEqual(first["rhythm"], sig([60, 64, 60], [PPQ, PPQ, 2 * PPQ])["rhythm"])
        ns = [dict(midi=60,pitch=[0,4,0],length=PPQ), dict(midi=61,pitch=[0,4,1],length=PPQ)]
        sharp = signature(ns, [0])
        ns[1]["pitch"] = [1,4,-1]
        flat = signature(ns, [0])
        self.assertEqual(sharp["chromaticIntervals"], flat["chromaticIntervals"])
        self.assertNotEqual(sharp["diatonicIntervals"], flat["diatonicIntervals"])

    def test_clipped_edges_do_not_supply_grouping(self):
        r = melody([60, 67, 60, None])
        r["notation"][0]["clippedStart"] = True
        r["notation"][2]["clippedEnd"] = True
        a = self.run_analysis(r)
        gesture = next(f for f in a["findings"] if f["kind"] == "gesture")
        self.assertEqual(gesture["evidence"]["salienceComponents"]["grouping"], 0)
        self.assertFalse(any("begins" in reason or "ends" in reason for reason in gesture["salienceReasons"]))

    def test_structural_scores_are_bounded_and_explained(self):
        a = self.run_analysis(melody([60, 60, 67, 60, None, 65, 65, 72, 65, None]))
        for f in a["findings"]:
            if f["kind"] not in ("cell", "gesture", "motif"):
                continue
            c = f["evidence"]["salienceComponents"]
            self.assertAlmostEqual(f["salience"], .35*c["grouping"] + .35*c["emphasis"] + .30*c["recurrence"], places=6)
            self.assertTrue(0 <= f["salience"] <= 1)
            self.assertLessEqual(f["evidence"]["signatureNoteCount"], 12)
            self.assertTrue(f["evidence"]["evidenceClaim"])

    def test_variants_inversion_retrograde_and_scaling(self):
        cases = [
            ([60, 62, 67, None, 60, 62, 64, 67], [PPQ]*3+[2*PPQ]+[PPQ]*4, "approximate variant"),
            ([60, 62, 67, None, 60, 58, 53], [PPQ]*3+[2*PPQ]+[PPQ]*3, "interval inversion"),
            ([60, 62, 67, None, 67, 62, 60], [PPQ]*3+[2*PPQ]+[PPQ]*3, "retrograde"),
            ([60, 62, 67, None, 67, 69, 74], [PPQ, PPQ, 2*PPQ, 2*PPQ, 2*PPQ, 2*PPQ, 4*PPQ], "rhythmic scaling"),
        ]
        for pitches, lengths, kind in cases:
            with self.subTest(kind=kind):
                a = self.run_analysis(melody(pitches, lengths))
                self.assertTrue(any(r["kind"] == kind for r in a["relations"]))

    def test_large_repetitive_passage_keeps_all_categories(self):
        a = self.run_analysis(melody([60, 62, 67, None] * 450))
        kinds = {f["kind"] for f in a["findings"]}
        self.assertTrue({"motif", "tonality", "boundary", "phrase", "rest"} <= kinds)
        self.assertLessEqual(len(a["findings"]), analysis.MAX_FINDINGS)
        self.assertEqual(len(a["notes"]), 1350)
        self.assertTrue(a["notices"])

    def test_suffix_repeats_match_their_tokens(self):
        for tokens in [(2, 1, 2, 1, 2), (5, 6, 1, 5, 6, 1, 5), (2, 2, 3, 2, 2, 3, 2, 2)]:
            for a, b, length in analysis.suffix_repeats(tokens):
                self.assertEqual(tokens[a:a+length], tokens[b:b+length])


if __name__ == "__main__":
    unittest.main()
