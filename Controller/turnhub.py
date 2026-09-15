"""TurnHub main application."""

from __future__ import annotations

import time

from audio import AudioController

from config import (
    LOOP_SLEEP_SECONDS,
    MODULE_IDS,
    START_COUNTDOWN_SECONDS,
    START_COUNTDOWN_TONES,
    STATE_GAME_OVER,
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    STATE_STARTING,
    WARNING_OFF,
)

from game_engine import GameEngine
from game_log import GameLogWriter, format_duration
from leds import LEDController
from lobby import Lobby
from serial_controller import SerialController
from settings import SettingsController
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

        # ====================================================
        # Hardware Controllers
        # ====================================================

        self.serial = SerialController(
            message_callback=self.handle_serial_line
        )

        self.audio = AudioController(
            self.serial
        )

        self.leds = LEDController(
            self.serial
        )

        # Physical hub DIP switches.
        self.settings = SettingsController()

        # ====================================================
        # Game Controllers
        # ====================================================

        self.lobby = Lobby()
        self.game = GameEngine()

        self.status = StatusMonitor()
        self.game_log = GameLogWriter()

        self.state = STATE_LOBBY

        # Read the physical timer selector.
        self.warning_ms = (
            self.settings.warning_ms()
        )

        print(
            "[SETTINGS] Turn timer: "
            f"{self.settings.description()}"
        )

        self.start_countdown_at: float | None = None
        self.last_countdown_tone = -1

    # ========================================================
    # Physical Hub Settings
    # ========================================================

    def update_settings(self) -> None:
        """
        Watch the physical DIP switches.

        Changes update the warning setting used for future
        turns. The GameEngine locks the selected value when
        each turn begins, so changing the switch does not
        alter a turn already in progress.
        """

        if not self.settings.changed():
            return

        self.warning_ms = (
            self.settings.warning_ms()
        )

        print(
            "[SETTINGS] Turn timer: "
            f"{self.settings.description()}"
        )

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

        # In the lobby, the host's Pass button is the
        # starting-player randomizer. The selected module is
        # shown by the existing solid blue starter LED.
        if self.state == STATE_LOBBY:

            if (
                module == self.lobby.host_module
                and self.lobby.player_count >= 2
            ):

                starter = (
                    self.lobby.random_starter()
                )

                if starter is not None:
                    player_number = (
                        self.lobby.player_number(
                            starter
                        )
                    )

                    print(
                        "[LOBBY] Random starter: "
                        f"Player {player_number} / "
                        f"Module {starter}."
                    )

                    self.audio.random_starter()

            return

        if self.state != STATE_RUNNING:
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
            self.audio.countdown_cancelled()

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

        if self.state in (
            STATE_RUNNING,
            STATE_PAUSED,
        ):
            return

        if self.state == STATE_GAME_OVER:

            if module == self.lobby.host_module:

                print(
                    "[GAME] Host requested rematch."
                )

                self.enter_rematch_lobby()

            else:
                return

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

            self.audio.player_joined()
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

        self.audio.starter_selected()

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

                self.audio.pause()

            return

        if self.state == STATE_PAUSED:

            if self.game.resume():

                self.state = STATE_RUNNING

                print(
                    f"[GAME] Resumed by "
                    f"module {module}."
                )

                self.audio.resume()

            return

        if self.state == STATE_GAME_OVER:

            if module == self.lobby.host_module:

                print(
                    "[GAME] Host requested "
                    "full lobby reset."
                )

                self.enter_empty_lobby()

            else:
                return

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

        self.audio.start_armed()

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

                self.audio.game_over()

                self.print_game_summary()
                self.save_game_log()

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

            self.audio.countdown_tone(
                START_COUNTDOWN_TONES[
                    second_index
                ]
            )

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

        # Capture the physical timer setting when the
        # first turn begins.
        self.warning_ms = (
            self.settings.warning_ms()
        )

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

        winner = self.game.winner_module
        winner_number = (
            self.lobby.player_number(winner)
            if winner is not None
            else None
        )

        print(
            "Winner: "
            f"Player {winner_number} / "
            f"Module {winner}"
        )

        print(
            "Game time: "
            f"{format_duration(self.game.game_elapsed())}"
        )

        print(
            "Paused time: "
            f"{format_duration(self.game.total_paused_seconds)}"
        )

        for module in self.game.players:

            stats = (
                self.game.stats[module]
            )

            player_number = (
                self.lobby.player_number(module)
            )

            print(
                f"Player {player_number} / "
                f"Module {module}: "
                f"{stats.turns_completed} "
                "completed turns, "
                f"{format_duration(stats.total_turn_seconds)} "
                "completed-turn time"
            )

        if self.game.active_module is not None:
            print(
                "Final partial turn: "
                f"Player {self.lobby.player_number(self.game.active_module)} / "
                f"Module {self.game.active_module}, "
                f"{format_duration(self.game.current_turn_elapsed())}"
            )

        print(
            "=================================="
        )

        print("")

    def save_game_log(self) -> None:

        try:
            path = self.game_log.write(
                self.game
            )

            print(
                "[STATS] Saved game log: "
                f"{path}"
            )

        except OSError as exc:
            print(
                "[STATS] Could not save game log: "
                f"{exc}"
            )

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
            "[SETTINGS] Turn timer: "
            f"{self.settings.description()}"
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

        self.status.update(
            self,
            force=True,
        )

        try:

            while True:

                now = time.monotonic()

                # Check physical hub controls.
                self.update_settings()

                self.update_countdown(
                    now
                )

                self.update_game_logging(
                    now
                )

                self.update_leds(
                    now
                )

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

            # Release the DIP-switch GPIO inputs.
            try:
                self.settings.cleanup()

            except Exception:
                pass

            self.audio.stop()

            self.serial.stop()


if __name__ == "__main__":
    TurnHub().run()