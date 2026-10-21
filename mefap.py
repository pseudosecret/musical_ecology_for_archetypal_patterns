"""Launch the C++ editor in Poetry's Python environment."""
import os
from pathlib import Path
import sys


def main() -> int:
    executable = Path(__file__).resolve().parent / "build" / (
        "music_as_becoming.exe" if os.name == "nt" else "music_as_becoming"
    )
    try:
        os.execv(executable, [str(executable), *sys.argv[1:]])
    except OSError as error:
        print(f"Could not launch the editor: {error}", file=sys.stderr)
        print("Build it with: cmake -S . -B build && cmake --build build", file=sys.stderr)
        return 1
