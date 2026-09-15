"""Compact TurnHub status monitor."""

from __future__ import annotations

from config import (
    STATE_GAME_OVER,
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    STATE_STARTING,
)
from game_log import format_duration
from player import PlayerSeat


class StatusMonitor:
    """Print compact state snapshots only when meaningful data changes."""

    def __init__(self) -> None:
        self.last_snapshot = None

    def build_snapshot(self, turnhub) -> tuple:
        connected = tuple(turnhub.serial.connected_modules())

        lobby_players = tuple(
            (
                player.player_number,
                player.module_id,
                player.slot,
            )
            for player in turnhub.lobby.players
        )

        if turnhub.game.players:
            stats = tuple(
                (
                    player.player_number,
                    turnhub.game.stats[player.player_number].turns_completed,
                )
                for player in turnhub.game.players
            )
        else:
            stats = ()

        selected = turnhub.lobby.selected_starter_player
        active = turnhub.game.active_player

        return (
            turnhub.state,
            connected,
            lobby_players,
            turnhub.lobby.host_module,
            selected.seat_key if selected else None,
            active.player_number if active else None,
            turnhub.game.winner_player,
            stats,
        )

    def update(self, turnhub, force: bool = False) -> None:
        snapshot = self.build_snapshot(turnhub)

        if not force and snapshot == self.last_snapshot:
            return

        self.last_snapshot = snapshot
        self.display(turnhub)

    @staticmethod
    def _player_label(
        player: PlayerSeat | None,
        shared_module: bool = False,
    ) -> str:
        if player is None:
            return "none"

        slot = player.slot_name if shared_module else ""
        return f"P{player.player_number}/M{player.module_id}{slot}"

    @staticmethod
    def _lobby_label(turnhub, player: PlayerSeat) -> str:
        shared = turnhub.lobby.has_secondary(player.module_id)
        return StatusMonitor._player_label(player, shared)

    def display(self, turnhub) -> None:
        connected = (
            ",".join(
                f"M{module}"
                for module in turnhub.serial.connected_modules()
            )
            or "none"
        )

        parts = [
            f"[STATUS] {turnhub.state}",
            f"connected={connected}",
        ]

        if turnhub.state in (STATE_LOBBY, STATE_STARTING):
            players = (
                ",".join(
                    self._lobby_label(turnhub, player)
                    for player in turnhub.lobby.players
                )
                or "none"
            )

            parts.append(f"players={players}")
            parts.append(
                "host="
                + (
                    f"M{turnhub.lobby.host_module}"
                    if turnhub.lobby.host_module is not None
                    else "none"
                )
            )

            starter = turnhub.lobby.selected_starter_player
            parts.append(
                "starter="
                + (
                    self._lobby_label(turnhub, starter)
                    if starter is not None
                    else "none"
                )
            )

        if turnhub.state in (
            STATE_RUNNING,
            STATE_PAUSED,
            STATE_GAME_OVER,
        ):
            if turnhub.state == STATE_GAME_OVER:
                winner = turnhub.game.player_by_number(
                    turnhub.game.winner_player
                )
                parts.append(
                    "winner="
                    + self._player_label(
                        winner,
                        len(turnhub.game.players_for_module(winner.module_id)) > 1
                        if winner is not None
                        else False,
                    )
                )
            else:
                active = turnhub.game.active_player
                parts.append(
                    "active="
                    + self._player_label(
                        active,
                        len(turnhub.game.players_for_module(active.module_id)) > 1
                        if active is not None
                        else False,
                    )
                )

            parts.append(
                "game=" + format_duration(turnhub.game.game_elapsed())
            )

            turns = ",".join(
                (
                    f"P{player.player_number}="
                    f"{turnhub.game.stats[player.player_number].turns_completed}"
                )
                for player in turnhub.game.players
            )

            parts.append(f"turns={turns or 'none'}")

        print(" | ".join(parts))
