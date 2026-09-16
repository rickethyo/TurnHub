"""Runtime platform detection for TurnHub.

TurnHub's game engine is platform-neutral.  This module decides whether the
physical Atlas/Sigil adapter may be enabled.  Production Atlas authentication
will eventually be backed by an offline-verifiable device identity stored in a
secure element.  Until that hardware exists, the prototype can be explicitly
started in development-atlas mode.
"""
from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class PlatformIdentity:
    mode: str
    hardware_enabled: bool
    official_atlas: bool
    development: bool
    reason: str

    @property
    def display_name(self) -> str:
        if self.official_atlas:
            return "Atlas"
        if self.development:
            return "Atlas development mode"
        return "TurnHub Virtual"


def detect_platform() -> PlatformIdentity:
    """Select the runtime adapter without making the game engine hardware-aware.

    TURNHUB_MODE values:
      virtual            Always use browser/virtual controls only.
      development-atlas  Enable prototype physical hardware explicitly.
      auto               Reserved for production Atlas identity verification.

    The default is development-atlas for the current prototype so this change
    does not unexpectedly disable Ricky's existing bench hardware.  Before a
    public software release, the packaging default should become ``auto`` (or
    ``virtual``) and only a cryptographically verified Atlas should enable the
    physical adapter.
    """
    requested = os.getenv("TURNHUB_MODE", "development-atlas").strip().lower()

    if requested in {"virtual", "software"}:
        return PlatformIdentity(
            mode="virtual",
            hardware_enabled=False,
            official_atlas=False,
            development=False,
            reason="Virtual mode selected; physical Atlas/Sigil I/O is disabled.",
        )

    if requested in {"development-atlas", "dev-atlas", "hardware"}:
        return PlatformIdentity(
            mode="development-atlas",
            hardware_enabled=True,
            official_atlas=False,
            development=True,
            reason="Prototype hardware enabled by explicit development mode.",
        )

    # Production hook.  Do NOT silently treat a Raspberry Pi, serial number,
    # MAC address, filesystem marker, or editable config file as an Atlas.
    # Those identifiers are cloneable.  ``auto`` therefore fails safely into
    # Virtual Mode until secure-element certificate verification is added.
    return PlatformIdentity(
        mode="virtual",
        hardware_enabled=False,
        official_atlas=False,
        development=False,
        reason="No verified Atlas device identity was found; using Virtual Mode.",
    )
