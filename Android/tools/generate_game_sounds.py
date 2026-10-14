"""Generate TurnHub's original short game-action cues (stdlib only).

Run from any directory. --check validates committed WAV files against the
defined patterns. No sampled or third-party audio is used.
"""
import argparse
import io
import math
from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parents[1] / "app/src/main/res/raw"
RATE = 22050
# (frequency Hz, duration ms), with silence separating notes. Different rhythms
# distinguish actions as well as pitch. Raised-cosine edges prevent clicks.
PATTERNS = {
    "pass": [(660, 55), (0, 35), (880, 80)],
    "start": [(523, 85), (0, 20), (659, 85), (0, 20), (784, 150)],
    "pause": [(587, 160), (0, 45), (392, 100)],
    "resume": [(392, 65), (0, 20), (587, 120)],
    "win": [(523, 85), (0, 20), (659, 85), (0, 20), (784, 85), (0, 20), (1047, 220)],
    "draw": [(440, 180), (0, 70), (330, 220)],
}


def render(pattern):
    frames = bytearray()
    for frequency, duration in pattern:
        count = round(RATE * duration / 1000)
        edge = min(round(RATE * .012), count // 2)
        for i in range(count):
            envelope = .5 - .5 * math.cos(math.pi * min(i, count - 1 - i, edge) / edge)
            phase = 2 * math.pi * frequency * i / RATE
            value = .22 * envelope * (math.sin(phase) + .12 * math.sin(2 * phase)) if frequency else 0
            frames.extend(struct.pack("<h", round(32767 * value)))
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(frames)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        ROOT.mkdir(parents=True, exist_ok=True)
    for name, pattern in PATTERNS.items():
        path = ROOT / f"game_{name}.wav"
        data = render(pattern)
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit(f"Regenerate {path.name} with Android/tools/generate_game_sounds.py")
        else:
            path.write_bytes(data)
    print(f"{'Checked' if args.check else 'Generated'} {len(PATTERNS)} original game cues")


if __name__ == "__main__":
    main()
