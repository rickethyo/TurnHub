"""TurnHub game timing and game-state logic."""

from __future__ import annotations

import time

from dataclasses import (
    dataclass,
    field,
)

from config import (
    STATE_GAME_OVER,
    STATE_PAUSED,
    STATE_RUNNING,
    WARNING_CAUTION_FRACTION,
    WARNING_OFF,
    WARNING_OFF_GREEN_MS,
)


@dataclass
class PlayerStats:
    turns_completed: int = 0
    total_turn_seconds: float = 0.0


@dataclass
class GameEngine:
    players: list[int] = field(
        default_factory=list
    )

    active_index: int = 0
    state: str = STATE_RUNNING

    game_started_at: float = 0.0
    game_ended_at: float | None = None

    turn_started_at: float = 0.0
    pause_started_at: float | None = None

    total_paused_seconds: float = 0.0
    total_hardware_downtime: float = 0.0

    current_warning_ms: int = WARNING_OFF

    winner_module: int | None = None
    win_armed_module: int | None = None

    warning_logged_for_turn: bool = False

    stats: dict[int, PlayerStats] = field(
        default_factory=dict
    )

    def start(
        self,
        players: list[int],
        starter: int,
        warning_ms: int,
    ) -> None:

        self.players = list(players)

        self.active_index = (
            self.players.index(starter)
        )

        self.state = STATE_RUNNING

        now = time.monotonic()

        self.game_started_at = now
        self.game_ended_at = None

        self.turn_started_at = now
        self.pause_started_at = None

        self.total_paused_seconds = 0.0
        self.total_hardware_downtime = 0.0

        self.current_warning_ms = warning_ms

        self.winner_module = None
        self.win_armed_module = None

        self.warning_logged_for_turn = False

        self.stats = {
            module: PlayerStats()
            for module in self.players
        }

    @property
    def active_module(
        self,
    ) -> int | None:

        if not self.players:
            return None

        return self.players[
            self.active_index
        ]

    def current_turn_elapsed(
        self,
        now: float | None = None,
    ) -> float:

        if now is None:
            now = time.monotonic()

        end = now

        if (
            self.state == STATE_PAUSED
            and self.pause_started_at is not None
        ):
            end = self.pause_started_at

        elif (
            self.state == STATE_GAME_OVER
            and self.game_ended_at is not None
        ):
            end = self.game_ended_at

        return max(
            0.0,
            end - self.turn_started_at,
        )

    def game_elapsed(
        self,
        now: float | None = None,
    ) -> float:

        if now is None:
            now = time.monotonic()

        if self.game_ended_at is not None:
            end = self.game_ended_at
        else:
            end = now

        return max(
            0.0,
            end
            - self.game_started_at
            - self.total_paused_seconds,
        )

    def warning_phase(
        self,
        now: float | None = None,
    ) -> str:

        elapsed_ms = (
            self.current_turn_elapsed(now)
            * 1000.0
        )

        # ----------------------------------------------------
        # Warning OFF
        # ----------------------------------------------------
        #
        # No red warning is ever generated.
        #
        # After five minutes, the active player's green LED
        # turns on and remains on.
        # ----------------------------------------------------

        if (
            self.current_warning_ms
            == WARNING_OFF
        ):

            if (
                elapsed_ms
                >= WARNING_OFF_GREEN_MS
            ):
                return "OFF_GREEN"

            return "NORMAL"

        # ----------------------------------------------------
        # Normal warning
        # ----------------------------------------------------

        if (
            elapsed_ms
            >= self.current_warning_ms
        ):
            return "WARNING"

        caution_time = (
            self.current_warning_ms
            * WARNING_CAUTION_FRACTION
        )

        if elapsed_ms >= caution_time:
            return "CAUTION"

        return "NORMAL"

    def pass_turn(
        self,
        module: int,
        next_warning_ms: int,
    ) -> bool:

        if self.state != STATE_RUNNING:
            return False

        if module != self.active_module:
            return False

        now = time.monotonic()

        elapsed = max(
            0.0,
            now - self.turn_started_at,
        )

        stat = self.stats[module]

        stat.turns_completed += 1
        stat.total_turn_seconds += elapsed

        self.active_index = (
            self.active_index + 1
        ) % len(self.players)

        self.turn_started_at = now

        # The warning setting is locked at the beginning
        # of each new turn.
        self.current_warning_ms = (
            next_warning_ms
        )

        self.warning_logged_for_turn = False
        self.win_armed_module = None

        return True

    def pause(
        self,
        armed_by: int | None = None,
    ) -> bool:

        if self.state != STATE_RUNNING:
            return False

        self.state = STATE_PAUSED

        self.pause_started_at = (
            time.monotonic()
        )

        self.win_armed_module = armed_by

        return True

    def resume(self) -> bool:

        if (
            self.state != STATE_PAUSED
            or self.pause_started_at is None
        ):
            return False

        now = time.monotonic()

        pause_duration = (
            now - self.pause_started_at
        )

        self.total_paused_seconds += (
            pause_duration
        )

        self.turn_started_at += (
            pause_duration
        )

        self.pause_started_at = None
        self.win_armed_module = None

        self.state = STATE_RUNNING

        return True

    def declare_winner(
        self,
        module: int,
    ) -> bool:

        if self.state != STATE_PAUSED:
            return False

        if module != self.win_armed_module:
            return False

        if module not in self.players:
            return False

        now = time.monotonic()

        self.winner_module = module
        self.game_ended_at = now

        if self.pause_started_at is not None:

            pause_duration = (
                now - self.pause_started_at
            )

            self.total_paused_seconds += (
                pause_duration
            )

            self.turn_started_at += (
                pause_duration
            )

        self.pause_started_at = None
        self.win_armed_module = None

        self.state = STATE_GAME_OVER

        return True

    def freeze_for_hardware_loss(
        self,
    ) -> dict:

        return {
            "was_running": (
                self.state == STATE_RUNNING
            ),
            "was_paused": (
                self.state == STATE_PAUSED
            ),
            "disconnect_at": (
                time.monotonic()
            ),
        }

    def restore_after_hardware_loss(
        self,
        outage: dict,
    ) -> None:

        now = time.monotonic()

        downtime = max(
            0.0,
            now - outage["disconnect_at"],
        )

        self.total_hardware_downtime += (
            downtime
        )

        # Exclude hardware downtime from game timing.

        self.game_started_at += downtime
        self.turn_started_at += downtime

        if self.pause_started_at is not None:
            self.pause_started_at += downtime

        # After reconnection the game always returns
        # in a paused state and requires a deliberate
        # long press to continue.

        if self.state in (
            STATE_RUNNING,
            STATE_PAUSED,
        ):

            self.state = STATE_PAUSED
            self.pause_started_at = now
            self.win_armed_module = None