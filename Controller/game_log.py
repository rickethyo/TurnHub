"""Persistent TurnHub game statistics."""

from __future__ import annotations

from datetime import datetime
from pathlib import Path

from game_engine import GameEngine
from player import PlayerSeat


def format_duration(seconds: float) -> str:
    """Format elapsed seconds as minutes:seconds."""

    total_seconds = max(0, int(seconds + 0.5))
    minutes = total_seconds // 60
    remaining = total_seconds % 60
    return f"{minutes:02d}:{remaining:02d}"


class GameLogWriter:
    """Writes one human-readable text file per completed game."""

    def __init__(
        self,
        statistics_directory: Path | None = None,
    ) -> None:

        if statistics_directory is None:
            statistics_directory = (
                Path(__file__).resolve().parent.parent
                / "statistics"
            )

        self.statistics_directory = Path(statistics_directory)

    @staticmethod
    def _player_label(
        game: GameEngine,
        player_number: int | None,
    ) -> str:

        player = game.player_by_number(player_number)
        if player is None:
            return "None"

        return player.label(include_slot=True)

    def _unique_path(
        self,
        timestamp: datetime,
    ) -> Path:

        stem = timestamp.strftime("game_%Y-%m-%d_%H-%M-%S")
        path = self.statistics_directory / f"{stem}.txt"
        suffix = 2

        while path.exists():
            path = self.statistics_directory / f"{stem}_{suffix}.txt"
            suffix += 1

        return path

    def write(self, game: GameEngine) -> Path:
        """Write a completed game's statistics and return the path."""

        self.statistics_directory.mkdir(
            parents=True,
            exist_ok=True,
        )

        timestamp = datetime.now()
        path = self._unique_path(timestamp)

        lines = [
            "TurnHub Game Log",
            "================",
            "Saved: " + timestamp.strftime("%Y-%m-%d %I:%M:%S %p"),
            "Starting player: "
            + self._player_label(game, game.starter_player),
            "Winner: "
            + self._player_label(game, game.winner_player),
            "Game time: "
            + format_duration(game.game_elapsed()),
            "Paused time: "
            + format_duration(game.total_paused_seconds),
            "",
            "Player Statistics",
            "-----------------",
        ]

        for player in game.players:
            stats = game.stats[player.player_number]

            lines.extend(
                [
                    player.label(include_slot=True),
                    "  Completed turns: "
                    f"{stats.turns_completed}",
                    "  Completed-turn time: "
                    + format_duration(stats.total_turn_seconds),
                ]
            )

            if stats.turns_completed > 0:
                average = (
                    stats.total_turn_seconds
                    / stats.turns_completed
                )
                lines.append(
                    "  Average completed turn: "
                    + format_duration(average)
                )

            lines.append("")

        if (
            game.game_ended_at is not None
            and game.active_player is not None
        ):
            lines.extend(
                [
                    "Final partial turn",
                    "------------------",
                    game.active_player.label(include_slot=True)
                    + ": "
                    + format_duration(game.current_turn_elapsed()),
                    (
                        "This partial turn is not included "
                        "in completed-turn totals."
                    ),
                    "",
                ]
            )

        path.write_text("\n".join(lines), encoding="utf-8")
        return path
