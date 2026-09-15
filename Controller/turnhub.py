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

        # Paused-game elimination is deliberately a multi-step physical
        # operation: Action+Pass selects a module, Action Short cycles an
        # exact logical seat, and Pass confirms. The selection is transient
        # and intentionally is not restored after a reboot.
        self.elimination_target_player: int | None = None
        self._elimination_chord_modules: set[int] = set()
        self._suppress_elimination_short_modules: set[int] = set()

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

        if self.state == STATE_PAUSED:

            # During a victory claim the physical confirmation cursor owns
            # the buttons. Action confirms the next logical voter; Pass denies.
            if self.game.has_win_claim:
                expected_number = self.game.next_win_confirmation_player
                expected = self.game.player_by_number(expected_number)
                if expected is None or module != expected.module_id:
                    expected_label = (
                        self.persistence.player_label(expected)
                        if expected is not None
                        else "another player"
                    )
                    print(
                        "[WIN] Denial ignored. Waiting for "
                        f"{expected_label}."
                    )
                    return

                self._deny_win_claim(
                    expected.player_number,
                    source=f"physical Module {module}",
                )
                return

            # Deliberate elimination chord: while paused, hold Action and
            # tap Pass on the module containing the player to eliminate.
            if module in self.lobby.held_modules:
                self._elimination_chord_modules.add(module)
                self._suppress_elimination_short_modules.add(module)
                self._begin_elimination_selection(module)
                return

            # Once a target is selected, a normal Pass on that same module
            # is the explicit confirmation.
            if self.elimination_target_player is not None:
                self._confirm_elimination(module)

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
    # Global Web Pause / Victory Claims
    # ========================================================

    def on_web_pause(
        self,
        player_number: int,
    ) -> bool:
        """Allow any living paired player to stop the clock globally."""
        if self.state != STATE_RUNNING or self.game.has_win_claim:
            return False

        player = self.game.player_by_number(player_number)
        if player is None or self.game.is_eliminated(player_number):
            return False

        if not self.game.pause():
            return False

        # A web pause is pause-only. It must not arm the legacy physical
        # hold-to-win path, and resuming still requires physical hardware.
        self.game.win_armed_module = None
        self.game.win_armed_player = None
        self.state = STATE_PAUSED
        self.audio.pause()
        self.persistence.mark_dirty()
        print(
            "[WEB] Global pause requested by "
            f"{self.persistence.player_label(player)}."
        )
        return True

    def _request_win_claim(
        self,
        player_number: int,
        restore_state: str | None = None,
        source: str = "web",
    ) -> bool:
        if self.elimination_target_player is not None:
            return False

        claimant = self.game.player_by_number(player_number)
        if claimant is None or self.game.is_eliminated(player_number):
            return False

        if not self.game.begin_win_claim(
            player_number,
            restore_state=restore_state,
        ):
            return False

        self.state = self.game.state
        # Freeze controller-pairing changes while the table is voting.
        self.web_control.cancel_pending()

        # A single surviving player is an immediate game-over edge case.
        if self.game.state == STATE_GAME_OVER:
            self._finish_game_outputs(
                reason="Victory claim completed immediately."
            )
            return True

        required_labels = [
            self.persistence.player_label(
                self.game.player_by_number(number)
            )
            for number in self.game.win_claim_required
        ]
        print(
            "[WIN] Victory claimed by "
            f"{self.persistence.player_label(claimant)} via {source}."
        )
        print(
            "[WIN] Requires confirmation from every other living player: "
            + ", ".join(required_labels)
            + ". Any one denial cancels the claim."
        )
        self.audio.win_claimed()
        self.leds.clear_cache()
        self.persistence.mark_dirty()
        return True

    def on_web_life_adjust(
        self,
        player_number: int,
        delta: int,
    ) -> bool:
        """Adjust life for the authenticated web player's own seat only."""
        if delta not in (-100, -10, -1, 1, 10, 100):
            return False

        if self.elimination_target_player is not None:
            return False

        if not self.game.adjust_life(player_number, delta):
            return False

        self.persistence.mark_dirty()
        return True

    def on_web_win_claim(self, player_number: int) -> bool:
        restore_state = self.state
        return self._request_win_claim(
            player_number,
            restore_state=restore_state,
            source="web portal",
        )

    def _confirm_win_claim(
        self,
        player_number: int,
        source: str,
    ) -> bool:
        voter = self.game.player_by_number(player_number)
        accepted, game_finished = self.game.confirm_win_claim(player_number)
        if not accepted:
            return False

        print(
            "[WIN] Confirmed by "
            f"{self.persistence.player_label(voter)} via {source}."
        )
        self.audio.win_confirmed()
        self.persistence.mark_dirty()
        self.leds.clear_cache()

        if game_finished:
            self.state = STATE_GAME_OVER
            self._finish_game_outputs(
                reason="All living opponents confirmed the victory claim."
            )

        return True

    def on_web_win_confirm(self, player_number: int) -> bool:
        return self._confirm_win_claim(player_number, "web portal")

    def _deny_win_claim(
        self,
        player_number: int,
        source: str,
    ) -> bool:
        voter = self.game.player_by_number(player_number)
        if not self.game.deny_win_claim(player_number):
            return False

        self.state = self.game.state
        self.audio.win_denied()
        self.leds.clear_cache()
        self.persistence.mark_dirty()
        print(
            "[WIN] Victory claim denied by "
            f"{self.persistence.player_label(voter)} via {source}. "
            f"Game restored {self.state}."
        )
        return True

    def on_web_win_deny(self, player_number: int) -> bool:
        return self._deny_win_claim(player_number, "web portal")

    def on_web_win_cancel(self, player_number: int) -> bool:
        claimant = self.game.player_by_number(player_number)
        if not self.game.cancel_win_claim(player_number):
            return False

        self.state = self.game.state
        self.audio.win_claim_cancelled()
        self.leds.clear_cache()
        self.persistence.mark_dirty()
        print(
            "[WIN] Victory claim cancelled by "
            f"{self.persistence.player_label(claimant)}. "
            f"Game restored {self.state}."
        )
        return True

    def _finish_game_outputs(self, reason: str | None = None) -> None:
        winner = self.game.player_by_number(self.game.winner_player)
        if reason:
            print(f"[GAME] {reason}")
        print(
            "[GAME] Winner: "
            + (
                self.persistence.player_label(winner)
                if winner is not None
                else "Unknown"
            )
            + "."
        )
        self.audio.game_over()
        self.persistence.mark_dirty()
        self.print_game_summary()
        self.save_game_log()
        self.celebrate_winner()

    # ========================================================
    # Player Elimination
    # ========================================================

    def _begin_elimination_selection(
        self,
        module: int,
    ) -> None:
        """Arm a living logical player on one module for elimination."""

        if self.state != STATE_PAUSED:
            return

        candidates = self.game.living_players_for_module(module)

        if not candidates:
            print(
                "[GAME] Elimination ignored: "
                f"Module {module} has no living players."
            )
            return

        target = candidates[0]
        self.elimination_target_player = target.player_number

        # Entering elimination selection intentionally disarms the old
        # hold-to-win candidate from the action that originally paused play.
        self.game.win_armed_module = None
        self.game.win_armed_player = None

        label = self.persistence.player_label(target)
        print(
            "[GAME] Elimination selected: "
            f"{label}. "
            "Action Short cycles seats; Pass confirms; "
            "Action Long cancels."
        )

        self.audio.elimination_armed()
        self.leds.clear_cache()

    def _cycle_elimination_target(
        self,
        module: int,
    ) -> None:
        """Cycle the selected living seat on a shared module."""

        target = self.game.player_by_number(
            self.elimination_target_player
        )

        if target is None:
            return

        if module != target.module_id:
            print(
                "[GAME] Elimination selection is on "
                f"Module {target.module_id}; Action on Module {module} ignored."
            )
            return

        candidates = self.game.living_players_for_module(module)
        if not candidates:
            self._cancel_elimination_selection()
            return

        if len(candidates) == 1:
            selected = candidates[0]
        else:
            try:
                index = next(
                    i
                    for i, player in enumerate(candidates)
                    if player.player_number == target.player_number
                )
            except StopIteration:
                index = -1

            selected = candidates[(index + 1) % len(candidates)]

        self.elimination_target_player = selected.player_number
        print(
            "[GAME] Elimination target: "
            f"{self.persistence.player_label(selected)}."
        )
        self.audio.elimination_target_changed()
        self.leds.clear_cache()

    def _cancel_elimination_selection(self) -> None:
        if self.elimination_target_player is None:
            return

        self.elimination_target_player = None
        self.audio.elimination_cancelled()
        self.leds.clear_cache()
        print(
            "[GAME] Elimination selection cancelled. Game remains paused."
        )

    def _confirm_elimination(
        self,
        module: int,
    ) -> None:
        """Confirm the selected elimination with Pass on its module."""

        target = self.game.player_by_number(
            self.elimination_target_player
        )

        if target is None:
            self.elimination_target_player = None
            return

        if module != target.module_id:
            print(
                "[GAME] Elimination confirmation ignored: "
                f"use Pass on Module {target.module_id}."
            )
            return

        eliminated, game_finished = self.game.eliminate_player(
            target.player_number,
            self.warning_ms,
        )

        if not eliminated:
            print(
                "[GAME] Elimination could not be completed for "
                f"{self.persistence.player_label(target)}."
            )
            return

        self.elimination_target_player = None
        self.leds.player_eliminated(module)
        self.audio.player_eliminated()

        print(
            "[GAME] Eliminated: "
            f"{self.persistence.player_label(target)}."
        )

        if game_finished:
            self.state = STATE_GAME_OVER
            winner = self.game.player_by_number(
                self.game.winner_player
            )

            print(
                "[GAME] Last player standing. Winner: "
                + (
                    self.persistence.player_label(winner)
                    if winner is not None
                    else "Unknown"
                )
                + "."
            )

            self._finish_game_outputs(
                reason="Last player standing."
            )
            return

        self.state = STATE_PAUSED
        active = self.game.active_player
        print(
            "[GAME] Game remains paused. "
            + (
                f"Next active player: {self.persistence.player_label(active)}."
                if active is not None
                else ""
            )
        )
        self.persistence.mark_dirty()

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
        self._elimination_chord_modules.discard(module)

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

        used_lobby_chord = module in self.lobby.shared_chord_modules
        used_elimination_chord = module in self._elimination_chord_modules
        was_long = module in self.lobby.action_long_modules

        self.lobby.shared_chord_modules.discard(module)
        self._elimination_chord_modules.discard(module)
        self.lobby.action_long_modules.discard(module)

        if used_lobby_chord:
            # ACTION SHORT follows UP only if LONG was never reached.
            if was_long:
                self.lobby.suppress_next_short_modules.discard(module)
            return

        if used_elimination_chord:
            if was_long:
                self._suppress_elimination_short_modules.discard(module)
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

        if module in self._suppress_elimination_short_modules:
            self._suppress_elimination_short_modules.discard(module)
            return

        if self.state == STATE_PAUSED and self.game.has_win_claim:
            expected_number = self.game.next_win_confirmation_player
            expected = self.game.player_by_number(expected_number)
            if expected is None or module != expected.module_id:
                expected_label = (
                    self.persistence.player_label(expected)
                    if expected is not None
                    else "another player"
                )
                print(
                    "[WIN] Confirmation ignored. Waiting for "
                    f"{expected_label}."
                )
                return

            self._confirm_win_claim(
                expected.player_number,
                source=f"physical Module {module}",
            )
            return

        # While an elimination target is armed, Action Short belongs to the
        # elimination selector and cannot also approve a web-controller claim.
        if (
            self.state == STATE_PAUSED
            and self.elimination_target_player is not None
        ):
            self._cycle_elimination_target(module)
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

        if (
            self.state == STATE_PAUSED
            and module in self._elimination_chord_modules
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
                    "to request victory."
                )

                self.audio.pause()
                self.persistence.mark_dirty()

            return

        if self.state == STATE_PAUSED:

            if self.game.has_win_claim:
                print(
                    "[WIN] Resume ignored while a victory claim is pending. "
                    "Confirm, deny, or cancel the claim first."
                )
                return

            if self.elimination_target_player is not None:
                self._cancel_elimination_selection()
                return

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

            if self.elimination_target_player is not None:
                print(
                    "[GAME] WIN ignored while elimination selection is active."
                )
                return

            if self.game.has_win_claim:
                print(
                    "[WIN] A victory claim is already pending."
                )
                return

            # The normal 5-second physical WIN gesture begins as the same
            # continuous hold that paused the game at LONG. On shared modules
            # the claimant is therefore the logical player that was active
            # when that module initiated the hold.
            active = self.game.active_player
            if (
                module != self.game.win_armed_module
                or active is None
                or active.module_id != module
                or self.game.is_eliminated(active.player_number)
            ):
                print(
                    f"[WIN] Physical claim from Module {module} ignored. "
                    "Only the active player's module can make a physical claim."
                )
                return

            # Shared modules cannot identify which human held the physical
            # button, so a physical claim always belongs to the ACTIVE logical
            # player on that module.
            claimant = active.player_number
            if not self._request_win_claim(
                claimant,
                restore_state=STATE_RUNNING,
                source=f"physical Module {module}",
            ):
                print(
                    f"[WIN] Physical claim from Module {module} ignored."
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
            starting_life=self.persistence.starting_life(),
        )

        self.elimination_target_player = None
        self._elimination_chord_modules.clear()
        self._suppress_elimination_short_modules.clear()
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
        self.elimination_target_player = None
        self._elimination_chord_modules.clear()
        self._suppress_elimination_short_modules.clear()

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
        self.elimination_target_player = None
        self._elimination_chord_modules.clear()
        self._suppress_elimination_short_modules.clear()

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
            status = (
                " [ELIMINATED]"
                if self.game.is_eliminated(player.player_number)
                else ""
            )

            print(
                f"{player.label()}{status}: "
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
                elimination_target_player=self.elimination_target_player,
                win_confirmation_player=self.game.next_win_confirmation_player,
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