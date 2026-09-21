"""Regression guard for the current Atlas controller/application boundary."""
from pathlib import Path
import re

source = (Path(__file__).resolve().parents[2] / "src/main.cpp").read_text()
adapters = ["handleWebControl", "handleProfileControl", "handleLobbyShort", "handlePass", "handleActionDown",
            "handleActionUp", "handleActionShort", "handleActionLong", "handleActionWin",
            "processSigilEvents", "updateMasterButton", "updateCountdown", "updatePendingPass"]
for name in adapters:
    match = re.search(r"\b(?:void|bool) " + name + r"\([^)]*\)\s*\{", source)
    assert match, name
    start = match.end()
    depth, end = 1, start
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    body = source[start:end - 1]
    forbidden = [
        r"game\.(?:start|reset|passTurn|pause|resume|eliminatePlayer|beginWinClaim|confirmWinClaim|denyWinClaim|cancelWinClaim)\s*\(",
        r"lobby\.(?:join|leave|toggleSecondary|selectStarter|selectStarterSeat|randomStarter|resetEmpty|resetForRematch|setStartArmedBy|clearStartArm)\s*\(",
        r"\b(?:hubState|eliminationTargetPlayer|winArmedModule|winArmedPlayer|pendingPass|countdownStartedAtMs)\s*=(?!=)",
        r"\b(?:enterEmptyLobby|enterRematchLobby|startGame|beginCountdown|cancelCountdown|confirmElimination|beginEliminationSelection|cycleEliminationTarget|cancelEliminationSelection|clearPendingPass|cancelPendingPassForModule|requestPass)\s*\(",
    ]
    for pattern in forbidden:
        assert not re.search(pattern, body), f"{name} bypasses dispatcher: {pattern}"
print(f"PASS: {len(adapters)} adapters have no direct canonical mutations or transition calls")
