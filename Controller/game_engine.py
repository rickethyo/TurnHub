"""TurnHub game timing and game-state logic."""

from __future__ import annotations

import time

from dataclasses import dataclass, field

from config import (
    STATE_GAME_OVER,
    STATE_PAUSED,
    STATE_RUNNING,
    WARNING_CAUTION_FRACTION,
    WARNING_OFF,
    WARNING_OFF_GREEN_MS,
)
from player import PlayerSeat


@dataclass
class PlayerStats:
    turns_completed: int = 0
    total_turn_seconds: float = 0.0


@dataclass
class GameEngine:
    players: list[PlayerSeat] = field(default_factory=list)

    active_index: int = 0
    starter_player: int | None = None
    state: str = STATE_RUNNING

    game_started_at: float = 0.0
    game_ended_at: float | None = None

    turn_started_at: float = 0.0
    pause_started_at: float | None = None

    total_paused_seconds: float = 0.0

    current_warning_ms: int = WARNING_OFF

    winner_player: int | None = None
    win_armed_module: int | None = None
    win_armed_player: int | None = None

    warning_logged_for_turn: bool = False

    stats: dict[int, PlayerStats] = field(default_factory=dict)

    # ========================================================
    # Game Start
    # ========================================================

    def start(
        self,
        players: list[PlayerSeat],
        starter: PlayerSeat,
        warning_ms: int,
    ) -> None:

        self.players = list(players)

        self.active_index = next(
            index
            for index, player in enumerate(self.players)
            if player.seat_key == starter.seat_key
        )

        self.starter_player = (
            self.players[self.active_index].player_number
        )
        self.state = STATE_RUNNING

        now = time.monotonic()

        self.game_started_at = now
        self.game_ended_at = None
        self.turn_started_at = now
        self.pause_started_at = None
        self.total_paused_seconds = 0.0
        self.current_warning_ms = warning_ms
        self.winner_player = None
        self.win_armed_module = None
        self.win_armed_player = None
        self.warning_logged_for_turn = False

        self.stats = {
            player.player_number: PlayerStats()
            for player in self.players
        }

    # ========================================================
    # Player / Module Lookup
    # ========================================================

    @property
    def active_player(self) -> PlayerSeat | None:
        if not self.players:
            return None
        return self.players[self.active_index]

    @property
    def active_module(self) -> int | None:
        player = self.active_player
        return player.module_id if player is not None else None

    @property
    def active_player_number(self) -> int | None:
        player = self.active_player
        return player.player_number if player is not None else None

    @property
    def modules(self) -> list[int]:
        result: list[int] = []
        for player in self.players:
            if player.module_id not in result:
                result.append(player.module_id)
        return result

    def player_by_number(
        self,
        player_number: int | None,
    ) -> PlayerSeat | None:
        if player_number is None:
            return None

        for player in self.players:
            if player.player_number == player_number:
                return player
        return None

    def players_for_module(self, module: int) -> list[PlayerSeat]:
        return [
            player
            for player in self.players
            if player.module_id == module
        ]

    @property
    def starter_module(self) -> int | None:
        player = self.player_by_number(self.starter_player)
        return player.module_id if player is not None else None

    @property
    def winner_module(self) -> int | None:
        player = self.player_by_number(self.winner_player)
        return player.module_id if player is not None else None

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

        return max(0.0, end - self.turn_started_at)

    # ========================================================
    # Game Timing
    # ========================================================

    def game_elapsed(
        self,
        now: float | None = None,
    ) -> float:

        if now is None:
            now = time.monotonic()

        end = (
            self.game_ended_at
            if self.game_ended_at is not None
            else now
        )

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

        elapsed_ms = self.current_turn_elapsed(now) * 1000.0

        if self.current_warning_ms == WARNING_OFF:
            if elapsed_ms >= WARNING_OFF_GREEN_MS:
                return "OFF_GREEN"
            return "NORMAL"

        if elapsed_ms >= self.current_warning_ms:
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

        active = self.active_player
        if active is None or module != active.module_id:
            return False

        now = time.monotonic()
        elapsed = max(0.0, now - self.turn_started_at)

        stat = self.stats[active.player_number]
        stat.turns_completed += 1
        stat.total_turn_seconds += elapsed

        self.active_index = (
            self.active_index + 1
        ) % len(self.players)

        self.turn_started_at = now
        self.current_warning_ms = next_warning_ms
        self.warning_logged_for_turn = False
        self.win_armed_module = None
        self.win_armed_player = None

        return True

    # ========================================================
    # Pause / Resume
    # ========================================================

    def pause(
        self,
        armed_by: int | None = None,
    ) -> bool:

        if self.state != STATE_RUNNING:
            return False

        self.state = STATE_PAUSED
        self.pause_started_at = time.monotonic()
        self.win_armed_module = armed_by

        # A shared physical module cannot tell which person held
        # the button. Prefer its currently active logical player;
        # otherwise use that module's first seat as the candidate.
        candidates = self.players_for_module(armed_by) if armed_by is not None else []

        if (
            self.active_player is not None
            and self.active_player.module_id == armed_by
        ):
            self.win_armed_player = self.active_player.player_number
        elif candidates:
            self.win_armed_player = candidates[0].player_number
        else:
            self.win_armed_player = None

        return True

    def resume(self) -> bool:
        if (
            self.state != STATE_PAUSED
            or self.pause_started_at is None
        ):
            return False

        now = time.monotonic()
        pause_duration = now - self.pause_started_at

        self.total_paused_seconds += pause_duration
        self.turn_started_at += pause_duration

        self.pause_started_at = None
        self.win_armed_module = None
        self.win_armed_player = None
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

        if self.win_armed_player is None:
            return False

        now = time.monotonic()

        self.winner_player = self.win_armed_player
        self.game_ended_at = now

        if self.pause_started_at is not None:
            pause_duration = now - self.pause_started_at
            self.total_paused_seconds += pause_duration
            self.turn_started_at += pause_duration

        self.pause_started_at = None
        self.win_armed_module = None
        self.win_armed_player = None
        self.state = STATE_GAME_OVER

        return True
