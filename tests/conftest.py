"""Shared pytest configuration for TurnHub tests."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTROLLER = ROOT / "Controller"

if str(CONTROLLER) not in sys.path:
    sys.path.insert(0, str(CONTROLLER))
