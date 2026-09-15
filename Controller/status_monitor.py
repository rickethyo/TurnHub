"""Compact TurnHub status monitor."""

from __future__ import annotations

from config import (
    MODULE_IDS,
    STATE_GAME_OVER,
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    STATE_STARTING,
)
from game_log import format_duration


class StatusMonitor:
    """Print compact state snapshots only when meaningful data changes."""

    def __init__(self) -> None:
        self.last_snapshot = None

    def build_snapshot(
        self,
        turnhub,
    ) -> tuple:

        connected = tuple(
            turnhub.serial.connected_modules()
        )

        joined = tuple(
            turnhub.lobby.player_modules
        )

        if turnhub.game.players:
            stats = tuple(
                (
                    module,
                    turnhub.game.stats[module]
                    .turns_completed,
                )
                for module in turnhub.game.players
            )
        else:
            stats = ()

        return (
            turnhub.state,
            connected,
            joined,
            turnhub.lobby.host_module,
            turnhub.lobby.selected_starter,
            turnhub.game.active_module,
            turnhub.game.winner_module,
            stats,
        )

    def update(
        self,
        turnhub,
        force: bool = False,
    ) -> None:

        snapshot = self.build_snapshot(
            turnhub
        )

        if (
            not force
            and snapshot == self.last_snapshot
        ):
            return

        self.last_snapshot = snapshot
        self.display(turnhub)

    @staticmethod
    def _label(
        turnhub,
        module: int | None,
    ) -> str:

        if module is None:
            return "none"

        player_number = (
            turnhub.lobby.player_number(module)
        )

        if player_number is None:
            return f"M{module}"

        return f"P{player_number}/M{module}"

    def display(
        self,
        turnhub,
    ) -> None:

        connected = (
            ",".join(
                f"M{module}"
                for module
                in turnhub.serial.connected_modules()
            )
            or "none"
        )

        parts = [
            f"[STATUS] {turnhub.state}",
            f"connected={connected}",
        ]

        if turnhub.state in (
            STATE_LOBBY,
            STATE_STARTING,
        ):

            players = (
                ",".join(
                    self._label(
                        turnhub,
                        module,
                    )
                    for module
                    in turnhub.lobby.player_modules
                )
                or "none"
            )

            parts.append(
                f"players={players}"
            )

            parts.append(
                "host="
                + self._label(
                    turnhub,
                    turnhub.lobby.host_module,
                )
            )

            parts.append(
                "starter="
                + self._label(
                    turnhub,
                    turnhub.lobby.selected_starter,
                )
            )

        if turnhub.state in (
            STATE_RUNNING,
            STATE_PAUSED,
            STATE_GAME_OVER,
        ):

            if turnhub.state == STATE_GAME_OVER:
                parts.append(
                    "winner="
                    + self._label(
                        turnhub,
                        turnhub.game.winner_module,
                    )
                )
            else:
                parts.append(
                    "active="
                    + self._label(
                        turnhub,
                        turnhub.game.active_module,
                    )
                )

            parts.append(
                "game="
                + format_duration(
                    turnhub.game.game_elapsed()
                )
            )

            turns = ",".join(
                (
                    f"P{index}="
                    f"{turnhub.game.stats[module].turns_completed}"
                )
                for index, module in enumerate(
                    turnhub.game.players,
                    start=1,
                )
            )

            parts.append(
                f"turns={turns or 'none'}"
            )

        print(" | ".join(parts))
