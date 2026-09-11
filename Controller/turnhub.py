"""TurnHub main application."""

from __future__ import annotations

import time

from audio import AudioController

from config import (
    DEFAULT_WARNING_MS,
    LOOP_SLEEP_SECONDS,
    MODULE_IDS,
    START_COUNTDOWN_SECONDS,
    STATE_GAME_OVER,
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    STATE_STARTING,
    WARNING_OFF,
)

from game_engine import GameEngine
from leds import LEDController
from lobby import Lobby
from serial_controller import SerialController
from status_monitor import StatusMonitor


def warning_description(
    warning_ms: int,
) -> str:

    if warning_ms == WARNING_OFF:
        return "OFF (green at 5 minutes)"

    seconds = warning_ms // 1000

    if seconds < 60:
        return f"{seconds} seconds"

    minutes = seconds // 60

    if minutes == 1:
        return "1 minute"

    return f"{minutes} minutes"


class TurnHub:
    def __init__(self) -> None:

        # SerialController receives messages in its
        # background reader threads and forwards them here.
        self.serial = SerialController(
            message_callback=self.handle_serial_line
        )

        # Audio is now hub-level hardware connected directly
        # to the Raspberry Pi.
        self.audio = AudioController(
            self.serial
        )

        self.leds = LEDController(
            self.serial
        )

        self.lobby = Lobby()
        self.game = GameEngine()

        # Development status monitor.
        self.status = StatusMonitor()

        self.state = STATE_LOBBY

        # Warning threshold comes from config.py.
        # During prototype testing this can be set very low,
        # such as 10 seconds.
        self.warning_ms = DEFAULT_WARNING_MS

        self.start_countdown_at: float | None = None
        self.last_countdown_tone = -1

    # ========================================================
    # Serial Protocol
    # ========================================================

    def handle_serial_line(
        self,
        line: str,
    ) -> None:

        if not line:
            return

        parts = line.split("|")
        event = parts[0].upper()

        # ----------------------------------------------------
        # Module Ready
        # READY|module
        # ----------------------------------------------------

        if event == "READY":

            if len(parts) < 2:
                return

            try:
                module = int(parts[1])

            except ValueError:
                return

            if module not in MODULE_IDS:
                return

            print(
                f"[TURNHUB] Module {module} ready."
            )

            self.leds.clear_cache()

            return

        # ----------------------------------------------------
        # Dedicated Pass Button
        # PASS|module
        # ----------------------------------------------------

        if event == "PASS":

            if len(parts) < 2:
                return

            try:
                module = int(parts[1])

            except ValueError:
                return

            if module not in MODULE_IDS:
                return

            self.on_pass(
                module
            )

            return

        # ----------------------------------------------------
        # Action Button
        # ACTION|module|DOWN
        # ACTION|module|UP
        # ACTION|module|SHORT
        # ACTION|module|LONG
        # ACTION|module|WIN
        # ----------------------------------------------------

        if event == "ACTION":

            if len(parts) < 3:
                return

            try:
                module = int(parts[1])

            except ValueError:
                return

            if module not in MODULE_IDS:
                return

            action = parts[2].upper()

            if action == "DOWN":

                self.on_action_down(
                    module
                )

            elif action == "UP":

                self.on_action_up(
                    module
                )

            elif action == "SHORT":

                self.on_action_short(
                    module
                )

            elif action == "LONG":

                self.on_action_long(
                    module
                )

            elif action == "WIN":

                self.on_action_win(
                    module
                )

            return

        print(
            f"[SERIAL] Unknown: {line}"
        )

    # ========================================================
    # Dedicated Pass Button
    # ========================================================

    def on_pass(
        self,
        module: int,
    ) -> None:

        # PASS has exactly one job:
        # pass the active player's turn during a running game.

        if self.state != STATE_RUNNING:

            print(
                f"[GAME] PASS from module {module} "
                f"ignored while state is {self.state}."
            )

            return

        if self.game.pass_turn(
            module,
            self.warning_ms,
        ):

            print(
                f"[GAME] Module {module} "
                "passed turn."
            )

            print(
                "[GAME] Active module: "
                f"{self.game.active_module}, "
                "warning locked at "
                f"{warning_description(self.game.current_warning_ms)}"
            )

            self.audio.turn_pass()

        else:

            print(
                f"[GAME] PASS from module "
                f"{module} ignored."
            )

    # ========================================================
    # Action Button Tracking
    # ========================================================

    def on_action_down(
        self,
        module: int,
    ) -> None:

        self.lobby.held_modules.add(
            module
        )

        if self.state == STATE_STARTING:

            print(
                "[LOBBY] Countdown cancelled "
                "by action button press."
            )

            self.cancel_countdown()

    def on_action_up(
        self,
        module: int,
    ) -> None:

        self.lobby.held_modules.discard(
            module
        )

        if (
            self.state == STATE_LOBBY
            and self.lobby.start_armed_by == module
            and module == self.lobby.host_module
        ):

            print(
                "[LOBBY] Host released. "
                "Starting countdown."
            )

            self.lobby.start_armed_by = None

            self.begin_countdown()

    # ========================================================
    # Action Short Press
    # ========================================================

    def on_action_short(
        self,
        module: int,
    ) -> None:

        if self.state == STATE_LOBBY:

            self.handle_lobby_short(
                module
            )

            return

        # During an active game, ACTION SHORT currently has
        # no game function. Passing is handled exclusively
        # by the dedicated PASS button.

        if self.state == STATE_RUNNING:

            print(
                f"[GAME] ACTION SHORT from "
                f"module {module} ignored."
            )

            return

        if self.state == STATE_PAUSED:

            print(
                f"[GAME] ACTION SHORT from "
                f"module {module} ignored."
            )

            return

        if self.state == STATE_GAME_OVER:

            if module == self.lobby.host_module:

                print(
                    "[GAME] Host requested rematch."
                )

                self.enter_rematch_lobby()

            else:

                print(
                    "[GAME] Non-host post-game "
                    "ACTION SHORT ignored."
                )

    def handle_lobby_short(
        self,
        module: int,
    ) -> None:

        if not self.lobby.is_joined(
            module
        ):

            player_number = (
                self.lobby.join(module)
            )

            print(
                "[LOBBY] Physical module "
                f"{module} joined as "
                f"Player {player_number}."
            )

            if player_number == 1:

                print(
                    f"[LOBBY] Module {module} "
                    "is the host."
                )

            return

        self.lobby.select_starter(
            module
        )

        player_number = (
            self.lobby.player_number(module)
        )

        print(
            f"[LOBBY] Player {player_number} / "
            f"module {module} selected to go first."
        )

    # ========================================================
    # Action Long Press
    # ========================================================

    def on_action_long(
        self,
        module: int,
    ) -> None:

        if self.state == STATE_LOBBY:

            self.handle_lobby_long(
                module
            )

            return

        if self.state == STATE_RUNNING:

            if self.game.pause(
                armed_by=module
            ):

                self.state = STATE_PAUSED

                print(
                    f"[GAME] Paused by module {module}. "
                    "Continue same hold to 5s "
                    "to declare victory."
                )

            return

        if self.state == STATE_PAUSED:

            if self.game.resume():

                self.state = STATE_RUNNING

                print(
                    f"[GAME] Resumed by "
                    f"module {module}."
                )

            return

        if self.state == STATE_GAME_OVER:

            if module == self.lobby.host_module:

                print(
                    "[GAME] Host requested "
                    "full lobby reset."
                )

                self.enter_empty_lobby()

            else:

                print(
                    "[GAME] Non-host post-game "
                    "ACTION LONG ignored."
                )

    def handle_lobby_long(
        self,
        module: int,
    ) -> None:

        if module != self.lobby.host_module:

            print(
                "[LOBBY] Only the host "
                "can start the game."
            )

            return

        if self.lobby.player_count < 2:

            print(
                "[LOBBY] At least two "
                "players are required."
            )

            return

        other_held = (
            self.lobby.held_modules
            - {module}
        )

        if other_held:

            print(
                "[LOBBY] Cannot arm start while "
                f"modules {other_held} are held."
            )

            return

        self.lobby.start_armed_by = (
            module
        )

        print(
            "[LOBBY] Start armed. "
            "Release before 5s to begin countdown, "
            "or keep holding to 5s to reset lobby."
        )

    # ========================================================
    # Five-Second Action Hold / WIN
    # ========================================================

    def on_action_win(
        self,
        module: int,
    ) -> None:

        if self.state == STATE_LOBBY:

            if (
                module == self.lobby.host_module
                and self.lobby.start_armed_by == module
            ):

                print(
                    "[LOBBY] Host held to 5s. "
                    "Clearing lobby."
                )

                self.lobby.start_armed_by = None

                self.enter_empty_lobby()

            return

        if self.state == STATE_PAUSED:

            if self.game.declare_winner(
                module
            ):

                self.state = STATE_GAME_OVER

                print(
                    f"[GAME] Module {module} wins!"
                )

                # Global hub victory sound.
                self.audio.game_over()

                self.print_game_summary()

                self.celebrate_winner(
                    module
                )

            else:

                print(
                    f"[GAME] WIN from module "
                    f"{module} ignored."
                )

    # ========================================================
    # Lobby / Countdown
    # ========================================================

    def begin_countdown(self) -> None:

        if self.lobby.player_count < 2:
            return

        self.state = STATE_STARTING

        self.start_countdown_at = (
            time.monotonic()
        )

        self.last_countdown_tone = -1

        print(
            "[LOBBY] 3-second countdown started."
        )

    def cancel_countdown(self) -> None:

        self.state = STATE_LOBBY

        self.start_countdown_at = None
        self.last_countdown_tone = -1

        self.lobby.start_armed_by = None

    def update_countdown(
        self,
        now: float,
    ) -> None:

        if (
            self.state != STATE_STARTING
            or self.start_countdown_at is None
        ):
            return

        elapsed = (
            now - self.start_countdown_at
        )

        second_index = int(
            elapsed
        )

        if (
            0 <= second_index < 3
            and second_index
            != self.last_countdown_tone
        ):

            self.last_countdown_tone = (
                second_index
            )

            print(
                "[LOBBY] Starting in "
                f"{3 - second_index}..."
            )

            # Short audible cue for each countdown step.
            self.audio.countdown_tone()

        if (
            elapsed
            >= START_COUNTDOWN_SECONDS
        ):

            self.start_game()

    def start_game(self) -> None:

        starter = (
            self.lobby.starter_or_default()
        )

        if (
            starter is None
            or self.lobby.player_count < 2
        ):

            self.cancel_countdown()

            return

        self.game.start(
            players=self.lobby.player_modules,
            starter=starter,
            warning_ms=self.warning_ms,
        )

        self.state = STATE_RUNNING

        self.start_countdown_at = None
        self.last_countdown_tone = -1

        print(
            "[GAME] Game started."
        )

        # Audible confirmation that the game is live.
        self.audio.game_start()

        print(
            "[GAME] Host module: "
            f"{self.lobby.host_module}"
        )

        print(
            "[GAME] Starting module: "
            f"{starter}"
        )

        print(
            "[GAME] Warning threshold locked at "
            f"{warning_description(self.game.current_warning_ms)}"
        )

    def enter_empty_lobby(self) -> None:

        self.state = STATE_LOBBY

        self.lobby.reset_empty()

        self.game = GameEngine()

        self.start_countdown_at = None
        self.last_countdown_tone = -1

        self.leds.clear_cache()

        print(
            "[LOBBY] Empty lobby ready."
        )

    def enter_rematch_lobby(self) -> None:

        self.state = STATE_LOBBY

        self.lobby.reset_for_rematch()

        self.game = GameEngine()

        self.start_countdown_at = None
        self.last_countdown_tone = -1

        self.leds.clear_cache()

        print(
            "[LOBBY] Rematch lobby ready. "
            "Player assignments preserved."
        )

    # ========================================================
    # Game Monitoring
    # ========================================================

    def update_game_logging(
        self,
        now: float,
    ) -> None:

        if self.state != STATE_RUNNING:
            return

        # OFF_GREEN deliberately does not count as a warning.
        # Only the true red warning state is logged.

        if (
            self.game.warning_phase(now)
            == "WARNING"
            and not self.game.warning_logged_for_turn
        ):

            self.game.warning_logged_for_turn = True

            print(
                "[WARNING] Module "
                f"{self.game.active_module} reached "
                f"{warning_description(self.game.current_warning_ms)}."
            )

            # Play once per turn when the warning threshold
            # is first crossed.
            self.audio.warning()

    # ========================================================
    # Winner Celebration
    # ========================================================

    def celebrate_winner(
        self,
        winner: int,
    ) -> None:

        for _ in range(5):

            for module in self.game.players:

                self.leds.blue(
                    module,
                    255
                    if module == winner
                    else 0,
                )

                self.leds.red(
                    module,
                    False,
                )

                self.leds.green(
                    module,
                    False,
                )

            time.sleep(0.10)

            self.leds.blue(
                winner,
                0,
            )

            time.sleep(0.10)

        self.leds.clear_cache()

        print(
            "[GAME] GREEN LED identifies the host."
        )

        print(
            "[GAME] Host ACTION SHORT: "
            "rematch with same players."
        )

        print(
            "[GAME] Host ACTION LONG: "
            "clear players and reset lobby."
        )

    # ========================================================
    # Game Summary
    # ========================================================

    def print_game_summary(self) -> None:

        print("")
        print(
            "========== GAME SUMMARY =========="
        )

        print(
            "Winner module: "
            f"{self.game.winner_module}"
        )

        print(
            "Game time: "
            f"{self.game.game_elapsed():.1f}s"
        )

        print(
            "Paused time: "
            f"{self.game.total_paused_seconds:.1f}s"
        )

        for module in self.game.players:

            stats = (
                self.game.stats[module]
            )

            print(
                f"Module {module}: "
                f"{stats.turns_completed} "
                "completed turns, "
                f"{stats.total_turn_seconds:.1f}s "
                "total completed-turn time"
            )

        print(
            "=================================="
        )

        print("")

    # ========================================================
    # LED Rendering
    # ========================================================

    def update_leds(
        self,
        now: float,
    ) -> None:

        if self.state == STATE_LOBBY:

            self.leds.render_lobby(
                self.lobby,
                now,
            )

        elif (
            self.state == STATE_STARTING
            and self.start_countdown_at is not None
        ):

            elapsed = (
                now
                - self.start_countdown_at
            )

            self.leds.render_starting(
                self.lobby,
                elapsed,
                now,
            )

        elif self.state in (
            STATE_RUNNING,
            STATE_PAUSED,
            STATE_GAME_OVER,
        ):

            self.game.state = (
                self.state
            )

            self.leds.render_game(
                self.game,
                self.lobby.host_module,
                now,
            )

    # ========================================================
    # Main Loop
    # ========================================================

    def run(self) -> None:

        print(
            "==================================="
        )

        print(
            " TurnHub MTG Turn Timer"
        )

        print(
            "==================================="
        )

        print(
            "Starting player modules..."
        )

        self.serial.start()

        connected = (
            self.serial.connected_modules()
        )

        print(
            "[TURNHUB] Connected modules: "
            f"{connected}"
        )

        if not connected:

            print(
                "[TURNHUB] No player modules connected."
            )

        self.leds.clear_cache()

        # Always show an initial snapshot when TurnHub starts.
        self.status.update(
            self,
            force=True,
        )

        try:

            while True:

                now = time.monotonic()

                self.update_countdown(
                    now
                )

                self.update_game_logging(
                    now
                )

                self.update_leds(
                    now
                )

                # Prints a new status snapshot only when
                # meaningful application state changes.
                self.status.update(
                    self
                )

                time.sleep(
                    LOOP_SLEEP_SECONDS
                )

        except KeyboardInterrupt:

            print(
                "\n[TURNHUB] Shutting down."
            )

        finally:

            try:
                self.serial.all_off()

            except Exception:
                pass

            self.audio.stop()

            self.serial.stop()


if __name__ == "__main__":
    TurnHub().run()