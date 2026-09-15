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

    # Life totals are optional web-UI game data. Hardware behavior does not
    # depend on them. starting_life is captured when the game begins so a
    # later settings change only affects future games.
    starting_life: int = 40
    life_totals: dict[int, int] = field(default_factory=dict)

    # Logical players remain in the game record after elimination so player
    # numbers, names, web-controller claims, and statistics stay stable.
    # Order records the elimination sequence.
    eliminated_players: list[int] = field(default_factory=list)

    # A victory claim freezes the game until every other living player
    # confirms. Any one denial cancels the claim and restores the prior
    # running/paused state without charging review time to the clocks.
    win_claim_player: int | None = None
    win_claim_required: list[int] = field(default_factory=list)
    win_claim_confirmed: list[int] = field(default_factory=list)
    win_claim_restore_state: str | None = None

    # ========================================================
    # Game Start
    # ========================================================

    def start(
        self,
        players: list[PlayerSeat],
        starter: PlayerSeat,
        warning_ms: int,
        starting_life: int = 40,
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
        self.eliminated_players = []
        self.win_claim_player = None
        self.win_claim_required = []
        self.win_claim_confirmed = []
        self.win_claim_restore_state = None

        self.stats = {
            player.player_number: PlayerStats()
            for player in self.players
        }

        self.starting_life = int(starting_life)
        self.life_totals = {
            player.player_number: self.starting_life
            for player in self.players
        }

    # ========================================================
    # Life Totals (Web UI)
    # ========================================================

    def life_total(self, player_number: int | None) -> int | None:
        if player_number is None:
            return None
        return self.life_totals.get(player_number)

    def adjust_life(self, player_number: int, delta: int) -> bool:
        """Adjust one logical player's life total.

        Life is deliberately independent from elimination. Reaching zero or
        a negative number never removes a player from turn order.
        """
        if self.state not in (STATE_RUNNING, STATE_PAUSED):
            return False
        if self.has_win_claim:
            return False
        if self.player_by_number(player_number) is None:
            return False
        if self.is_eliminated(player_number):
            return False
        if player_number not in self.life_totals:
            self.life_totals[player_number] = self.starting_life

        new_total = self.life_totals[player_number] + int(delta)
        if abs(new_total) > 1_000_000:
            return False

        self.life_totals[player_number] = new_total
        return True

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

    def living_players_for_module(self, module: int) -> list[PlayerSeat]:
        return [
            player
            for player in self.players_for_module(module)
            if not self.is_eliminated(player.player_number)
        ]

    def is_eliminated(self, player_number: int | None) -> bool:
        if player_number is None:
            return False
        return player_number in self.eliminated_players

    @property
    def living_players(self) -> list[PlayerSeat]:
        return [
            player
            for player in self.players
            if not self.is_eliminated(player.player_number)
        ]

    @property
    def starter_module(self) -> int | None:
        player = self.player_by_number(self.starter_player)
        return player.module_id if player is not None else None

    @property
    def winner_module(self) -> int | None:
        player = self.player_by_number(self.winner_player)
        return player.module_id if player is not None else None

    def _next_living_index(self, start_index: int) -> int | None:
        if not self.players:
            return None

        for offset in range(1, len(self.players) + 1):
            index = (start_index + offset) % len(self.players)
            player = self.players[index]
            if not self.is_eliminated(player.player_number):
                return index

        return None

    def normalize_active_player(self) -> None:
        """Move an invalid/eliminated active index to the next living seat."""
        if not self.players:
            self.active_index = 0
            return

        self.active_index = max(
            0,
            min(self.active_index, len(self.players) - 1),
        )

        active = self.active_player
        if active is not None and not self.is_eliminated(active.player_number):
            return

        next_index = self._next_living_index(self.active_index)
        if next_index is not None:
            self.active_index = next_index

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

        if self.is_eliminated(active.player_number):
            return False

        now = time.monotonic()
        elapsed = max(0.0, now - self.turn_started_at)

        stat = self.stats[active.player_number]
        stat.turns_completed += 1
        stat.total_turn_seconds += elapsed

        next_index = self._next_living_index(self.active_index)
        if next_index is None:
            return False

        self.active_index = next_index

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
        # otherwise use that module's first living seat as the candidate.
        candidates = (
            self.living_players_for_module(armed_by)
            if armed_by is not None
            else []
        )

        if (
            self.active_player is not None
            and self.active_player.module_id == armed_by
            and not self.is_eliminated(self.active_player.player_number)
        ):
            self.win_armed_player = self.active_player.player_number
        elif candidates:
            self.win_armed_player = candidates[0].player_number
        else:
            self.win_armed_player = None

        return True

    def resume(self) -> bool:
        if self.has_win_claim:
            return False

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
    # Victory Claim / Confirmation
    # ========================================================

    @property
    def has_win_claim(self) -> bool:
        return self.win_claim_player is not None

    @property
    def next_win_confirmation_player(self) -> int | None:
        for player_number in self.win_claim_required:
            if player_number not in self.win_claim_confirmed:
                return player_number
        return None

    def _clear_win_claim(self) -> None:
        self.win_claim_player = None
        self.win_claim_required = []
        self.win_claim_confirmed = []
        self.win_claim_restore_state = None

    def begin_win_claim(
        self,
        player_number: int,
        restore_state: str | None = None,
    ) -> bool:
        if self.has_win_claim or self.state == STATE_GAME_OVER:
            return False

        claimant = self.player_by_number(player_number)
        if claimant is None or self.is_eliminated(player_number):
            return False

        if self.state not in (STATE_RUNNING, STATE_PAUSED):
            return False

        prior_state = restore_state or self.state
        if prior_state not in (STATE_RUNNING, STATE_PAUSED):
            prior_state = STATE_PAUSED

        if self.state == STATE_RUNNING:
            if not self.pause():
                return False

        # Physical confirmation walks MODULES around the table, not merely
        # logical player numbers. With temporary shared modules this means a
        # claim from M0/P1 asks every living player on the next physical
        # module first, then wraps back to the other living seat on M0.
        module_order = self.modules
        try:
            claimant_module_index = module_order.index(claimant.module_id)
        except ValueError:
            return False

        confirmation_modules = (
            module_order[claimant_module_index + 1 :]
            + module_order[: claimant_module_index + 1]
        )

        required: list[int] = []
        for module_id in confirmation_modules:
            for player in self.players_for_module(module_id):
                if player.player_number == player_number:
                    continue
                if self.is_eliminated(player.player_number):
                    continue
                required.append(player.player_number)

        # If somehow only one living player remains, finish immediately.
        if not required:
            self._finish_game(player_number)
            return True

        self.win_claim_player = player_number
        self.win_claim_required = required
        self.win_claim_confirmed = []
        self.win_claim_restore_state = prior_state
        self.win_armed_module = None
        self.win_armed_player = None
        return True

    def confirm_win_claim(
        self,
        player_number: int,
    ) -> tuple[bool, bool]:
        """Return (accepted, game_finished)."""
        if not self.has_win_claim:
            return (False, False)

        if player_number not in self.win_claim_required:
            return (False, False)

        if player_number in self.win_claim_confirmed:
            return (False, False)

        if self.is_eliminated(player_number):
            return (False, False)

        self.win_claim_confirmed.append(player_number)

        if all(
            number in self.win_claim_confirmed
            for number in self.win_claim_required
        ):
            winner = self.win_claim_player
            if winner is None:
                return (False, False)
            self._finish_game(winner)
            return (True, True)

        return (True, False)

    def _restore_after_win_claim(self) -> bool:
        if not self.has_win_claim:
            return False

        restore_state = self.win_claim_restore_state
        self._clear_win_claim()

        if restore_state == STATE_RUNNING:
            return self.resume()

        # The game was already paused before the claim. Keep the existing
        # pause timestamp so review time remains excluded from game/turn time.
        self.state = STATE_PAUSED
        return True

    def deny_win_claim(self, player_number: int) -> bool:
        if not self.has_win_claim:
            return False
        if player_number not in self.win_claim_required:
            return False
        if player_number in self.win_claim_confirmed:
            return False
        if self.is_eliminated(player_number):
            return False
        return self._restore_after_win_claim()

    def cancel_win_claim(self, claimant_player: int) -> bool:
        if not self.has_win_claim:
            return False
        if claimant_player != self.win_claim_player:
            return False
        return self._restore_after_win_claim()

    # ========================================================
    # Elimination
    # ========================================================

    def eliminate_player(
        self,
        player_number: int,
        next_warning_ms: int,
    ) -> tuple[bool, bool]:
        """
        Eliminate one logical player while paused.

        Returns (eliminated, game_finished). If the active player is
        eliminated, their incomplete turn is intentionally not counted and
        the next living player starts at 00:00 when the game resumes.
        """

        if self.state != STATE_PAUSED or self.has_win_claim:
            return (False, False)

        player = self.player_by_number(player_number)
        if player is None or self.is_eliminated(player_number):
            return (False, False)

        if len(self.living_players) <= 1:
            return (False, False)

        was_active = (
            self.active_player is not None
            and self.active_player.player_number == player_number
        )

        self.eliminated_players.append(player_number)
        self.win_armed_module = None
        self.win_armed_player = None

        if was_active:
            next_index = self._next_living_index(self.active_index)
            if next_index is not None:
                self.active_index = next_index

            # The new active player's turn should begin only after resume.
            if self.pause_started_at is not None:
                self.turn_started_at = self.pause_started_at
            else:
                self.turn_started_at = time.monotonic()

            self.current_warning_ms = next_warning_ms
            self.warning_logged_for_turn = False

        living = self.living_players
        if len(living) == 1:
            self._finish_game(living[0].player_number)
            return (True, True)

        return (True, False)

    # ========================================================
    # Finish / Declare Winner
    # ========================================================

    def _finish_game(self, winner_player: int) -> None:
        now = time.monotonic()

        self.winner_player = winner_player
        self.game_ended_at = now

        if self.pause_started_at is not None:
            pause_duration = now - self.pause_started_at
            self.total_paused_seconds += pause_duration
            self.turn_started_at += pause_duration

        self.pause_started_at = None
        self.win_armed_module = None
        self.win_armed_player = None
        self._clear_win_claim()
        self.state = STATE_GAME_OVER

    def declare_winner(
        self,
        module: int,
    ) -> bool:
        """Legacy physical WIN entry point now opens a confirmation claim."""
        if self.state != STATE_PAUSED:
            return False
        if module != self.win_armed_module:
            return False
        if self.win_armed_player is None:
            return False
        if self.is_eliminated(self.win_armed_player):
            return False
        return self.begin_win_claim(
            self.win_armed_player,
            restore_state=STATE_RUNNING,
        )
