"""Logical TurnHub player seats."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class PlayerSeat:
    """One logical player assigned to a physical module seat."""

    player_number: int
    module_id: int
    slot: int = 1

    @property
    def slot_name(self) -> str:
        return "A" if self.slot == 1 else "B"

    @property
    def seat_key(self) -> tuple[int, int]:
        return (self.module_id, self.slot)

    def label(self, include_slot: bool = True) -> str:
        slot = self.slot_name if include_slot else ""
        return (
            f"Player {self.player_number} / "
            f"Module {self.module_id}{slot}"
        )
