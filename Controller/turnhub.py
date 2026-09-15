"""TurnHub main application."""

from __future__ import annotations

import threading
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
from persistence import PersistentStore
from serial_controller import SerialController
from settings import SettingsController
from status_monitor import StatusMonitor
from web_control import WebControlManager
from web_portal import WebPortal


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
        self.persistence = PersistentStore()

        self.state = STATE_LOBBY

        # Browser controllers are bound to exact physical seats. Tokens are
        # generated only after a matching physical confirmation and are never
        # accepted merely because a browser claims a player number.
        self.web_control = WebControlManager(self)

        self.game_log = GameLogWriter(
            name_resolver=self.persistence.player_label
        )
        self.web = WebPortal(self)

        # Physical and web Pass events can arrive from different threads.
        # Serialize turn advancement and briefly suppress a duplicate physical
        # pass after a successful web pass on the same module.
        self._turn_advance_lock = threading.RLock()
        self._suppress_physical_pass_until: dict[int, float] = {}

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

        # Restore the last recoverable table state, if any. Active games
        # intentionally return paused so downtime is never charged to a turn.
        self.persistence.restore_session(self)

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

        if self.state == STATE_LOBBY:

            # Deliberate setup chord: hold Action and tap Pass.
            # This takes priority over the host randomizer.
            if module in self.lobby.held_modules:
                self.lobby.shared_chord_modules.add(module)
                self.lobby.suppress_next_short_modules.add(module)

                if self.lobby.start_armed_by == module:
                    self.lobby.start_armed_by = None

                result = self.lobby.toggle_secondary(module)

                if result is None:
                    print(
                        "[LOBBY] Shared-player chord ignored: "
                        f"Module {module} must join first."
                    )
                    return

                added, player = result

                if added:
                    print(
                        "[LOBBY] Added second player on "
                        f"Module {module}: {player.label()}."
                    )
                    self.leds.shared_player_added(module)
                    self.audio.shared_player_added()
                else:
                    print(
                        "[LOBBY] Removed second player from "
                        f"Module {module}: {player.label()}."
                    )
                    self.leds.shared_player_removed(module)
                    self.audio.shared_player_removed()

                # Removing a physical seat also invalidates any browser claim
                # that had been paired to that seat.
                self.web_control.reconcile_claims()

                assignments = ", ".join(
                    player.label()
                    for player in self.lobby.players
                )
                print(
                    "[LOBBY] Table order: "
                    f"{assignments}"
                )
                self.persistence.mark_dirty()
                return

            # Normal host Pass remains the randomizer.
            if (
                module == self.lobby.host_module
                and self.lobby.player_count >= 2
            ):
                starter = self.lobby.random_starter()

                if starter is not None:
                    print(
                        "[LOBBY] Random starter: "
                        f"{starter.label()}."
                    )
                    self.audio.random_starter()
                    self.persistence.mark_dirty()

            return

        if self.state != STATE_RUNNING:
            return

        # If a web pass was just accepted from this same physical module,
        # ignore the near-simultaneous hardware event that can otherwise skip
        # a shared-module player.
        if time.monotonic() < self._suppress_physical_pass_until.get(module, 0.0):
            print(
                "[GAME] Ignored duplicate physical Pass immediately "
                f"after web Pass on Module {module}."
            )
            return

        self._pass_running_turn(module)

    def _pass_running_turn(
        self,
        module: int,
    ) -> bool:
        """Advance one running turn from an already-authorized module."""
        with self._turn_advance_lock:
            if self.state != STATE_RUNNING:
                return False

            previous = self.game.active_player

            if not self.game.pass_turn(
                module,
                self.warning_ms,
            ):
                return False

            current = self.game.active_player

            if previous is not None and current is not None:
                print(
                    "[GAME] "
                    f"{previous.label()} passed to "
                    f"{current.label()}."
                )

                if previous.module_id == current.module_id:
                    self.leds.same_module_pass(module)
                    self.audio.same_module_pass()
                    print(
                        "[GAME] Same-module handoff on "
                        f"Module {module}."
                    )
                else:
                    self.audio.turn_pass()

            print(
                "[GAME] Warning locked at "
                f"{warning_description(self.game.current_warning_ms)}"
            )
            self.persistence.mark_dirty()
            return True

    def on_web_pass(
        self,
        module: int,
        slot: int,
    ) -> bool:
        """Advance only if the requested exact logical seat is active."""
        with self._turn_advance_lock:
            if self.state != STATE_RUNNING:
                return False

            active = self.game.active_player
            if active is None or active.seat_key != (module, slot):
                return False

            if not self._pass_running_turn(module):
                return False

            # Protect shared modules from a nearly simultaneous physical click
            # generated by the same human action.
            self._suppress_physical_pass_until[module] = time.monotonic() + 0.45
            return True

    # ========================================================
    # Action Button Tracking
    # ========================================================

    def on_action_down(
        self,
        module: int,
    ) -> None:

        self.lobby.held_modules.add(module)
        self.lobby.action_long_modules.discard(module)
        self.lobby.shared_chord_modules.discard(module)

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

        self.lobby.held_modules.discard(module)

        used_chord = module in self.lobby.shared_chord_modules
        was_long = module in self.lobby.action_long_modules

        self.lobby.shared_chord_modules.discard(module)
        self.lobby.action_long_modules.discard(module)

        if used_chord:
            # ACTION SHORT follows UP only if LONG was never reached.
            if was_long:
                self.lobby.suppress_next_short_modules.discard(module)
            return

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

        if module in self.lobby.suppress_next_short_modules:
            self.lobby.suppress_next_short_modules.discard(module)
            return

        # Web-controller pairing uses a deliberate physical short press.
        # It consumes this Action event so it cannot simultaneously select a
        # starter in the lobby or perform some future short-press action.
        pairing = self.web_control.confirm_physical_action(module)
        if pairing is not None:
            player = pairing.get("player")
            mode = pairing.get("mode")
            label = (
                self.persistence.player_label(player)
                if player is not None
                else str(pairing.get("seat_key"))
            )
            if mode == "reassign":
                print(
                    "[WEB] Host approved web-controller reassignment for "
                    f"{label}."
                )
                self.audio.web_controller_reassigned()
            else:
                print(
                    "[WEB] Physical module confirmed web controller for "
                    f"{label}."
                )
                self.audio.web_controller_paired()
            return

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
            self.web_control.reconcile_claims()
            self.persistence.mark_dirty()
            return

        starter = self.lobby.select_starter(module)

        if starter is None:
            return

        print(
            "[LOBBY] Selected starter: "
            f"{starter.label()}."
        )

        self.audio.starter_selected()
        self.persistence.mark_dirty()

    # ========================================================
    # Action Long Press
    # ========================================================

    def on_action_long(
        self,
        module: int,
    ) -> None:

        self.lobby.action_long_modules.add(module)

        if (
            self.state == STATE_LOBBY
            and module in self.lobby.shared_chord_modules
        ):
            return

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
                self.persistence.mark_dirty()

            return

        if self.state == STATE_PAUSED:

            if self.game.resume():

                self.state = STATE_RUNNING

                print(
                    f"[GAME] Resumed by "
                    f"module {module}."
                )

                self.audio.resume()
                self.persistence.mark_dirty()

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

                winner = self.game.player_by_number(
                    self.game.winner_player
                )

                print(
                    "[GAME] Winner: "
                    + (
                        winner.label()
                        if winner is not None
                        else f"Module {module}"
                    )
                    + "."
                )

                self.audio.game_over()
                self.persistence.mark_dirty()

                self.print_game_summary()
                self.save_game_log()

                self.celebrate_winner()

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
        self.persistence.mark_dirty()

    def cancel_countdown(self) -> None:

        self.state = STATE_LOBBY

        self.start_countdown_at = None
        self.last_countdown_tone = -1

        self.lobby.start_armed_by = None
        self.persistence.mark_dirty()

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

        # Once play begins, browser identity is locked. Any unfinished
        # lobby pairing request is discarded rather than carrying into play.
        self.web_control.cancel_pending()

        self.game.start(
            players=self.lobby.players,
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
            "[GAME] Starting player: "
            f"{starter.label()}."
        )

        print(
            "[GAME] Warning threshold locked at "
            f"{warning_description(self.game.current_warning_ms)}"
        )
        self.persistence.mark_dirty()

    def enter_empty_lobby(self) -> None:

        self.state = STATE_LOBBY

        self.lobby.reset_empty()
        self.web_control.clear_all_claims()

        self.game = GameEngine()

        self.start_countdown_at = None
        self.last_countdown_tone = -1

        self.leds.clear_feedback()
        self.leds.clear_cache()

        print(
            "[LOBBY] Empty lobby ready."
        )
        self.persistence.mark_dirty()

    def enter_rematch_lobby(self) -> None:

        self.state = STATE_LOBBY

        self.lobby.reset_for_rematch()
        self.web_control.cancel_pending()

        self.game = GameEngine()

        self.start_countdown_at = None
        self.last_countdown_tone = -1

        self.leds.clear_feedback()
        self.leds.clear_cache()

        print(
            "[LOBBY] Rematch lobby ready. "
            "Player assignments preserved."
        )
        self.persistence.mark_dirty()

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

            active = self.game.active_player
            label = active.label() if active is not None else "Unknown player"

            print(
                "[WARNING] "
                f"{label} reached "
                f"{warning_description(self.game.current_warning_ms)}."
            )

            self.audio.warning()

    # ========================================================
    # Winner Celebration
    # ========================================================

    def celebrate_winner(self) -> None:

        winner_module = self.game.winner_module
        if winner_module is None:
            return

        for _ in range(5):
            for module in self.game.modules:
                self.leds.blue(
                    module,
                    255 if module == winner_module else 0,
                )
                self.leds.red(module, False)
                self.leds.green(module, False)

            time.sleep(0.10)
            self.leds.blue(winner_module, 0)
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
        print("========== GAME SUMMARY ==========")

        winner = self.game.player_by_number(
            self.game.winner_player
        )

        print(
            "Winner: "
            + (winner.label() if winner is not None else "None")
        )
        print(
            "Game time: "
            f"{format_duration(self.game.game_elapsed())}"
        )
        print(
            "Paused time: "
            f"{format_duration(self.game.total_paused_seconds)}"
        )

        for player in self.game.players:
            stats = self.game.stats[player.player_number]

            print(
                f"{player.label()}: "
                f"{stats.turns_completed} completed turns, "
                f"{format_duration(stats.total_turn_seconds)} "
                "completed-turn time"
            )

        if self.game.active_player is not None:
            print(
                "Final partial turn: "
                f"{self.game.active_player.label()}, "
                f"{format_duration(self.game.current_turn_elapsed())}"
            )

        print("==================================")
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

        self.web.start()

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

                self.persistence.update_autosave(
                    self,
                    now,
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
                self.persistence.save_session(self)
            except OSError as exc:
                print(
                    "[PERSIST] Final state save failed: "
                    f"{exc}"
                )

            self.web.stop()

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