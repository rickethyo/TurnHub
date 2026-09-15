"""All TurnHub LED rendering."""

from __future__ import annotations

import math
import threading
import time

from config import (
    BREATHE_MAX,
    BREATHE_MIN,
    BREATHE_PERIOD_SECONDS,
    LOBBY_FLASH_GAP,
    LOBBY_FLASH_OFF,
    LOBBY_FLASH_ON,
    LOBBY_IDLE_LED_TIME,
    MODULE_IDS,
    NEW_TURN_FLASH_INTERVAL,
    NEW_TURN_FLASH_SECONDS,
    START_COUNTDOWN_FLASH_TIME,
    STATE_GAME_OVER,
    STATE_PAUSED,
    STATE_RUNNING,
    WARNING_BLINK_INTERVAL,
)

from game_engine import GameEngine
from lobby import Lobby
from serial_controller import SerialController


_FEEDBACK_STEP = 0.12


class LEDController:
    def __init__(
        self,
        serial_controller: SerialController,
    ) -> None:

        self.serial = serial_controller

        self.cache: dict[
            tuple[str, int],
            int | bool,
        ] = {}

        # module -> (effect name, monotonic start time)
        self._feedback: dict[int, tuple[str, float]] = {}
        self._feedback_lock = threading.Lock()

    # ========================================================
    # Basic LED Control
    # ========================================================

    def clear_cache(self) -> None:
        self.cache.clear()

    def _set_cached(
        self,
        kind: str,
        module: int,
        value: int | bool,
    ) -> None:

        key = (kind, module)

        if self.cache.get(key) == value:
            return

        sent = False

        if kind == "BLUE":
            sent = self.serial.set_blue(module, int(value))
        elif kind == "RED":
            sent = self.serial.set_red(module, bool(value))
        elif kind == "GREEN":
            sent = self.serial.set_green(module, bool(value))

        if sent:
            self.cache[key] = value

    def blue(self, module: int, brightness: int) -> None:
        brightness = max(0, min(255, int(brightness)))
        self._set_cached("BLUE", module, brightness)

    def red(self, module: int, on: bool) -> None:
        self._set_cached("RED", module, bool(on))

    def green(self, module: int, on: bool) -> None:
        self._set_cached("GREEN", module, bool(on))

    def off_module(self, module: int) -> None:
        self.blue(module, 0)
        self.red(module, False)
        self.green(module, False)

    def off_all(self) -> None:
        for module in MODULE_IDS:
            self.off_module(module)

    # ========================================================
    # One-shot Feedback Effects
    # ========================================================

    def _start_feedback(self, module: int, effect: str) -> None:
        with self._feedback_lock:
            self._feedback[module] = (
                effect,
                time.monotonic(),
            )

    def shared_player_added(self, module: int) -> None:
        self._start_feedback(module, "SHARED_ADD")

    def shared_player_removed(self, module: int) -> None:
        self._start_feedback(module, "SHARED_REMOVE")

    def same_module_pass(self, module: int) -> None:
        self._start_feedback(module, "SAME_MODULE_PASS")

    def player_eliminated(self, module: int) -> None:
        self._start_feedback(module, "PLAYER_ELIMINATED")

    def clear_feedback(self) -> None:
        with self._feedback_lock:
            self._feedback.clear()

    def _render_feedback(self, module: int, now: float) -> bool:
        with self._feedback_lock:
            data = self._feedback.get(module)

        if data is None:
            return False

        effect, started = data
        elapsed = max(0.0, now - started)
        step = int(elapsed / _FEEDBACK_STEP)

        if effect == "SHARED_ADD":
            # Two green acknowledgements, then blue+green.
            sequence = (
                (0, False, True),
                (0, False, False),
                (0, False, True),
                (0, False, False),
                (255, False, True),
                (0, False, False),
            )
        elif effect == "SHARED_REMOVE":
            # Mirror of add, but red and descending in meaning.
            sequence = (
                (0, True, False),
                (0, False, False),
                (0, True, False),
                (0, False, False),
                (255, True, False),
                (0, False, False),
            )
        elif effect == "PLAYER_ELIMINATED":
            # Three unmistakable red strikes.
            sequence = (
                (0, True, False),
                (0, False, False),
                (0, True, False),
                (0, False, False),
                (0, True, False),
                (0, False, False),
            )
        else:
            # A local hand-off needs to look different from the normal
            # three-second blue new-turn flash.
            sequence = (
                (255, False, False),
                (0, False, False),
                (0, False, True),
                (0, False, False),
                (255, False, False),
            )

        if step >= len(sequence):
            with self._feedback_lock:
                current = self._feedback.get(module)
                if current == data:
                    self._feedback.pop(module, None)
            return False

        blue, red, green = sequence[step]
        self.blue(module, blue)
        self.red(module, red)
        self.green(module, green)
        return True

    # ========================================================
    # LED Effects
    # ========================================================

    @staticmethod
    def breathe_value(now: float) -> int:
        phase = (
            now % BREATHE_PERIOD_SECONDS
        ) / BREATHE_PERIOD_SECONDS

        sine = (
            math.sin(
                phase * 2.0 * math.pi
                - math.pi / 2.0
            )
            + 1.0
        ) / 2.0

        shaped = sine ** 2.2

        return int(
            BREATHE_MIN
            + shaped
            * (BREATHE_MAX - BREATHE_MIN)
        )

    @staticmethod
    def player_number_red_on(
        player_number: int,
        now: float,
    ) -> bool:

        flash_block = LOBBY_FLASH_ON + LOBBY_FLASH_OFF
        sequence_length = (
            player_number * flash_block
            + LOBBY_FLASH_GAP
        )
        position = now % sequence_length

        for index in range(player_number):
            start = index * flash_block
            if start <= position < start + LOBBY_FLASH_ON:
                return True

        return False

    @staticmethod
    def starter_blue_value(
        lobby: Lobby,
        module: int,
        now: float,
    ) -> int:
        """Return blue brightness for the selected starter seat.

        Single-player modules preserve the original solid-blue starter
        indication. On a shared module, Slot A gives one blue pulse and
        Slot B gives two blue pulses in a repeating pattern.
        """

        selected = lobby.selected_starter_player

        if selected is None or selected.module_id != module:
            return 0

        if not lobby.has_secondary(module):
            return 255

        pulse_on = 0.18
        pulse_off = 0.18
        position = now % 1.80

        if selected.slot == 1:
            return 255 if position < pulse_on else 0

        second_start = pulse_on + pulse_off

        if (
            position < pulse_on
            or second_start <= position < second_start + pulse_on
        ):
            return 255

        return 0

    # ========================================================
    # Lobby
    # ========================================================

    def render_lobby(
        self,
        lobby: Lobby,
        now: float | None = None,
    ) -> None:

        if now is None:
            now = time.monotonic()

        selected = lobby.selected_starter_player

        for module in lobby.module_ids:
            if self._render_feedback(module, now):
                continue

            if lobby.is_joined(module):
                primary_number = lobby.player_number(module, slot=1)

                # Blue identifies the starter. Single-player modules
                # remain solid blue. Shared modules use one pulse for
                # Slot A and two pulses for Slot B.
                self.blue(
                    module,
                    self.starter_blue_value(
                        lobby,
                        module,
                        now,
                    ),
                )

                self.green(
                    module,
                    module == lobby.host_module,
                )

                self.red(
                    module,
                    self.player_number_red_on(
                        primary_number,
                        now,
                    )
                    if primary_number is not None
                    else False,
                )
            else:
                self.render_unjoined(module, now)

    def render_unjoined(self, module: int, now: float) -> None:
        cycle_length = LOBBY_IDLE_LED_TIME * 3
        position = now % cycle_length

        if position < LOBBY_IDLE_LED_TIME:
            self.blue(module, 255)
            self.red(module, False)
            self.green(module, False)
        elif position < LOBBY_IDLE_LED_TIME * 2:
            # Lobby idle is intentionally Blue -> Green -> Red. Earlier
            # prototype mapping made this look correct only because red/green
            # were inverted below LEDController.
            self.blue(module, 0)
            self.red(module, False)
            self.green(module, True)
        else:
            self.blue(module, 0)
            self.red(module, True)
            self.green(module, False)

    # ========================================================
    # Starting Countdown
    # ========================================================

    def render_starting(
        self,
        lobby: Lobby,
        countdown_elapsed: float,
        now: float | None = None,
    ) -> None:

        if now is None:
            now = time.monotonic()

        second_index = int(countdown_elapsed)
        within_second = countdown_elapsed - second_index
        flash_on = within_second < START_COUNTDOWN_FLASH_TIME

        for module in lobby.module_ids:
            if lobby.is_joined(module):
                self.blue(module, 255 if flash_on else 0)
                self.red(module, False)
                self.green(module, False)
            else:
                self.render_unjoined(module, now)

    # ========================================================
    # Game Rendering
    # ========================================================

    def render_game(
        self,
        game: GameEngine,
        host_module: int | None,
        now: float | None = None,
        elimination_target_player: int | None = None,
        win_confirmation_player: int | None = None,
    ) -> None:

        if now is None:
            now = time.monotonic()

        if game.state == STATE_GAME_OVER:
            self.render_game_over(game, host_module, now)
            return

        if game.state == STATE_PAUSED:
            breathe = self.breathe_value(now)
            target = game.player_by_number(elimination_target_player)
            win_target = game.player_by_number(win_confirmation_player)

            for module in game.modules:
                if self._render_feedback(module, now):
                    continue

                if win_target is not None and module == win_target.module_id:
                    # Green is the win-confirmation language. On a shared
                    # module, one pulse means Seat A and two means Seat B.
                    living_local = game.living_players_for_module(module)
                    if len(living_local) <= 1:
                        green = True
                    else:
                        pulse_on = 0.18
                        pulse_off = 0.18
                        position = now % 1.80
                        if win_target.slot == 1:
                            green = position < pulse_on
                        else:
                            second_start = pulse_on + pulse_off
                            green = (
                                position < pulse_on
                                or second_start
                                <= position
                                < second_start + pulse_on
                            )

                    self.blue(module, 0)
                    self.red(module, False)
                    self.green(module, green)
                    continue

                if target is not None and module == target.module_id:
                    # Red is the elimination-selection language. A module
                    # with one living seat stays solid red. On a shared
                    # module, Seat A pulses once and Seat B pulses twice.
                    living_local = game.living_players_for_module(module)
                    if len(living_local) <= 1:
                        red = True
                    else:
                        pulse_on = 0.18
                        pulse_off = 0.18
                        position = now % 1.80
                        if target.slot == 1:
                            red = position < pulse_on
                        else:
                            second_start = pulse_on + pulse_off
                            red = (
                                position < pulse_on
                                or second_start
                                <= position
                                < second_start + pulse_on
                            )

                    self.blue(module, 0)
                    self.red(module, red)
                    self.green(module, False)
                    continue

                self.blue(module, breathe)
                self.red(module, False)
                self.green(module, False)
            return

        if game.state != STATE_RUNNING:
            return

        active = game.active_module
        turn_elapsed = game.current_turn_elapsed(now)

        for module in game.modules:
            if self._render_feedback(module, now):
                continue

            if module != active:
                self.blue(module, 255)
                self.red(module, False)
                self.green(module, False)
                continue

            if turn_elapsed < NEW_TURN_FLASH_SECONDS:
                blink_on = (
                    int(
                        turn_elapsed
                        / NEW_TURN_FLASH_INTERVAL
                    )
                    % 2
                    == 0
                )
                self.blue(module, 255 if blink_on else 0)
            else:
                self.blue(module, self.breathe_value(now))

            phase = game.warning_phase(now)

            if phase == "NORMAL":
                self.green(module, False)
                self.red(module, False)
            elif phase in ("CAUTION", "OFF_GREEN"):
                self.green(module, True)
                self.red(module, False)
            elif phase == "WARNING":
                self.green(module, False)
                blink_on = (
                    int(now / WARNING_BLINK_INTERVAL)
                    % 2
                    == 0
                )
                self.red(module, blink_on)

    # ========================================================
    # Game Over
    # ========================================================

    def render_game_over(
        self,
        game: GameEngine,
        host_module: int | None,
        now: float,
    ) -> None:

        winner = game.player_by_number(game.winner_player)

        for module in game.modules:
            blue = 0

            if winner is not None and module == winner.module_id:
                local_players = game.players_for_module(module)

                if len(local_players) == 1:
                    blue = 255
                else:
                    pulse_on = 0.18
                    pulse_off = 0.18
                    position = now % 1.80

                    if winner.slot == 1:
                        blue = 255 if position < pulse_on else 0
                    else:
                        second_start = pulse_on + pulse_off
                        if (
                            position < pulse_on
                            or second_start <= position < second_start + pulse_on
                        ):
                            blue = 255

            self.blue(module, blue)
            self.red(module, False)
            self.green(module, module == host_module)
