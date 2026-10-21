"""Protocol-only worker double. Never used by the application."""
import json
import sys
import time
from pathlib import Path

mode = sys.argv[1] if len(sys.argv) > 1 else "identity"
for line in sys.stdin:
    request = json.loads(line)
    if mode.startswith("preview"):
        sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))
        import worker
        command = request.get("command", "transform")
        if len(sys.argv) > 2:
            with open(sys.argv[2], "a") as log:
                log.write(command + "\n")
        if command == "analyze_melody":
            time.sleep(0.7 if mode == "preview_slow_report" else 0.12)
            if mode == "preview_report_error":
                response = dict(version=2, id=request["id"], ok=False,
                                error=dict(code="analysis", message="Fixture report failure"))
            else:
                from melody_analysis import analyze
                response = dict(version=2, id=request["id"], ok=True, **analyze(request, worker))
        else:
            time.sleep(0.2)
            # Deliberately malformed proposal after a valid preview, for rollback tests.
            if request.get("steps", [{}])[0].get("amount") == 13:
                request["events"][0]["pitches"] = [[9, 4, 0]]
                response = dict(request, ok=True)
            else:
                response = dict(version=2, id=request["id"], ok=True, **worker.transform(request))
        print(json.dumps(response, separators=(",", ":")), flush=True)
        continue
    if mode == "delay":
        time.sleep(10)
    if mode == "crash":
        raise SystemExit(7)
    if mode == "malformed":
        print("{broken", flush=True)
        continue
    if mode == "oversize":
        print("x" * (32 * 1024 * 1024 + 1), flush=True)
        continue
    request["ok"] = True
    if mode == "wrong_id":
        request["id"] = "unrelated"
    if mode == "bad_pitch":
        request["events"][0]["pitches"] = [[9, 4, 0]]
        request["events"][0]["ties"] = [False]
    if mode == "bad_tie":
        request["events"][-1]["ties"] = [True] * len(request["events"][-1]["pitches"])
    if mode == "bad_key":
        request["keys"][0]["fifths"] = 8
    print(json.dumps(request, separators=(",", ":")), flush=True)
