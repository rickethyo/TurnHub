"""Regression guard for the Atlas controller/application boundary.

Transport adapters (Sigil radio events, browser callbacks, front-panel
buttons, touchscreen buttons, loop timers) must only build Intents and dispatch them. This check
fails if an adapter body mutates canonical game/lobby state, assigns
table-decision state, calls a lifecycle transition helper, or calls an
Intent handler directly instead of going through the dispatcher.
"""
from pathlib import Path
import re

SRC = Path(__file__).resolve().parents[2] / "src"

# Adapter functions, grouped by the module that must define them.
ADAPTERS = {
    "sigil_input.cpp": ["handleLobbyShort", "handlePass", "handleActionDown", "handleActionUp",
                        "handleActionShort", "handleActionLong", "handleActionWin",
                        "processSigilEvents", "respondToWinClaim", "dispatchPauseOrResume",
                        "connectionBlocked"],
    "web_adapters.cpp": ["handleWebControl", "handleProfileControl", "configureGame",
                         "changeLife", "changeCounter", "moderateAccount", "manageDevices",
                         "dispatchBrowserSeatIntent"],
    "front_panel.cpp": ["updateMasterButton"],
    "touch_controls.cpp": ["updateTouchControls", "dispatchTouchAction"],
    "gameplay_intents.cpp": ["updatePendingPass"],
    "table_intents.cpp": ["updateCountdown"],
}

FORBIDDEN = [
    r"game\.(?:start|reset|passTurn|pause|resume|endInDraw|changeLife|requestLifeChange|respondLifeChange|expireLifeChanges|cancelLifeChanges|changeCommanderDamage|eliminatePlayer|beginWinClaim|confirmWinClaim|denyWinClaim|cancelWinClaim)\s*\(",
    r"lobby\.(?:join|leave|toggleSecondary|selectStarter|selectStarterSeat|randomStarter|resetEmpty|resetForRematch|setStartArmedBy|clearStartArm|replaceController)\s*\(",
    r"\b(?:hubState|eliminationTargetPlayer|winArmedModule|winArmedPlayer|pendingPass|countdownStartedAtMs|nextGameSettings)\s*=(?!=)",
    r"\b(?:enterEmptyLobby|enterRematchLobby|startGame|beginCountdown|cancelCountdown|finishGameState|clearDecisionState|confirmElimination|beginEliminationSelection|cycleEliminationTarget|cancelEliminationSelection|clearPendingPass|cancelPendingPassForModule)\s*\(",
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
