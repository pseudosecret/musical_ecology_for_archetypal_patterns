"""Version 2 selected-passage worker. Stdout contains JSON lines only."""
from bisect import bisect_right
from copy import deepcopy
from fractions import Fraction
import json
import math
import sys

VERSION = 2
PPQ = 6720
MAX_BYTES = 32 * 1024 * 1024
MAX_EVENTS = 250_000
MAX_TICKS = 2_000_000 * PPQ
NATURAL = (0, 2, 4, 5, 7, 9, 11)
COLLECTIONS = {
    "major": (0, 2, 4, 5, 7, 9, 11),
    "melodic": (0, 2, 3, 5, 7, 9, 11),
    "harmonic": (0, 2, 3, 5, 7, 8, 11),
    "aeolian": (0, 2, 3, 5, 7, 8, 10),
}


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError("Integer parameter outside its supported range.")
    return value


def ticks(value, positive=False):
    if not isinstance(value, str) or not value.isdecimal():
        raise ValueError("Timing must be a decimal integer string.")
    result = int(value)
    if not (1 if positive else 0) <= result <= MAX_TICKS:
        raise ValueError("Timing is outside the supported result range.")
    return result


def midi(p):
    return (p[1] + 1) * 12 + NATURAL[p[0]] + p[2]


def signature(fifths, minor=False, form="melodic"):
    step = (4 * fifths + (5 if minor else 0)) % 7
    pc = (7 * fifths + (9 if minor else 0)) % 12
    alter = (pc - NATURAL[step] + 6) % 12 - 6
    return dict(step=step, alter=alter, scale="minor" if minor else "major", minor=form if minor else "melodic")


def read_keys(raw, duration):
    if not isinstance(raw, list) or not 0 < len(raw) <= MAX_EVENTS:
        raise ValueError("Invalid key region count.")
    result = []
    previous = -1
    for k in raw:
        at = Fraction(ticks(k["tick"]), PPQ)
        if at <= previous or at >= duration:
            raise ValueError("Key regions must be sorted, unique, and inside the passage.")
        f, t = k["fifths"], k["tonality"]
        if f is not None:
            integer(f, -7, 7)
        if t is not None:
            if not isinstance(t, dict) or t.get("scale") not in ("major", "minor") or t.get("minor") not in ("melodic", "harmonic", "aeolian"):
                raise ValueError("Unsupported scale identity.")
            if f is None or any(type(t.get(field)) is not int for field in ("step", "alter")) or t != signature(f, t["scale"] == "minor", t["minor"]):
                raise ValueError("Tonic and key signature disagree.")
        result.append(dict(tick=at, fifths=f, tonality=deepcopy(t)))
        previous = at
    if result[0]["tick"] != 0:
        raise ValueError("The first key region must start at zero.")
    return result


def key_at(keys, at):
    # The caller builds the time index once per operation.
    regions, starts = keys
    return regions[bisect_right(starts, at) - 1]


def key_index(keys):
    return keys, [k["tick"] for k in keys]


def collection(key):
    t = key["tonality"]
    if t is None:
        raise ValueError("Confirm a key and scale for every region before using a tonal transformation.")
    return t, COLLECTIONS["major" if t["scale"] == "major" else t["minor"]]


def coordinate(p, key):
    t, scale = collection(key)
    degree = p[1] * 7 + p[0] - (28 + t["step"])
    octave, step = divmod(degree, 7)
    base = 60 + NATURAL[t["step"]] + t["alter"] + 12 * octave + scale[step]
    return degree, midi(p) - base


def from_coordinate(degree, inflection, key):
    t, scale = collection(key)
    octave, step = divmod(degree, 7)
    target = 60 + NATURAL[t["step"]] + t["alter"] + 12 * octave + scale[step] + inflection
    written_octave, written_step = divmod(28 + t["step"] + degree, 7)
    p = [written_step, written_octave, target - ((written_octave + 1) * 12 + NATURAL[written_step])]
    valid_pitch(p)
    return p


def valid_pitch(p):
    if not isinstance(p, list) or len(p) != 3:
        raise ValueError("Invalid pitch spelling.")
    integer(p[0], 0, 6)
    integer(p[1], -1, 9)
    integer(p[2], -2, 2)
    if not 0 <= midi(p) <= 127:
        raise ValueError("Pitch is outside MIDI 0–127.")


def alteration(fifths, step):
    if fifths is None:
        return 0
    order = (3, 0, 4, 1, 5, 2, 6) if fifths >= 0 else (6, 2, 5, 1, 4, 0, 3)
    return (1 if fifths >= 0 else -1) if step in order[:abs(fifths)] else 0


def spell(target, old, key):
    if target == midi(old):
        return list(old)
    if not 0 <= target <= 127:
        raise ValueError("Transformation produces a pitch outside MIDI 0–127.")
    candidates = []
    for octave in range(max(-1, target // 12 - 2), min(9, target // 12) + 1):
        for step in range(7):
            alter = target - ((octave + 1) * 12 + NATURAL[step])
            if not -2 <= alter <= 2:
                continue
            p = [step, octave, alter]
            in_scale = key["tonality"] is not None and coordinate(p, key)[1] == 0
            preference = alter > 0 if (key["fifths"] or 0) < 0 else alter < 0
            cost = (not in_scale, abs(alter - alteration(key["fifths"], step)), abs(alter), preference, step)
            candidates.append((cost, p))
    return min(candidates)[1]


def validate_events(events):
    for index, e in enumerate(events):
        pitches = e["pitches"]
        if not isinstance(pitches, list) or len(pitches) > 16 or not isinstance(e["ties"], list) or len(pitches) != len(e["ties"]) or any(type(t) is not bool for t in e["ties"]):
            raise ValueError("Invalid chord or tie data.")
        for p in pitches:
            valid_pitch(p)
        if len({midi(p) for p in pitches}) != len(pitches):
            raise ValueError("Transformation collapses chord tones onto the same pitch; choose Preserve chromatic inflections or a different operation.")
        for p, tied in zip(pitches, e["ties"]):
            if tied and (index + 1 == len(events) or p not in events[index + 1]["pitches"]):
                raise ValueError("Invalid outgoing tie.")


def passage(request):
    # Dependency errors remain recoverable even for operations using exact arithmetic.
    import music21  # noqa: F401
    if request.get("version") != VERSION or request.get("ppq") != PPQ:
        raise ValueError("Unsupported worker protocol.")
    raw = request["events"]
    if not isinstance(raw, list) or not 0 < len(raw) <= MAX_EVENTS:
        raise ValueError("Invalid passage event count.")
    duration = Fraction(ticks(request["duration"], True), PPQ)
    events = deepcopy(raw)
    for e in events:
        e["length"] = Fraction(ticks(e["ticks"], True), PPQ)
    if sum(e["length"] for e in events) != duration:
        raise ValueError("Passage duration does not match its events.")
    validate_events(events)
    return events, read_keys(request["keys"], duration), duration


def map_attacks(events, action):
    at, carry = Fraction(0), {}
    for e in events:
        changed = []
        following = {}
        for p, tied in zip(e["pitches"], e["ties"]):
            source = tuple(p)
            result = carry[source] if source in carry else action(p, at)
            changed.append(list(result))
            if tied:
                following[source] = result
        e["pitches"] = changed
        carry = following
        at += e["length"]


def remap(p, source, destination, strict):
    if not strict and source["tonality"] == destination["tonality"] and source["fifths"] == destination["fifths"]:
        return list(p)
    degree, inflection = coordinate(p, source)
    a, _ = collection(source)
    b, _ = collection(destination)
    difference = NATURAL[b["step"]] + b["alter"] - NATURAL[a["step"]] - a["alter"]
    shift = (difference + 6) % 12 - 6
    if shift == -6 and difference > 0:
        shift = 6
    octave_shift = (shift - difference) // 12
    return from_coordinate(degree + 7 * octave_shift, 0 if strict else inflection, destination)


def retrograde(events, regions, duration, reverse_keys, strict):
    original = key_index(regions)
    if reverse_keys:
        ends = [k["tick"] for k in regions[1:]] + [duration]
        regions = [dict(k, tick=duration - end) for k, end in reversed(list(zip(regions, ends)))]
    destination = key_index(regions)
    at, incoming, chains = Fraction(0), {}, []
    assignments = []
    for e in events:
        ids, following = [], {}
        for p, tied in zip(e["pitches"], e["ties"]):
            identity = incoming.get(tuple(p))
            if identity is None:
                identity = len(chains)
                chains.append(dict(pitch=p, start=at, end=at + e["length"]))
            chains[identity]["end"] = at + e["length"]
            ids.append(identity)
            if tied:
                following[tuple(p)] = identity
        assignments.append(ids)
        incoming = following
        at += e["length"]
    changed = [remap(c["pitch"], key_at(original, c["start"]), key_at(destination, duration - c["end"]), strict) for c in chains]
    reversed_ties = [[False] * len(e["ties"]) for e in events]
    for i, e in enumerate(events[:-1]):
        for p, tied in zip(e["pitches"], e["ties"]):
            if tied:
                reversed_ties[i + 1][events[i + 1]["pitches"].index(p)] = True
    for e, ids, ties in zip(events, assignments, reversed_ties):
        e["pitches"] = [list(changed[identity]) for identity in ids]
        e["ties"] = ties
    events.reverse()
    return regions


def transform(request):
    if request.get("command") != "transform":
        raise ValueError("Unsupported worker command.")
    events, regions, duration = passage(request)
    steps = request["steps"]
    if not isinstance(steps, list) or not 0 < len(steps) <= 64:
        raise ValueError("Invalid transformation count.")
    scale = Fraction(1)
    for index, operation in enumerate(steps):
        kind = operation.get("kind")
        policy = operation.get("accidentals", "preserve")
        if policy not in ("preserve", "scale"):
            raise ValueError("Unsupported accidental policy.")
        strict = policy == "scale"
        keys = key_index(regions)
        if kind in ("transpose", "invert"):
            mode = operation.get("mode", "semitones")
            if mode not in ("semitones", "degrees"):
                raise ValueError("Unsupported pitch mode.")
            amount = integer(operation.get("amount", 0), 0 if kind == "invert" and mode == "semitones" else -127, 127)
            axis_degree = integer(operation.get("axisDegree", 1), 1, 7)
            tonic_octave = integer(operation.get("tonicOctave", 4), -1, 9)

            def change(p, at):
                key = key_at(keys, at)
                if mode == "semitones":
                    return spell(midi(p) + amount if kind == "transpose" else 2 * amount - midi(p), p, key)
                degree, inflection = coordinate(p, key)
                if strict:
                    inflection = 0
                if kind == "transpose":
                    degree += amount
                else:
                    degree = 2 * (axis_degree - 1 + 7 * (tonic_octave - 4)) - degree
                    inflection = -inflection
                return from_coordinate(degree, inflection, key)

            map_attacks(events, change)
        elif kind == "scale":
            factor = Fraction(integer(operation.get("numerator"), 1, 1_000_000), integer(operation.get("denominator"), 1, 1_000_000))
            scale *= factor
            duration *= factor
            if duration * PPQ > MAX_TICKS:
                raise ValueError("The scaled passage exceeds the result duration limit.")
            for e in events:
                e["length"] *= factor
            for k in regions:
                k["tick"] *= factor
        elif kind == "retrograde":
            reverse_keys = operation.get("reverseKeys", False)
            if type(reverse_keys) is not bool:
                raise ValueError("Invalid reverse-key-regions setting.")
            regions = retrograde(events, regions, duration, reverse_keys, strict)
        else:
            raise ValueError(f"Unsupported transformation in step {index + 1}.")
        validate_events(events)
    output = []
    for e in events:
        raw = {k: v for k, v in e.items() if k != "length"}
        exact = e["length"] * PPQ
        if exact.denominator != 1 or exact <= 0:
            raise ValueError("The requested rhythm does not fit the exact score time grid.")
        raw["ticks"] = str(exact.numerator)
        if scale != 1:
            scaled_unit = Fraction(int(raw["unit"])) * scale
            scaled_normal = Fraction(raw["normal"]) * scale
            bases = (PPQ // 8, PPQ // 4, PPQ // 2, PPQ, 2 * PPQ, 4 * PPQ)
            ratios = ((3, 2), (5, 3), (5, 4), (7, 4), (7, 6))
            if raw["group"] != "0" and scaled_unit.denominator == 1 and scaled_unit in bases:
                raw["unit"] = str(scaled_unit.numerator)
            elif raw["group"] != "0" and scaled_normal.denominator == 1 and (raw["actual"], scaled_normal.numerator) in ratios:
                raw["normal"] = scaled_normal.numerator
            else:
                raw.update(group="0", actual=1, normal=1, unit="0")
        output.append(raw)
    key_output = []
    for key in regions:
        exact = key["tick"] * PPQ
        if exact.denominator != 1:
            raise ValueError("A scaled key change does not fit the exact score time grid.")
        key_output.append(dict(key, tick=str(exact.numerator)))
    return dict(ppq=PPQ, duration=str(int(duration * PPQ)), events=output, keys=key_output)


def analyze_keys(request):
    from music21 import chord, note, pitch, stream
    if request.get("command") != "analyze_keys":
        raise ValueError("Unsupported worker command.")
    events, regions, duration = passage(request)
    ends = [k["tick"] for k in regions[1:]] + [duration]
    starts, at = [], Fraction(0)
    for e in events:
        starts.append(at)
        at += e["length"]
    results = []
    for index, (key, end) in enumerate(zip(regions, ends)):
        if key["tonality"] is not None:
            continue
        part = stream.Stream()
        count = 0
        first = max(0, bisect_right(starts, key["tick"]) - 1)
        for position in range(first, len(events)):
            e, at = events[position], starts[position]
            if at >= end:
                break
            length = min(at + e["length"], end) - max(at, key["tick"])
            if length > 0 and e["pitches"]:
                pitches = []
                for step, octave, alter in e["pitches"]:
                    p = pitch.Pitch()
                    p.step, p.octave, p.accidental = "CDEFGAB"[step], octave, alter
                    pitches.append(p)
                obj = chord.Chord(pitches) if len(pitches) > 1 else note.Note(pitches[0])
                obj.duration.quarterLength = length
                part.insert(max(at, key["tick"]) - key["tick"], obj)
                count += 1
        candidates = []
        if count:
            best = part.analyze("key.krumhanslschmuckler")
            ranked = [best] + best.alternateInterpretations
            if key["fifths"] is not None:
                for minor in (False, True):
                    tonic = signature(key["fifths"], minor)
                    pc = (NATURAL[tonic["step"]] + tonic["alter"]) % 12
                    matching = [c for c in ranked if c.mode == ("minor" if minor else "major") and c.tonic.pitchClass == pc]
                    if matching:
                        candidates.append(dict(fifths=key["fifths"], form=int(minor), score=float(matching[0].correlationCoefficient)))
                candidates.sort(key=lambda c: -c["score"])
            else:
                for candidate in ranked:
                    if candidate.mode in ("major", "minor") and -7 <= candidate.sharps <= 7:
                        candidates.append(dict(fifths=candidate.sharps, form=int(candidate.mode == "minor"), score=float(candidate.correlationCoefficient)))
                    if len(candidates) == 3:
                        break
        if any(not math.isfinite(c["score"]) for c in candidates):
            candidates = []
        results.append(dict(region=index, candidates=candidates))
    return dict(regions=results)


def serve():
    for line in iter(lambda: sys.stdin.buffer.readline(MAX_BYTES + 1), b""):
        if len(line) > MAX_BYTES or not line.endswith(b"\n"):
            return 2
        request_id = ""
        request = {}
        try:
            request = json.loads(line)
            request_id = request.get("id", "")
            if not isinstance(request_id, str):
                raise ValueError("Request ID must be a string.")
            if request.get("command") == "analyze_melody":
                from melody_analysis import analyze
                result = analyze(request, sys.modules[__name__])
            elif request.get("command") == "analyze_keys":
                result = analyze_keys(request)
            else:
                result = transform(request)
            response = dict(version=VERSION, id=request_id, ok=True, **result)
        except Exception as exc:
            code = "dependency" if isinstance(exc, ImportError) else "analysis" if isinstance(request, dict) and request.get("command") == "analyze_melody" else "transformation"
            message = "Python/music21 is unavailable. Run poetry install, then launch the editor with poetry run." if code == "dependency" else str(exc)
            response = dict(version=VERSION, id=request_id, ok=False, error=dict(code=code, message=message))
        encoded = json.dumps(response, separators=(",", ":"), allow_nan=False)
        if len(encoded.encode()) > MAX_BYTES:
            return 2
        print(encoded, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(serve())
