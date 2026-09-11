"""All TurnHub LED rendering."""

from __future__ import annotations

import math
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

        key = (
            kind,
            module,
        )

        if self.cache.get(key) == value:
            return

        self.cache[key] = value

        if kind == "BLUE":

            self.serial.set_blue(
                module,
                int(value),
            )

        elif kind == "RED":

            self.serial.set_red(
                module,
                bool(value),
            )

        elif kind == "GREEN":

            self.serial.set_green(
                module,
                bool(value),
            )

    def blue(
        self,
        module: int,
        brightness: int,
    ) -> None:

        brightness = max(
            0,
            min(
                255,
                int(brightness),
            ),
        )

        self._set_cached(
            "BLUE",
            module,
            brightness,
        )

    def red(
        self,
        module: int,
        on: bool,
    ) -> None:

        self._set_cached(
            "RED",
            module,
            bool(on),
        )

    def green(
        self,
        module: int,
        on: bool,
    ) -> None:

        self._set_cached(
            "GREEN",
            module,
            bool(on),
        )

    def off_module(
        self,
        module: int,
    ) -> None:

        self.blue(
            module,
            0,
        )

        self.red(
            module,
            False,
        )

        self.green(
            module,
            False,
        )

    def off_all(self) -> None:

        for module in MODULE_IDS:
            self.off_module(module)

    # ========================================================
    # LED Effects
    # ========================================================

    @staticmethod
    def breathe_value(
        now: float,
    ) -> int:

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
            * (
                BREATHE_MAX
                - BREATHE_MIN
            )
        )

    @staticmethod
    def player_number_red_on(
        player_number: int,
        now: float,
    ) -> bool:

        flash_block = (
            LOBBY_FLASH_ON
            + LOBBY_FLASH_OFF
        )

        sequence_length = (
            player_number
            * flash_block
            + LOBBY_FLASH_GAP
        )

        position = (
            now % sequence_length
        )

        for index in range(
            player_number
        ):

            start = (
                index
                * flash_block
            )

            if (
                start
                <= position
                < start
                + LOBBY_FLASH_ON
            ):
                return True

        return False

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

        for module in lobby.module_ids:

            # ------------------------------------------------
            # Joined Module
            # ------------------------------------------------

            if lobby.is_joined(
                module
            ):

                player_number = (
                    lobby.player_number(
                        module
                    )
                )

                # BLUE:
                # Selected starting player.
                self.blue(
                    module,
                    255
                    if module
                    == lobby.selected_starter
                    else 0,
                )

                # GREEN:
                # Host indicator.
                #
                # This remains solid even while the red
                # player-number LED flashes.
                self.green(
                    module,
                    module
                    == lobby.host_module,
                )

                # RED:
                # Player number indicator.
                #
                # This applies to ALL joined players,
                # including the host.
                #
                # Player 1 = one flash
                # Player 2 = two flashes
                # etc.
                self.red(
                    module,
                    self.player_number_red_on(
                        player_number,
                        now,
                    )
                    if player_number
                    is not None
                    else False,
                )

            # ------------------------------------------------
            # Unjoined Module
            # ------------------------------------------------

            else:

                self.render_unjoined(
                    module,
                    now,
                )

    def render_unjoined(
        self,
        module: int,
        now: float,
    ) -> None:

        cycle_length = (
            LOBBY_IDLE_LED_TIME
            * 3
        )

        position = (
            now
            % cycle_length
        )

        # Blue
        if (
            position
            < LOBBY_IDLE_LED_TIME
        ):

            self.blue(
                module,
                255,
            )

            self.red(
                module,
                False,
            )

            self.green(
                module,
                False,
            )

        # Red
        elif (
            position
            < LOBBY_IDLE_LED_TIME
            * 2
        ):

            self.blue(
                module,
                0,
            )

            self.red(
                module,
                True,
            )

            self.green(
                module,
                False,
            )

        # Green
        else:

            self.blue(
                module,
                0,
            )

            self.red(
                module,
                False,
            )

            self.green(
                module,
                True,
            )

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

        second_index = int(
            countdown_elapsed
        )

        within_second = (
            countdown_elapsed
            - second_index
        )

        flash_on = (
            within_second
            < START_COUNTDOWN_FLASH_TIME
        )

        for module in lobby.module_ids:

            if lobby.is_joined(
                module
            ):

                self.blue(
                    module,
                    255
                    if flash_on
                    else 0,
                )

                self.red(
                    module,
                    False,
                )

                self.green(
                    module,
                    False,
                )

            else:

                self.render_unjoined(
                    module,
                    now,
                )

    # ========================================================
    # Game Rendering
    # ========================================================

    def render_game(
        self,
        game: GameEngine,
        host_module: int | None,
        now: float | None = None,
    ) -> None:

        if now is None:
            now = time.monotonic()

        # ----------------------------------------------------
        # Game Over
        # ----------------------------------------------------

        if (
            game.state
            == STATE_GAME_OVER
        ):

            self.render_game_over(
                game,
                host_module,
            )

            return

        # ----------------------------------------------------
        # Paused
        # ----------------------------------------------------

        if (
            game.state
            == STATE_PAUSED
        ):

            breathe = (
                self.breathe_value(
                    now
                )
            )

            for module in game.players:

                self.blue(
                    module,
                    breathe,
                )

                self.red(
                    module,
                    False,
                )

                self.green(
                    module,
                    False,
                )

            return

        # ----------------------------------------------------
        # Running
        # ----------------------------------------------------

        if (
            game.state
            != STATE_RUNNING
        ):
            return

        active = (
            game.active_module
        )

        turn_elapsed = (
            game.current_turn_elapsed(
                now
            )
        )

        for module in game.players:

            # --------------------------------------------
            # Waiting Players
            # --------------------------------------------

            if module != active:

                self.blue(
                    module,
                    255,
                )

                self.red(
                    module,
                    False,
                )

                self.green(
                    module,
                    False,
                )

                continue

            # --------------------------------------------
            # Active Player Blue LED
            # --------------------------------------------

            if (
                turn_elapsed
                < NEW_TURN_FLASH_SECONDS
            ):

                blink_on = (
                    int(
                        turn_elapsed
                        / NEW_TURN_FLASH_INTERVAL
                    )
                    % 2
                    == 0
                )

                self.blue(
                    module,
                    255
                    if blink_on
                    else 0,
                )

            else:

                self.blue(
                    module,
                    self.breathe_value(
                        now
                    ),
                )

            # --------------------------------------------
            # Warning State
            # --------------------------------------------

            phase = (
                game.warning_phase(
                    now
                )
            )

            # Normal turn
            if phase == "NORMAL":

                self.green(
                    module,
                    False,
                )

                self.red(
                    module,
                    False,
                )

            # 75% threshold or timer-disabled
            # five-minute indicator
            elif phase in (
                "CAUTION",
                "OFF_GREEN",
            ):

                self.green(
                    module,
                    True,
                )

                self.red(
                    module,
                    False,
                )

            # Warning threshold reached
            elif phase == "WARNING":

                self.green(
                    module,
                    False,
                )

                blink_on = (
                    int(
                        now
                        / WARNING_BLINK_INTERVAL
                    )
                    % 2
                    == 0
                )

                self.red(
                    module,
                    blink_on,
                )

    # ========================================================
    # Game Over
    # ========================================================

    def render_game_over(
        self,
        game: GameEngine,
        host_module: int | None,
    ) -> None:

        for module in game.players:

            # Winner gets blue.
            self.blue(
                module,
                255
                if module
                == game.winner_module
                else 0,
            )

            self.red(
                module,
                False,
            )

            # Host gets green.
            self.green(
                module,
                module
                == host_module,
            )