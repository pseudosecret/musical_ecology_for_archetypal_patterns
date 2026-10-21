"""Deterministic monophonic diagnostics. No score mutations or stochastic inference.

All positions are exact ticks relative to the excerpt. Evidence scores are
heuristics, not probabilities. Limits and algorithm parameters travel with results.
"""
from bisect import bisect_right
from collections import Counter, defaultdict
from fractions import Fraction
from functools import reduce
from math import gcd, log2
from musical_patterns import discover_gestures, describe_family, independent_occurrences, MAX_WINDOW, SALIENCE_WEIGHTS

SCHEMA = 2
ALGORITHM = "melody-v2"
MAX_NOTES = 20000
MAX_FINDINGS = 1200
MAX_FAMILIES = 120
MAX_OCCURRENCES = 64
MAX_COMPARISONS = 4000
SCALES = {
    "major / Ionian": (0, 2, 4, 5, 7, 9, 11),
    "Dorian": (0, 2, 3, 5, 7, 9, 10),
    "Phrygian": (0, 1, 3, 5, 7, 8, 10),
    "Lydian": (0, 2, 4, 6, 7, 9, 11),
    "Mixolydian": (0, 2, 4, 5, 7, 9, 10),
    "natural minor / Aeolian": (0, 2, 3, 5, 7, 8, 10),
    "Locrian": (0, 1, 3, 5, 6, 8, 10),
    "harmonic minor": (0, 2, 3, 5, 7, 8, 11),
    "melodic minor (fixed raised 6/7)": (0, 2, 3, 5, 7, 9, 11),
    "major pentatonic": (0, 2, 4, 7, 9),
    "minor pentatonic": (0, 3, 5, 7, 10),
}
PC_NAMES = ("C", "C♯/D♭", "D", "E♭/D♯", "E", "F", "F♯/G♭", "G", "A♭/G♯", "A", "B♭/A♯", "B")


def ratio(a, b):
    return round(a / b, 6) if b else 0.0


def signed(value):
    return f"{value:+d}"


def pitch_name(p):
    return "CDEFGAB"[p[0]] + {0: "", 1: "♯", -1: "♭", 2: "𝄪", -2: "𝄫"}[p[2]] + str(p[1])


def fraction_text(n, d):
    return str(Fraction(n, d))


def suffix_repeats(tokens):
    """Adjacent suffix LCPs expose variable-length repeat seeds, without all substrings."""
    n = len(tokens)
    if n < 2:
        return []
    symbols = {v: i for i, v in enumerate(sorted(set(tokens)))}
    rank = [symbols[v] for v in tokens]
    order = list(range(n))
    width = 1
    while width < n:
        order.sort(key=lambda i: (rank[i], rank[i + width] if i + width < n else -1))
        new = [0] * n
        for a, b in zip(order, order[1:]):
            new[b] = new[a] + ((rank[a], rank[a + width] if a + width < n else -1) !=
                               (rank[b], rank[b + width] if b + width < n else -1))
        rank = new
        if rank[order[-1]] == n - 1:
            break
        width *= 2
    position = [0] * n
    for i, start in enumerate(order):
        position[start] = i
    matches, length = [], 0
    for a in range(n):
        at = position[a]
        if at == 0:
            length = 0
            continue
        b = order[at - 1]
        while a + length < n and b + length < n and tokens[a + length] == tokens[b + length]:
            length += 1
        if length:
            # Overlapping occurrences are not independent evidence for a motif.
            count = min(length, abs(a - b))
            if count:
                matches.append((min(a, b), max(a, b), count))
            length = max(0, length - 1)
    return matches


def edit_similarity(a, b):
    if not a and not b:
        return 1.0
    row = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        following = [i]
        for j, y in enumerate(b, 1):
            following.append(min(following[-1] + 1, row[j] + 1, row[j - 1] + (x != y)))
        row = following
    return round(1 - row[-1] / max(len(a), len(b), 1), 6)


def tonal_candidates(notes):
    if not notes:
        return []
    histogram = Counter()
    accents = Counter()
    endings = Counter()
    for i, n in enumerate(notes):
        pc = n["midi"] % 12
        histogram[pc] += n["length"]
        accents[pc] += n["strength"]
        if not n["clippedEnd"] and (i == len(notes) - 1 or notes[i + 1]["at"] > n["at"] + n["length"]):
            endings[pc] += 1
    total = sum(histogram.values())
    result = []
    for tonic in range(12):
        emphasis = (0.45 * ratio(histogram[tonic], total) +
                    0.25 * ratio(accents[tonic], sum(accents.values())) +
                    0.30 * ratio(endings[tonic], sum(endings.values())))
        for name, collection in SCALES.items():
            pcs = {(tonic + step) % 12 for step in collection}
            fit = ratio(sum(v for p, v in histogram.items() if p in pcs), total)
            coverage = ratio(len(set(histogram) & pcs), len(pcs))
            # Coverage stops a seven-tone superset automatically beating a fully
            # articulated pentatonic collection. Tonic evidence stays independent.
            score = round(0.65 * fit + 0.20 * emphasis + 0.15 * coverage, 6)
            result.append(dict(tonic=tonic, scale=name, score=score, fit=fit,
                               intervals=list(collection),
                               tonicEmphasis=round(emphasis, 6), degreeCoverage=coverage,
                               observedDegrees=[i + 1 for i, step in enumerate(collection) if (tonic + step) % 12 in histogram],
                               absentDegrees=[i + 1 for i, step in enumerate(collection) if (tonic + step) % 12 not in histogram],
                               outsidePitchClasses=sorted(set(histogram) - pcs)))
    result.sort(key=lambda c: (-c["score"], -c["tonicEmphasis"], c["tonic"], c["scale"]))
    return result[:6]


def analyze(request, bridge):
    from music21 import meter
    events, keys, duration_ql = bridge.passage(request)
    if request.get("command") != "analyze_melody" or request.get("analysisSchema") != SCHEMA:
        raise ValueError("Unsupported melody-analysis command or schema.")
    duration = int(duration_ql * bridge.PPQ)
    offset = bridge.ticks(request["offset"])
    raw_measures = request["measures"]
    refs = request["notation"]
    if not isinstance(raw_measures, list) or not 0 < len(raw_measures) <= MAX_NOTES:
        raise ValueError("Invalid measure map.")
    if not isinstance(refs, list) or len(refs) != len(events):
        raise ValueError("Invalid source-note references.")
    measures, starts, metric_cache = [], [], {}
    end = 0
    for m in raw_measures:
        start = bridge.ticks(m["start"])
        length = bridge.ticks(m["ticks"], True)
        num, den = bridge.integer(m["numerator"], 1, 32), bridge.integer(m["denominator"], 1, 64)
        groups = m["groups"]
        if (start != end or den not in (1, 2, 4, 8, 16, 32, 64) or
                not isinstance(groups, list) or not groups or
                any(type(g) is not int or g <= 0 for g in groups) or sum(groups) != num or
                length * den != bridge.PPQ * 4 * num):
            raise ValueError("Invalid meter grouping or measure coverage.")
        source_measure = bridge.integer(m["sourceMeasure"], 1, 1_000_000)
        signature = (num, den, tuple(groups))
        if signature not in metric_cache:
            ts = meter.TimeSignature(f"{num}/{den}")
            if [int(b.duration.quarterLength * den / 4) for b in ts.beatSequence] != groups:
                ts = meter.TimeSignature("+".join(f"{g}/{den}" for g in groups))
            metric_cache[signature] = ts
        measures.append(dict(start=start, length=length, numerator=num, denominator=den,
                             groups=groups, source=source_measure, meter=metric_cache[signature]))
        starts.append(start)
        end = start + length
    if offset >= measures[0]["length"] or offset + duration > end or offset + duration <= starts[-1]:
        raise ValueError("Measure map does not cover the excerpt exactly.")

    def position(at):
        m = measures[bisect_right(starts, at + offset) - 1]
        within = at + offset - m["start"]
        unit = bridge.PPQ * 4 // m["denominator"]
        units = Fraction(within, unit)
        passed = 0
        for i, group in enumerate(m["groups"]):
            if units < passed + group:
                beat = Fraction(i + 1) + (units - passed) / group
                break
            passed += group
        else:
            beat = Fraction(len(m["groups"]) + 1)
        strength = float(m["meter"].getAccentWeight(Fraction(within, bridge.PPQ), forcePositionMatch=True))
        return m, f"m{m['source']} beat {beat}", round(strength, 6)

    notes, rests, at, continuing = [], [], 0, False
    for i, (e, ref) in enumerate(zip(events, refs)):
        length = bridge.ticks(e["ticks"], True)
        if (not isinstance(ref, dict) or not isinstance(ref.get("sourceId"), str) or
                not ref["sourceId"].isascii() or not ref["sourceId"].isdecimal() or
                type(ref.get("clippedStart")) is not bool or type(ref.get("clippedEnd")) is not bool):
            raise ValueError("Invalid notation reference.")
        written = bridge.ticks(ref["written"], True)
        m, location, strength = position(at)
        if len(e["pitches"]) > 1:
            raise ValueError(f"Analysis requires one monophonic melody: stacked notes at {location}.")
        if not e["pitches"]:
            if rests and rests[-1]["at"] + rests[-1]["length"] == at:
                rests[-1]["length"] += length
                rests[-1]["sources"].append(i)
            else:
                rests.append(dict(at=at, length=length, sources=[i], location=location))
        elif continuing:
            notes[-1]["length"] += length
            notes[-1]["sources"].append(i)
            notes[-1]["writtenTicks"].append(str(written))
            notes[-1]["clippedEnd"] = ref["clippedEnd"]
        else:
            p = e["pitches"][0]
            notes.append(dict(id=f"n{len(notes)}", at=at, length=length, pitch=p,
                              midi=bridge.midi(p), name=pitch_name(p), sources=[i],
                              writtenTicks=[str(written)], location=location, strength=strength,
                              measure=m["source"], clippedStart=ref["clippedStart"], clippedEnd=ref["clippedEnd"]))
        continuing = bool(e["ties"] and e["ties"][0])
        at += length
    if len(notes) > MAX_NOTES:
        raise ValueError(f"Select at most {MAX_NOTES:,} sustained notes for melody analysis.")

    findings, relations, notices = [], [], []
    finding_counts = Counter()

    def finding(kind, title, text, spans, score=1.0, evidence=None):
        # Reserve room for every category, so many rests/boundaries cannot crowd
        # motif and tonal findings out of a long passage's report.
        limit = 200 if kind == "boundary" else 121 if kind == "tonality" else 120
        if finding_counts[kind] >= limit:
            notice = f"{kind.capitalize()} cards limited to {limit}; additional candidates may exist."
            if notice not in notices:
                notices.append(notice)
            return None
        if len(findings) >= MAX_FINDINGS:
            if "Finding display limited to 1,200 entries; feature measurements remain complete." not in notices:
                notices.append("Finding display limited to 1,200 entries; feature measurements remain complete.")
            return None
        result = dict(id=f"f{len(findings)}", kind=kind, title=title, text=text,
                      ranges=[dict(start=str(a), end=str(b)) for a, b in spans],
                      score=round(score, 6), evidence=evidence or {})
        findings.append(result)
        finding_counts[kind] += 1
        return result

    lengths = [n["length"] for n in notes]
    pitches = [n["midi"] for n in notes]
    intervals = [b["midi"] - a["midi"] for a, b in zip(notes, notes[1:])]
    diatonic = [(b["pitch"][1] - a["pitch"][1]) * 7 + b["pitch"][0] - a["pitch"][0] for a, b in zip(notes, notes[1:])]
    iois = [b["at"] - a["at"] for a, b in zip(notes, notes[1:])]
    gaps = [b["at"] - a["at"] - a["length"] for a, b in zip(notes, notes[1:])]
    contours = [(v > 0) - (v < 0) for v in intervals]
    sounding = sum(lengths)
    motion = Counter("repeat" if d == 0 else "step" if abs(d) == 1 else "leap" for d in diatonic)
    direction = Counter("rising" if d > 0 else "falling" if d < 0 else "repeated" for d in intervals)
    changes = [i for i in range(1, len(contours)) if not gaps[i] and not gaps[i - 1] and contours[i] and contours[i - 1] and contours[i] != contours[i - 1]]
    histogram = Counter()
    for n in notes:
        histogram[n["midi"] % 12] += n["length"]
    per_measure = []
    attacks_by_measure = Counter(n["measure"] for n in notes if not n["clippedStart"])
    for m in measures:
        span = min(offset + duration, m["start"] + m["length"]) - max(offset, m["start"])
        per_measure.append(dict(measure=m["source"], attacks=attacks_by_measure[m["source"]],
                                attacksPerQuarter=ratio(attacks_by_measure[m["source"]] * bridge.PPQ, span)))
    syncopations = []
    # Walk meter groups inside sustained notes rather than every fine-grid tick.
    for n in notes:
        if n["clippedStart"]:
            continue
        first = bisect_right(starts, n["at"] + offset) - 1
        crossing = None
        for measure_index in range(first, len(measures)):
            m = measures[measure_index]
            if m["start"] >= n["at"] + offset + n["length"]:
                break
            unit = bridge.PPQ * 4 // m["denominator"]
            for boundary in range(m["numerator"]):
                tick = m["start"] + boundary * unit - offset
                if n["at"] < tick < n["at"] + n["length"]:
                    _, _, weight = position(tick)
                    if weight > n["strength"]:
                        crossing = tick
                        break
            if crossing is not None:
                break
        if crossing is not None:
            syncopations.append(n["id"])
    features = dict(noteCount=len(notes), attackCount=sum(not n["clippedStart"] for n in notes),
                    restCount=len(rests), soundingTicks=str(sounding), silenceTicks=str(duration - sounding),
                    silenceProportion=ratio(duration - sounding, duration),
                    lowestMidi=min(pitches) if pitches else None, highestMidi=max(pitches) if pitches else None,
                    rangeSemitones=max(pitches) - min(pitches) if pitches else None,
                    weightedRegister=ratio(sum(n["midi"] * n["length"] for n in notes), sounding) if notes else None,
                    netMotion=intervals and sum(intervals) or 0,
                    motionCounts=dict(sorted(motion.items())), motionProportions={k: ratio(motion[k], len(intervals)) for k in ("repeat", "step", "leap")},
                    directionCounts=dict(sorted(direction.items())), directionChanges=len(changes),
                    intervalsAcrossRests=sum(g > 0 for g in gaps), uninterruptedIntervalCount=sum(g == 0 for g in gaps),
                    intervalDistribution={str(k): v for k, v in sorted(Counter(intervals).items())},
                    durationDistribution={str(k): v for k, v in sorted(Counter(lengths).items())},
                    pitchClassTicks={str(k): str(v) for k, v in sorted(histogram.items())},
                    offbeatAttacks=sum(n["strength"] <= 0.125 and not n["clippedStart"] for n in notes),
                    strongerPositionSustains=syncopations, density=per_measure)
    overview = [f"{len(notes)} sustained notes ({features['attackCount']} attacks), {len(rests)} rest groups; "
                f"{fraction_text(duration, bridge.PPQ)} quarter notes. Silence occupies {features['silenceProportion']:.1%} of the excerpt."]
    if notes:
        lo, hi = min(notes, key=lambda n: n["midi"]), max(notes, key=lambda n: n["midi"])
        overview.append(f"Register: {lo['name']}–{hi['name']} ({features['rangeSemitones']} semitones); duration-weighted MIDI register {features['weightedRegister']:.2f}.")
        overview.append(f"Motion: {direction['rising']} rises, {direction['falling']} falls, {direction['repeated']} repeated pitches; net {signed(features['netMotion'])} semitones. "
                        f"Written steps {motion['step']}, leaps {motion['leap']}, repeated staff positions {motion['repeat']}. Direction changes: {len(changes)}.")
        finding("register", "Range and registral extremes", overview[-2], [(lo["at"], lo["at"] + lo["length"]), (hi["at"], hi["at"] + hi["length"])], evidence=dict(low=lo["id"], high=hi["id"]))
    if len(notes) < 2:
        overview.append("There are too few notes to infer intervals, repeated motifs, or internal phrase boundaries.")
    overview.append("Durations (quarter notes × count): " + (", ".join(f"{fraction_text(t, bridge.PPQ)} × {c}" for t, c in sorted(Counter(lengths).items())) or "none") + ".")
    overview.append("Intervals (semitones × count): " + (", ".join(f"{signed(t)} × {c}" for t, c in sorted(Counter(intervals).items())) or "none") + ".")
    overview.append(f"{features['offbeatAttacks']} weak-subdivision attacks; {len(syncopations)} notes sustain across a stronger metric position. These are notated syncopation cues, not performed accents.")
    overview.append("Attack density per quarter: " + "; ".join(f"m{d['measure']}: {d['attacksPerQuarter']:g}" for d in per_measure[:120]) + (". Remaining measures are in structured data." if len(per_measure) > 120 else "."))
    for r in rests[:120]:
        finding("rest", f"Rest · {r['location']}", f"Silence for {fraction_text(r['length'], bridge.PPQ)} quarter notes.", [(r["at"], r["at"] + r["length"])], evidence=dict(durationTicks=str(r["length"])))
    if len(rests) > 120:
        notices.append("Rest cards limited to 120; structured data contains all rests.")
    if notes:
        run_start = 0
        while run_start < len(contours):
            if gaps[run_start]:
                run_start += 1
                continue
            end = run_start + 1
            while end < len(contours) and not gaps[end] and contours[end] == contours[run_start]:
                end += 1
            if end - run_start >= 2:
                a, b = notes[run_start], notes[end]
                text = "rising" if contours[run_start] > 0 else "falling" if contours[run_start] < 0 else "repeated-pitch"
                finding("contour", f"{text.capitalize()} run · {a['location']}", f"{end - run_start + 1} notes, {signed(b['midi'] - a['midi'])} semitones overall.", [(a["at"], b["at"] + b["length"])], evidence=dict(first=a["id"], last=b["id"]))
            run_start = end
        for i in changes[:120]:
            n = notes[i]
            label = "Peak" if contours[i - 1] > 0 else "Trough"
            finding("contour", f"{label} · {n['location']}", f"Direction changes at {n['name']}.", [(n["at"], n["at"] + n["length"])], evidence=dict(note=n["id"]))

    # LBDM-style local change/proximity, with independently inspectable components.
    def boundary_profile(values):
        raw = []
        for i, x in enumerate(values):
            change = sum(abs(x - values[j]) / (x + values[j]) if x + values[j] else 0 for j in (i - 1, i + 1) if 0 <= j < len(values))
            raw.append(x * change)
        maximum = max(raw, default=0)
        return [v / maximum if maximum else 0 for v in raw]

    profiles = [boundary_profile([abs(v) for v in intervals]), boundary_profile(iois), boundary_profile(gaps)]
    strengths = [round(0.25 * p + 0.5 * t + 0.25 * r, 6) for p, t, r in zip(*profiles)]
    boundaries, boundary_profile_data = [], []
    for i, strength in enumerate(strengths):
        a, b = notes[i:i + 2]
        # A long silence is useful evidence even when all rests have equal lengths.
        rest_evidence = min(1.0, gaps[i] / bridge.PPQ)
        ending = min(1.0, max(0, a["length"] / max(1, b["length"]) - 1) / 3)
        score = round(max(strength, 0.75 * rest_evidence + 0.15 * ending + 0.1 * b["strength"]), 6)
        boundary_profile_data.append(dict(tick=str(b["at"]), strength=score, pitchChange=round(profiles[0][i], 6),
                                          timingChange=round(profiles[1][i], 6), restChange=round(profiles[2][i], 6)))
        if score >= 0.35 and (gaps[i] or (strength >= (strengths[i - 1] if i else 0) and strength >= (strengths[i + 1] if i + 1 < len(strengths) else 0))):
            f = finding("boundary", f"{'Phrase' if score >= 0.60 else 'Small-unit'} boundary candidate · {b['location']}",
                        f"Before {b['name']}: pitch gap {abs(intervals[i])} semitones, onset interval {fraction_text(iois[i], bridge.PPQ)} quarter notes, silence {fraction_text(gaps[i], bridge.PPQ)} quarter notes. Strength {score:.2f}.",
                        [(b["at"], b["at"] + b["length"])], score,
                        dict(tick=str(b["at"]), pitchChange=round(profiles[0][i], 6), timingChange=round(profiles[1][i], 6), restChange=round(profiles[2][i], 6), restEvidence=rest_evidence, sustainedEnding=ending, nextMetricStrength=b["strength"]))
            if score >= 0.60:
                boundaries.append((i + 1, f or {"score": score}))
    phrase_spans = []
    cuts = [0] + [i for i, _ in boundaries] + [len(notes)]
    features["boundaryProfile"] = boundary_profile_data
    features["phraseCandidateCount"] = len(cuts) - 1 if notes else 0
    for i, (a, b) in enumerate(zip(cuts, cuts[1:])):
        if a == b:
            continue
        start, end = notes[a]["at"], notes[b - 1]["at"] + notes[b - 1]["length"]
        region = notes[a:b]
        region_sounding = sum(n["length"] for n in region)
        region_low, region_high = min(n["midi"] for n in region), max(n["midi"] for n in region)
        f = finding("phrase", f"Phrase candidate {i + 1} · {notes[a]['location']}",
                    f"{b - a} sustained notes; net motion {signed(notes[b - 1]['midi'] - notes[a]['midi'])} semitones. " +
                    f"Range {region_high - region_low} semitones; {ratio((b - a) * bridge.PPQ, end - start):g} notes per quarter. " +
                    ("Extent follows the excerpt limits; no strong internal boundary was found." if len(cuts) == 2 else "Extent follows stronger boundary candidates and excerpt limits."),
                    [(start, end)], min((item[1]["score"] for item in boundaries[max(0, i - 1):i + 1]), default=0.0),
                    dict(first=notes[a]["id"], last=notes[b - 1]["id"], noteCount=b - a, lowestMidi=region_low, highestMidi=region_high,
                         weightedRegister=ratio(sum(n["midi"] * n["length"] for n in region), region_sounding), silenceTicks=str(end - start - region_sounding)))
        if f:
            phrase_spans.append((a, b, f))

    tonal_regions = [("Whole excerpt", 0, len(notes), None)] if notes else []
    tonal_regions += [(f["title"], a, b, f["id"]) for a, b, f in phrase_spans if b - a >= 3 and b - a < len(notes)]
    tonal_results = []
    for label, a, b, parent in tonal_regions[:121]:
        candidates = tonal_candidates(notes[a:b])
        tonal_results.append(dict(region=parent, candidates=candidates))
        top = candidates[0]
        alternatives = "; ".join(f"{PC_NAMES[c['tonic']]} {c['scale']} (fit {c['fit']:.0%}, strength {c['score']:.2f})" for c in candidates[:3])
        f = finding("tonality", f"Tonal candidates · {label}", alternatives + ". " +
                    ("Too few distinct pitch classes to establish a scale. " if len({n['midi'] % 12 for n in notes[a:b]}) < 3 else "") +
                    "Collection fit alone does not establish a tonic; alternatives remain plausible.",
                    [(notes[a]["at"], notes[b - 1]["at"] + notes[b - 1]["length"])], top["score"], dict(candidates=candidates))
        if f and parent:
            relations.append(dict(source=parent, target=f["id"], kind="contains", similarity=1.0))
    declared = []
    note_starts = [n["at"] for n in notes]
    for index, k in enumerate(keys):
        t = k["tonality"]
        label = (pitch_name([t["step"], 4, t["alter"]])[:-1] + " " + ("major" if t["scale"] == "major" else t["minor"] + " minor")) if t else "undeclared tonality"
        begin = int(k["tick"] * bridge.PPQ)
        end = int(keys[index + 1]["tick"] * bridge.PPQ) if index + 1 < len(keys) else duration
        fit, outside = None, []
        if t:
            tonic = bridge.NATURAL[t["step"]] + t["alter"]
            collection = bridge.COLLECTIONS["major" if t["scale"] == "major" else t["minor"]]
            pcs = {(tonic + p) % 12 for p in collection}
            total = matched = 0
            for j in range(max(0, bisect_right(note_starts, begin) - 1), len(notes)):
                n = notes[j]
                if n["at"] >= end:
                    break
                overlap = max(0, min(end, n["at"] + n["length"]) - max(begin, n["at"]))
                total += overlap
                if n["midi"] % 12 in pcs:
                    matched += overlap
                elif overlap:
                    outside.append(n["id"])
            fit = ratio(matched, total) if total else None
        declared.append(dict(tick=str(begin), label=label, fifths=k["fifths"], fit=fit, outsideNotes=outside))
    overview.append("Declared context: " + "; ".join(f"+{fraction_text(int(k['tick']), bridge.PPQ)} quarters: {k['label']}" +
                    (f" (duration-weighted fit {k['fit']:.0%})" if k['fit'] is not None else "") for k in declared) + ". Inferred candidates do not alter this context.")

    # Duration and gap on each note preserve internal rests; the terminal token's
    # following gap is deliberately omitted when a candidate is verified.
    note_gap = gaps + [0] if notes else []
    streams = {
        "exact": [(n["midi"], n["length"], note_gap[i]) for i, n in enumerate(notes)],
        "transposed": [(v, notes[i]["length"], iois[i]) for i, v in enumerate(intervals)],
        "rhythmic": [(n["length"], note_gap[i]) for i, n in enumerate(notes)],
        "interval": [(v,) for v in intervals],
        "diatonic interval": [(v,) for v in diatonic],
    }
    seeds = []
    for kind, tokens in streams.items():
        for a, b, length in suffix_repeats(tokens):
            count = length + (kind in ("transposed", "interval", "diatonic interval"))
            count = min(count, b - a, len(notes) - b)
            if count >= 2:
                seeds.append((a, b, count, kind))
    priority = {kind: i for i, kind in enumerate(streams)}
    seeds.sort(key=lambda s: (-s[2], priority[s[3]], s[0], s[1]))
    families, signatures = [], {}
    for a, b, count, kind in seeds:
        # Compact long cycles; retain short nested cells when their occurrence sets differ.
        if any(old['kind'] == kind and old['a'] <= a and old['b'] <= b and
               a - old['a'] == b - old['b'] and a + count <= old['a'] + old['count'] for old in families):
            continue
        aa, bb = notes[a:a + count], notes[b:b + count]
        rhythm_a = [(n["length"], note_gap[a + i] if i < count - 1 else 0) for i, n in enumerate(aa)]
        rhythm_b = [(n["length"], note_gap[b + i] if i < count - 1 else 0) for i, n in enumerate(bb)]
        if kind == "transposed" and rhythm_a != rhythm_b:
            kind = "interval"
        if kind == "transposed" and all(x["midi"] == y["midi"] for x, y in zip(aa, bb)):
            kind = "exact" if rhythm_a == rhythm_b else "interval"
        if kind == "exact":
            signature = (kind, tuple(n["midi"] for n in aa), tuple(rhythm_a))
        elif kind == "rhythmic":
            signature = (kind, tuple(rhythm_a))
        else:
            values = diatonic if kind == "diatonic interval" else intervals
            signature = (kind, tuple(values[a:a + count - 1]), tuple(rhythm_a) if kind == "transposed" else ())
        if signature in signatures:
            old = signatures[signature]
            if len(old["occurrences"]) < MAX_OCCURRENCES:
                old["occurrences"].update((a, b))
            else:
                notices.append("Some motif families have more than 64 occurrences; occurrence lists are capped.")
            continue
        # Prefer a more specific explanation of the same occurrences.
        if any(old['a'] == a and old['b'] == b and old['count'] == count and priority[old['kind']] < priority[kind] for old in families):
            continue
        if len(families) >= MAX_FAMILIES:
            notices.append("Exact pattern discovery retained the 120 longest representative families; shorter candidates may be omitted.")
            break
        family = dict(a=a, b=b, count=count, kind=kind, occurrences={a, b})
        families.append(family)
        signatures[signature] = family
    families.sort(key=lambda f: (f['a'], -f['count'], priority[f['kind']], f['b']))
    short_families, limited = discover_gestures(notes, gaps, strengths, duration)
    represented = {(f["count"], tuple(independent_occurrences(f["occurrences"], f["count"]))) for f in families}
    families += [f for f in short_families if (f["count"], tuple(f["occurrences"])) not in represented]
    if limited:
        notices.append("Short structural discovery is limited to 120 representative families and 64 independent occurrences per family.")
    structure_spans = []
    role_counts = Counter()
    for index, family in enumerate(families):
        label = str(index + 1)
        a, count, match = family["a"], family["count"], family["kind"]
        detail = describe_family(family, notes, gaps, strengths, duration)
        occurrences = detail.pop("occurrences")
        kind = detail.pop("kind")
        if not occurrences:
            continue
        a = occurrences[0]
        pattern = intervals[a:a + count - 1]
        spans = [(notes[start]["at"], notes[start + count - 1]["at"] + notes[start + count - 1]["length"]) for start in occurrences]
        shape = ", ".join(detail["descriptors"]) or detail["cellType"]
        title = ("Motif candidate" if kind == "motif" else "Gesture candidate" if kind == "gesture" else detail["cellType"].capitalize()) + f" {label} · {count} notes"
        if detail["descriptors"]:
            title += " · " + shape
        claim = ("Shared " + match + " pattern" if len(occurrences) > 1 else "Detected " + shape)
        text = claim + " at " + "; ".join(notes[start]["location"] for start in occurrences) + ". "
        text += " ".join(reason + "." for reason in detail["support"] + detail["limitations"])
        transposable = match in ("exact", "transposed", "interval")
        evidence = dict(matchType=match, evidenceClaim=claim, noteCount=count, intervalPattern=pattern,
                        durationTicks=[str(t) for t in lengths[a:a + count]], **detail,
                        occurrences=[dict(label=f"{label}.{j + 1}", first=notes[start]["id"], last=notes[start + count - 1]["id"],
                                          initialPitchOffset=notes[start]["midi"] - notes[a]["midi"],
                                          transposition=notes[start]["midi"] - notes[a]["midi"] if transposable else None)
                                     for j, start in enumerate(occurrences)])
        f = finding(kind, title, text, spans, 1.0, evidence)
        if f:
            f["salience"] = detail["salience"]
            f["salienceReasons"] = detail["salienceReasons"]
            role_counts[kind] += 1
            structure_spans.append((a, a + count, f))
            for pa, pb, phrase in phrase_spans:
                if any(pa <= start and start + count <= pb for start in occurrences):
                    relations.append(dict(source=phrase["id"], target=f["id"], kind="contains", similarity=1.0))
    if any(f["count"] > MAX_WINDOW for f in families):
        notices.append("Abstract signatures describe the first 12 notes of longer exact families; complete interval and duration patterns remain in their evidence.")

    # Bounded comparisons between discovered musical units. No all-substring
    # alignment: that would make highly repetitive long melodies quadratic.
    candidates = structure_spans + phrase_spans[:120]
    comparisons = 0
    for i, (a, b, first) in enumerate(candidates):
        if b - a > 64:
            continue
        for c, d, second in candidates[i + 1:]:
            if comparisons >= MAX_COMPARISONS:
                break
            if d - c > 64 or max(b - a, d - c) > 2 * min(b - a, d - c) or not (b <= c or d <= a):
                continue
            comparisons += 1
            ip, iq = intervals[a:b - 1], intervals[c:d - 1]
            rp, rq = lengths[a:b], lengths[c:d]
            gp, gq = note_gap[a:b - 1], note_gap[c:d - 1]
            norm_p = reduce(gcd, rp + gp)
            norm_q = reduce(gcd, rq + gq)
            rhythm_p = [(t // norm_p, gp[j] // norm_p if j < len(gp) else 0) for j, t in enumerate(rp)]
            rhythm_q = [(t // norm_q, gq[j] // norm_q if j < len(gq) else 0) for j, t in enumerate(rq)]
            interval_sim, rhythm_sim = edit_similarity(ip, iq), edit_similarity(rhythm_p, rhythm_q)
            pitch_sim = edit_similarity([n["midi"] - notes[a]["midi"] for n in notes[a:b]],
                                        [n["midi"] - notes[c]["midi"] for n in notes[c:d]])
            contour_sim = edit_similarity(contours[a:b - 1], contours[c:d - 1])
            kind = "approximate variant"
            similarity = round(0.6 * pitch_sim + 0.4 * rhythm_sim, 6)
            if ip and ip == [-v for v in iq] and rp == rq and gp == gq:
                kind, similarity = "interval inversion", 1.0
            elif ip and ip == [-v for v in reversed(iq)] and rp == list(reversed(rq)) and gp == list(reversed(gq)):
                kind, similarity = "retrograde", 1.0
            elif ip == iq and rhythm_p == rhythm_q:
                kind = "rhythmic scaling" if norm_p != norm_q else "transposed repetition"
                similarity = 1.0
            elif contour_sim == 1 and len(ip) >= 2 and rhythm_sim >= 0.75 and similarity < 0.75:
                kind, similarity = "contour similarity", contour_sim
            if similarity >= 0.75:
                relation = dict(source=first["id"], target=second["id"], kind=kind, similarity=similarity,
                                      pitchSimilarity=pitch_sim, intervalSimilarity=interval_sim, rhythmSimilarity=rhythm_sim, contourSimilarity=contour_sim,
                                      initialPitchOffset=notes[c]["midi"] - notes[a]["midi"], rhythmRatio=str(Fraction(norm_q, norm_p)))
                if kind in ("transposed repetition", "rhythmic scaling"):
                    relation["transposition"] = relation["initialPitchOffset"]
                if kind == "interval inversion":
                    relation["axisMidi"] = (notes[c]["midi"] + notes[a]["midi"]) / 2
                relations.append(relation)
    if comparisons >= MAX_COMPARISONS:
        notices.append("Approximate matching reached 4,000 comparisons; additional variants may exist.")
    if len(relations) > 20000:
        relations = relations[:20000]
        notices.append("Relationship display is limited to 20,000 entries; additional relationships may exist.")
    notices.append("Approximate variants compare discovered structures and phrase candidates of at most 64 notes; exact repetition searches the full excerpt.")
    overview.append(f"Proposed {features['phraseCandidateCount']} phrase regions ({len(phrase_spans)} shown) and {role_counts['motif']} motif, {role_counts['gesture']} gesture and {role_counts['cell']} cell families. Scores rank evidence, not probabilities of correctness.")
    if any(n["clippedStart"] or n["clippedEnd"] for n in notes):
        notices.append("The selection clips a note or tie chain; clipped lengths and excerpt edges are not evidence of performed articulation or phrase endings.")
    output_notes = []
    key_positions = [int(k["tick"] * bridge.PPQ) for k in keys]
    for i, n in enumerate(notes):
        key = keys[bisect_right(key_positions, n["at"]) - 1]
        degree, inflection = bridge.coordinate(n["pitch"], key) if key["tonality"] else (None, None)
        output_notes.append(dict(id=n["id"], start=str(n["at"]), end=str(n["at"] + n["length"]), pitch=n["pitch"], midi=n["midi"],
                                 sources=n["sources"], name=n["name"], location=n["location"], writtenTicks=n["writtenTicks"],
                                 metricStrength=n["strength"], clippedStart=n["clippedStart"], clippedEnd=n["clippedEnd"],
                                 declaredScaleDegree=degree % 7 + 1 if degree is not None else None, inflectionSemitones=inflection,
                                 intervalFromPrevious=intervals[i - 1] if i else None, diatonicStepsFromPrevious=diatonic[i - 1] if i else None,
                                 gapBefore=str(gaps[i - 1]) if i else None, onsetInterval=str(iois[i - 1]) if i else None))
    return dict(analysis=dict(schema=SCHEMA, algorithm=ALGORITHM, duration=str(duration), features=features,
                             notes=output_notes, rests=[dict(start=str(r["at"]), end=str(r["at"] + r["length"]), sources=r["sources"], location=r["location"]) for r in rests],
                             tonalRegions=tonal_results, declaredKeys=declared, findings=findings, relations=relations,
                             overview=overview, notices=list(dict.fromkeys(notices)),
                             parameters=dict(boundaryThreshold=0.35, phraseThreshold=0.6, variantThreshold=0.75,
                                             maxNotes=MAX_NOTES, maxFamilies=MAX_FAMILIES, maxOccurrences=MAX_OCCURRENCES,
                                             maxComparisons=MAX_COMPARISONS, localBoundaryWeights=[0.25, 0.5, 0.25],
                                             tonalWeights=[0.65, 0.20, 0.15], maxShortWindow=MAX_WINDOW,
                                             salienceWeights=list(SALIENCE_WEIGHTS), abstractMotifMinOccurrences=3)))
