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

    current_warning_ms: int = WARNING_OFF

    winner_module: int | None = None
    win_armed_module: int | None = None

    warning_logged_for_turn: bool = False

    stats: dict[int, PlayerStats] = field(
        default_factory=dict
    )

    # ========================================================
    # Game Start
    # ========================================================

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

        self.current_warning_ms = warning_ms

        self.winner_module = None
        self.win_armed_module = None

        self.warning_logged_for_turn = False

        self.stats = {
            module: PlayerStats()
            for module in self.players
        }

    # ========================================================
    # Active Player
    # ========================================================

    @property
    def active_module(
        self,
    ) -> int | None:

        if not self.players:
            return None

        return self.players[
            self.active_index
        ]

    # ========================================================
    # Turn Timing
    # ========================================================

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

    # ========================================================
    # Game Timing
    # ========================================================

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

    # ========================================================
    # Warning State
    # ========================================================

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
        # No red warning is generated.
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
        # Normal Warning
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

    # ========================================================
    # Pass Turn
    # ========================================================

    def pass_turn(
        self,
        module: int,
        next_warning_ms: int,
    ) -> bool:

        if self.state != STATE_RUNNING:
            return False

        # Only the currently active player's physical module
        # may pass the turn through the normal player-module
        # interface.
        #
        # A future hub-level master pass command can bypass
        # this check through a separate method.

        if module != self.active_module:
            return False

        if not self.players:
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

        # Warning configuration is locked at the beginning
        # of each new turn.
        self.current_warning_ms = (
            next_warning_ms
        )

        self.warning_logged_for_turn = False
        self.win_armed_module = None

        return True

    # ========================================================
    # Pause
    # ========================================================

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

    # ========================================================
    # Resume
    # ========================================================

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

        # Move the turn start forward by the duration of the
        # pause so paused time does not count against the
        # active player's turn.

        self.turn_started_at += (
            pause_duration
        )

        self.pause_started_at = None
        self.win_armed_module = None

        self.state = STATE_RUNNING

        return True

    # ========================================================
    # Declare Winner
    # ========================================================

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