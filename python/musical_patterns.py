"""Bounded, deterministic structural descriptions; no tonal or stylistic inference."""
from functools import reduce
from math import gcd

MAX_WINDOW = 12
MAX_GROUPS = 120
MAX_OCCURRENCES = 64
SALIENCE_WEIGHTS = (0.35, 0.35, 0.30)
SIGNATURE_FIELDS = ("exactPitches", "chromaticIntervals", "diatonicIntervals", "contour",
                    "pitchEquality", "returns", "repeatedNotes", "rhythm")


def independent_occurrences(starts, count):
    result = []
    for start in sorted(starts):
        if not result or start >= result[-1] + count:
            result.append(start)
        if len(result) == MAX_OCCURRENCES:
            break
    return result


def signature(notes, gaps):
    pitches = [n["midi"] for n in notes]
    intervals = [b - a for a, b in zip(pitches, pitches[1:])]
    spellings = [n["pitch"][1] * 7 + n["pitch"][0] for n in notes]
    equality, symbols = [], {}
    for pitch in pitches:
        equality.append(symbols.setdefault(pitch, len(symbols)))
    unit = reduce(gcd, [n["length"] for n in notes] + list(gaps))
    return dict(exactPitches=pitches, chromaticIntervals=intervals,
                diatonicIntervals=[b - a for a, b in zip(spellings, spellings[1:])],
                contour=[(v > 0) - (v < 0) for v in intervals], pitchEquality=equality,
                returns=[p == pitches[0] for p in pitches],
                repeatedNotes=[False] + [a == b for a, b in zip(pitches, pitches[1:])],
                rhythm=[[n["length"] // unit, gaps[i] // unit if i < len(gaps) else 0]
                        for i, n in enumerate(notes)], rhythmUnitTicks=str(unit))


def descriptors(region):
    pitches = [n["midi"] for n in region]
    intervals = [b - a for a, b in zip(pitches, pitches[1:])]
    directions = [(v > 0) - (v < 0) for v in intervals if v]
    turns = sum(a != b for a, b in zip(directions, directions[1:]))
    labels = []
    if pitches[0] == pitches[-1] and any(intervals):
        labels.append("departure-return")
        if max(map(abs, intervals)) >= 5:
            labels.append("leap-return")
    if turns == 1:
        labels.append("arch" if directions[0] > 0 else "inverted-arch")
    if any(v == 0 for v in intervals) and (any(intervals) or len({n["length"] for n in region}) > 1):
        labels.append("repeated-note emphasis")
    if len({n["length"] for n in region}) > 1:
        labels.append("rhythmic contrast")
    if len(region) == 2 and abs(intervals[0]) >= 5:
        labels.append("distinctive leap")
    return labels


def grouping(notes, gaps, boundary_strengths, duration, a, b, explain=True):
    first, last = notes[a], notes[b - 1]
    left_rest = (gaps[a - 1] > 0 if a else first["at"] > 0) and not first["clippedStart"]
    right_rest = (gaps[b - 1] > 0 if b < len(notes) else last["at"] + last["length"] < duration) and not last["clippedEnd"]
    strong_start = first["strength"] >= 0.5 and not first["clippedStart"]
    left_boundary = a > 0 and boundary_strengths[a - 1] >= 0.35 and not first["clippedStart"]
    right_boundary = b < len(notes) and boundary_strengths[b - 1] >= 0.35 and not last["clippedEnd"]
    sustained_end = b - a > 1 and last["length"] >= 2 * notes[b - 2]["length"] and not last["clippedEnd"]
    start = left_rest or strong_start or left_boundary
    end = right_rest or right_boundary or sustained_end
    score = (int(start) + int(end)) / 2
    if not explain:
        return score, bool(start and end), []
    reasons = []
    if left_rest:
        reasons.append("begins after notated silence")
    if strong_start:
        reasons.append("begins on a strong metric position")
    if left_boundary:
        reasons.append("begins at a structural boundary cue")
    if right_rest:
        reasons.append("ends before notated silence")
    if right_boundary:
        reasons.append("ends at a structural boundary cue")
    if sustained_end:
        reasons.append("ends with a sustained note")
    return score, bool(start and end), reasons


def discover_gestures(notes, gaps, boundary_strengths, duration):
    """At most one coherent short proposal per onset, then indexed shape families."""
    groups = {}
    a = 0
    while a < len(notes):
        end = a + 1
        while end < len(notes) and not gaps[end - 1] and notes[end]["midi"] == notes[a]["midi"] and notes[end]["length"] == notes[a]["length"]:
            end += 1
        if end - a >= 2:
            count = min(MAX_WINDOW, end - a)
            sig = signature(notes[a:a + count], gaps[a:a + count - 1])
            key = (tuple(sig["contour"]), tuple(sig["pitchEquality"]), tuple(map(tuple, sig["rhythm"])))
            family = groups.setdefault(key, dict(a=a, b=a, count=count, kind="interval", occurrences=[], total=0))
            for start in range(a, end - count + 1, count):
                family["total"] += 1
                if len(family["occurrences"]) < MAX_OCCURRENCES:
                    family["occurrences"].append(start)
        a = end
    for a in range(len(notes) - 1):
        window = notes[a:min(len(notes), a + MAX_WINDOW)]
        if all(n["midi"] == window[0]["midi"] and n["length"] == window[0]["length"] for n in window):
            continue
        best = None
        moving = repeated = varied_rhythm = False
        previous_direction = turns = largest_leap = 0
        for b in range(a + 2, min(len(notes), a + MAX_WINDOW) + 1):
            # Silence at least as long as the preceding note separates short
            # gestures; exact repetition still retains longer internal rests.
            if gaps[b - 2] >= notes[b - 2]["length"]:
                break
            previous, last = notes[b - 2], notes[b - 1]
            delta = last["midi"] - previous["midi"]
            direction = (delta > 0) - (delta < 0)
            if direction:
                if previous_direction and previous_direction != direction:
                    turns += 1
                previous_direction = direction
                moving = True
            else:
                repeated = True
            largest_leap = max(largest_leap, abs(delta))
            varied_rhythm = varied_rhythm or last["length"] != notes[a]["length"]
            returns = moving and last["midi"] == notes[a]["midi"]
            arch = turns == 1
            if not (returns or arch or varied_rhythm or (repeated and moving) or (b - a == 2 and largest_leap >= 5)):
                continue
            group, bounded, _ = grouping(notes, gaps, boundary_strengths, duration, a, b, explain=False)
            completed_shape = returns or arch
            if not bounded and not completed_shape:
                continue
            # Avoid arbitrary extensions of an already completed contour shape.
            if completed_shape and b < len(notes) and not bounded and notes[b - 1]["midi"] == notes[b]["midi"]:
                continue
            rank = (bounded, returns, returns and largest_leap >= 5, group, -(b - a))
            if best is None or rank > best[0]:
                best = rank, b
        if best is None:
            continue
        b = best[1]
        sig = signature(notes[a:b], gaps[a:b - 1])
        key = (tuple(sig["contour"]), tuple(sig["pitchEquality"]), tuple(map(tuple, sig["rhythm"])))
        if key not in groups:
            groups[key] = dict(a=a, b=a, count=b - a, kind="abstract gesture", occurrences=[], total=0)
        family = groups[key]
        if not family["occurrences"] or a >= family["occurrences"][-1] + family["count"]:
            family["total"] += 1
            if len(family["occurrences"]) < MAX_OCCURRENCES:
                family["occurrences"].append(a)
    ranked = sorted(groups.values(), key=lambda f: (-min(f["total"], 4), -f["count"], f["a"]))
    return ranked[:MAX_GROUPS], len(ranked) > MAX_GROUPS or any(f["total"] > MAX_OCCURRENCES for f in ranked)


def describe_family(family, notes, gaps, boundary_strengths, duration):
    count, match = family["count"], family["kind"]
    occurrences = independent_occurrences(family["occurrences"], count)
    a = occurrences[0]
    prefix = min(count, MAX_WINDOW)
    sig = signature(notes[a:a + prefix], gaps[a:a + prefix - 1])
    shared = list(SIGNATURE_FIELDS)
    rhythm = None
    equal_rhythm = True
    groups, shapes = [], []
    for start in occurrences:
        other = signature(notes[start:start + prefix], gaps[start:start + prefix - 1])
        shared = [field for field in shared if sig[field] == other[field]]
        durations = [n["length"] for n in notes[start:start + count]]
        rests = gaps[start:start + count - 1]
        unit = reduce(gcd, durations + rests)
        normalized = tuple(v // unit for v in durations + rests)
        if rhythm is None:
            rhythm = normalized
        elif rhythm != normalized:
            equal_rhythm = False
        groups.append(grouping(notes, gaps, boundary_strengths, duration, start, start + count))
        shapes.append(descriptors(notes[start:start + count]))
    labels = [v for v in shapes[0] if all(v in other for other in shapes[1:])]
    bounded = all(g[1] for g in groups)
    specific = equal_rhythm and (match in ("exact", "transposed", "interval", "diatonic interval") or
                                (prefix == count and ("chromaticIntervals" in shared or "diatonicIntervals" in shared)))
    abstract_support = (match == "abstract gesture" and len(occurrences) >= 3 and equal_rhythm and
                        "rhythmic contrast" in labels and all(notes[s]["strength"] >= 0.5 for s in occurrences))
    motif = len(occurrences) >= 2 and bounded and (specific or abstract_support)
    gesture = bool(labels) and (bounded or any(v in labels for v in ("departure-return", "arch", "inverted-arch")))
    kind = "motif" if motif else "gesture" if gesture else "cell"
    cell_type = "repeated-note cell" if not any(sig["chromaticIntervals"]) else "rhythmic cell" if match == "rhythmic" else "interval cell"
    supports, limitations = [], []
    if len(occurrences) >= 2:
        supports.append(f"{len(occurrences)} non-overlapping occurrences share the reported pattern")
    if bounded:
        supports.append("each occurrence has supported beginning and ending cues")
    if specific and len(occurrences) > 1:
        supports.append("pitch/interval identity and normalized rhythm agree")
    if abstract_support:
        supports.append("the shaped rhythm recurs at least three times on strong metric positions")
    if not motif:
        if len(occurrences) < 2:
            limitations.append("one occurrence cannot establish a recurring motif within this excerpt")
        elif not bounded:
            limitations.append("recurrence alone does not establish coherent musical grouping")
        elif not specific and not abstract_support:
            limitations.append("shared shape or rhythm alone does not establish a common motif identity")
    emphasis_values = {"departure-return": 0.7, "leap-return": 0.9, "arch": 0.6, "inverted-arch": 0.6,
                       "repeated-note emphasis": 0.6, "rhythmic contrast": 0.8, "distinctive leap": 0.7}
    # Correlated contour/return cues use their maximum, never independent votes.
    emphasis = [max((emphasis_values[v] for v in shape), default=0.0) for shape in shapes]
    salient = max(range(len(occurrences)), key=lambda i: (groups[i][0] * SALIENCE_WEIGHTS[0] + emphasis[i] * SALIENCE_WEIGHTS[1], -occurrences[i]))
    components = dict(grouping=groups[salient][0], emphasis=emphasis[salient], recurrence=min(1.0, (len(occurrences) - 1) / 3))
    salience = round(sum(w * v for w, v in zip(SALIENCE_WEIGHTS, components.values())), 6)
    reasons = [f"Occurrence {salient + 1}: " + "; ".join(groups[salient][2])] if groups[salient][2] else []
    if shapes[salient]:
        reasons.append("Shaped emphasis: " + ", ".join(shapes[salient]))
    if len(occurrences) > 1:
        reasons.append(f"Recurs in {len(occurrences)} independent locations")
    return dict(kind=kind, cellType=cell_type, descriptors=labels, occurrences=occurrences, pattern=sig,
                signatureNoteCount=prefix, signatureComplete=count == prefix, matchedDimensions=shared,
                support=supports, limitations=limitations, salience=salience, salienceComponents=components,
                salienceReasons=reasons, salientOccurrence=salient)
