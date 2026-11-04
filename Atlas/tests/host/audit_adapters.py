"""Regression guard for the Atlas controller/application boundary.

Transport adapters (Sigil radio events, browser callbacks,
touchscreen buttons, loop timers) must only build Intents and dispatch them
(or queue hosted inputs for the hub). This check fails if an adapter body
calls an Intent handler directly instead of going through the dispatcher, or
changes pairing state itself.
"""
from pathlib import Path
import re

SRC = Path(__file__).resolve().parents[2] / "src"

# Adapter functions, grouped by the module that must define them.
ADAPTERS = {
    "sigil_input.cpp": ["processSigilEvents", "handleIdleSelectAction"],
    "web_adapters.cpp": ["manageDevices"],
    "touch_controls.cpp": ["updateTouchControls", "dispatchTouchAction"],
    "front_panel.cpp": ["updateBootButton"],
}

FORBIDDEN = [
    r"\bhandle\w+Intent\s*\(",
    r"sigilBus\.(?:forget|openPairing)\s*\(",
    r"\bpairingWindowMs\s*=(?!=)",
]


def function_body(source: str, name: str) -> str:
    matches = list(re.finditer(r"\b(?:void|bool) " + name + r"\([^)]*\)\s*\{", source))
    assert len(matches) == 1, f"{name}: expected one definition, found {len(matches)}"
    start = matches[0].end()
    depth, end = 1, start
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end - 1]


count = 0
for filename, names in ADAPTERS.items():
    source = (SRC / filename).read_text()
    for name in names:
        body = function_body(source, name)
        for pattern in FORBIDDEN:
            assert not re.search(pattern, body), f"{filename}:{name} bypasses dispatcher: {pattern}"
        count += 1
print(f"PASS: {count} adapters have no direct canonical mutations or transition calls")
