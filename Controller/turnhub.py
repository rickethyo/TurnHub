"""TurnHub main application."""

from __future__ import annotations

import time

from audio import AudioController

from config import (
    LOOP_SLEEP_SECONDS,
    MODULE_IDS,
    POT_RAW_MAX_MS,
    POT_RAW_MIN_MS,
    START_COUNTDOWN_SECONDS,
    STATE_GAME_OVER,
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    STATE_STARTING,
    WARNING_MAX_MINUTES,
    WARNING_MIN_MINUTES,
    WARNING_OFF,
)

from game_engine import GameEngine
from leds import LEDController
from lobby import Lobby
from serial_controller import SerialController


def map_pot_to_warning(
    raw_value: int,
) -> int:
    """
    Convert the Arduino's existing potentiometer value into
    TurnHub's warning setting.

    Lowest physical position:
        Warning OFF

    Remaining physical range:
        1 through 10 minutes
    """

    # Fully counter-clockwise is OFF.
    if raw_value <= POT_RAW_MIN_MS:
        return WARNING_OFF

    ratio = (
        raw_value - POT_RAW_MIN_MS
    ) / (
        POT_RAW_MAX_MS - POT_RAW_MIN_MS
    )

    ratio = max(
        0.0,
        min(
            1.0,
            ratio,
        ),
    )

    minutes = round(
        WARNING_MIN_MINUTES
        + ratio
        * (
            WARNING_MAX_MINUTES
            - WARNING_MIN_MINUTES
        )
    )

    minutes = max(
        WARNING_MIN_MINUTES,
        min(
            WARNING_MAX_MINUTES,
            minutes,
        ),
    )

    return (
        minutes * 60_000
    )


def warning_description(
    warning_ms: int,
) -> str:

    if warning_ms == WARNING_OFF:
        return "OFF (green at 5 minutes)"

    minutes = int(
        warning_ms / 60_000
    )

    if minutes == 1:
        return "1 minute"

    return f"{minutes} minutes"


class TurnHub:
    def __init__(self) -> None:

        self.serial = SerialController()

        self.audio = AudioController(
            self.serial
        )

        self.leds = LEDController(
            self.serial
        )

        self.lobby = Lobby()
        self.game = GameEngine()

        self.state = STATE_LOBBY

        # Until the Arduino reports its initial pot position,
        # default to warning OFF.
        self.warning_ms = WARNING_OFF

        self.start_countdown_at: float | None = None
        self.last_countdown_tone = -1

        self.hardware_outage: dict | None = None
        self.was_ever_connected = False

    # ========================================================
    # Serial Lifecycle
    # ========================================================

    def connect_if_needed(self) -> None:

        if self.serial.connected:
            return

        startup_lines = (
            self.serial.connect()
        )

        if not self.serial.connected:
            return

        reconnecting = (
            self.was_ever_connected
        )

        self.was_ever_connected = True

        for line in startup_lines:
            self.handle_serial_line(
                line,
                startup=True,
            )

        self.leds.clear_cache()

        if reconnecting:
            self.on_connection_restored()

        else:
            print(
                "[TURNHUB] Arduino ready."
            )

            self.audio.ready()

    def on_connection_lost(self) -> None:

        print(
            "[TURNHUB] Arduino disconnected. "
            "Timer frozen."
        )

        self.lobby.held_modules.clear()
        self.lobby.start_armed_by = None

        self.game.win_armed_module = None

        if self.state in (
            STATE_RUNNING,
            STATE_PAUSED,
        ):

            self.hardware_outage = (
                self.game.freeze_for_hardware_loss()
            )

    def on_connection_restored(self) -> None:

        print(
            "[TURNHUB] Arduino reconnected."
        )

        if self.state == STATE_STARTING:

            print(
                "[LOBBY] Countdown cancelled "
                "after reconnect."
            )

            self.state = STATE_LOBBY
            self.start_countdown_at = None
            self.last_countdown_tone = -1

        elif (
            self.state
            in (
                STATE_RUNNING,
                STATE_PAUSED,
            )
            and self.hardware_outage
        ):

            self.game.restore_after_hardware_loss(
                self.hardware_outage
            )

            self.state = STATE_PAUSED

            print(
                "[GAME] Reconnected in PAUSED state. "
                "Long-press to resume."
            )

        self.hardware_outage = None

        self.leds.clear_cache()

    # ========================================================
    # Serial Protocol
    # ========================================================

    def handle_serial_line(
        self,
        line: str,
        startup: bool = False,
    ) -> None:

        if not line:
            return

        parts = line.split("|")

        event = (
            parts[0].upper()
        )

        # ----------------------------------------------------
        # Arduino Ready
        # ----------------------------------------------------

        if event == "READY":

            print(
                "[ARDUINO] READY"
            )

            return

        # ----------------------------------------------------
        # Potentiometer
        # ----------------------------------------------------

        if (
            event == "POT"
            and len(parts) >= 2
        ):

            try:
                raw_pot = int(
                    parts[1]
                )

            except ValueError:
                return

            new_warning = (
                map_pot_to_warning(
                    raw_pot
                )
            )

            changed = (
                new_warning
                != self.warning_ms
            )

            self.warning_ms = (
                new_warning
            )

            if changed:

                print(
                    "[POT] Next-turn warning: "
                    f"{warning_description(self.warning_ms)}"
                )

                if (
                    self.state
                    == STATE_LOBBY
                    and not startup
                ):

                    self.audio.pot_feedback(
                        raw_pot
                    )

            return

        # ----------------------------------------------------
        # Buttons
        # ----------------------------------------------------

        if (
            event != "BUTTON"
            or len(parts) < 3
        ):

            print(
                f"[SERIAL] Unknown: {line}"
            )

            return

        try:
            module = int(
                parts[1]
            )

        except ValueError:
            return

        action = (
            parts[2].upper()
        )

        if module not in MODULE_IDS:
            return

        if action == "DOWN":

            self.on_button_down(
                module
            )

        elif action == "UP":

            self.on_button_up(
                module
            )

        elif action == "SHORT":

            self.on_button_short(
                module
            )

        elif action == "LONG":

            self.on_button_long(
                module
            )

        elif action == "WIN":

            self.on_button_win(
                module
            )

    # ========================================================
    # Physical Button Tracking
    # ========================================================

    def on_button_down(
        self,
        module: int,
    ) -> None:

        self.lobby.held_modules.add(
            module
        )

        if self.state == STATE_STARTING:

            print(
                "[LOBBY] Countdown cancelled "
                "by button press."
            )

            self.audio.invalid()

            self.cancel_countdown()

    def on_button_up(
        self,
        module: int,
    ) -> None:

        self.lobby.held_modules.discard(
            module
        )

        if (
            self.state == STATE_LOBBY
            and self.lobby.start_armed_by
            == module
            and module
            == self.lobby.host_module
        ):

            print(
                "[LOBBY] Host released. "
                "Starting countdown."
            )

            self.lobby.start_armed_by = None

            self.begin_countdown()

    # ========================================================
    # Short Press
    # ========================================================

    def on_button_short(
        self,
        module: int,
    ) -> None:

        if self.state == STATE_LOBBY:

            self.handle_lobby_short(
                module
            )

            return

        if self.state == STATE_RUNNING:

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
                    f"[GAME] Ignored SHORT "
                    f"from module {module}."
                )

            return

        if self.state == STATE_GAME_OVER:

            if (
                module
                == self.lobby.host_module
            ):

                print(
                    "[GAME] Host requested rematch."
                )

                self.audio.rematch()

                self.enter_rematch_lobby()

            else:

                print(
                    "[GAME] Non-host post-game "
                    "SHORT ignored."
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

            self.audio.join()

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
    # Long Press
    # ========================================================

    def on_button_long(
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

            if (
                module
                == self.lobby.host_module
            ):

                print(
                    "[GAME] Host requested "
                    "full lobby reset."
                )

                self.audio.lobby_reset()

                self.enter_empty_lobby()

            else:

                print(
                    "[GAME] Non-host post-game "
                    "LONG ignored."
                )

    def handle_lobby_long(
        self,
        module: int,
    ) -> None:

        if (
            module
            != self.lobby.host_module
        ):

            print(
                "[LOBBY] Only the host "
                "can start the game."
            )

            self.audio.invalid()

            return

        if self.lobby.player_count < 2:

            print(
                "[LOBBY] At least two "
                "players are required."
            )

            self.audio.invalid()

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

            self.audio.invalid()

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
    # Five-Second Hold / WIN
    # ========================================================

    def on_button_win(
        self,
        module: int,
    ) -> None:

        if self.state == STATE_LOBBY:

            if (
                module
                == self.lobby.host_module
                and self.lobby.start_armed_by
                == module
            ):

                print(
                    "[LOBBY] Host held to 5s. "
                    "Clearing lobby."
                )

                self.lobby.start_armed_by = None

                self.audio.lobby_reset()

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

            self.audio.invalid()

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
            or self.start_countdown_at
            is None
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

            self.audio.countdown(
                second_index
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

        self.audio.victory()

        self.leds.clear_cache()

        print(
            "[GAME] GREEN LED identifies the host."
        )

        print(
            "[GAME] Host SHORT: "
            "rematch with same players."
        )

        print(
            "[GAME] Host LONG: "
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

        print(
            "Hardware downtime: "
            f"{self.game.total_hardware_downtime:.1f}s"
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
            and self.start_countdown_at
            is not None
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
            "Waiting for Arduino..."
        )

        try:

            while True:

                if not self.serial.connected:

                    self.connect_if_needed()

                    time.sleep(
                        LOOP_SLEEP_SECONDS
                    )

                    continue

                try:

                    for line in (
                        self.serial.read_lines()
                    ):

                        self.handle_serial_line(
                            line
                        )

                except Exception as exc:

                    print(
                        "[SERIAL] Lost connection: "
                        f"{exc}"
                    )

                    self.on_connection_lost()

                    self.serial.disconnect()

                    time.sleep(
                        LOOP_SLEEP_SECONDS
                    )

                    continue

                now = time.monotonic()

                self.update_countdown(
                    now
                )

                self.update_game_logging(
                    now
                )

                try:

                    self.update_leds(
                        now
                    )

                except Exception as exc:

                    print(
                        "[SERIAL] Output failed: "
                        f"{exc}"
                    )

                    self.on_connection_lost()

                    self.serial.disconnect()

                time.sleep(
                    LOOP_SLEEP_SECONDS
                )

        except KeyboardInterrupt:

            print(
                "\n[TURNHUB] Shutting down."
            )

        finally:

            if self.serial.connected:

                try:
                    self.serial.off()

                except Exception:
                    pass

            self.serial.disconnect()


if __name__ == "__main__":
    TurnHub().run()