"""TurnHub development status monitor."""

from __future__ import annotations

import time

from config import (
    MODULE_IDS,
    STATE_GAME_OVER,
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    STATE_STARTING,
    WARNING_OFF,
)


class StatusMonitor:
    """
    Development monitor for TurnHub.

    Displays the controller's view of:
        - application state
        - module connections
        - lobby assignments
        - active player
        - game timing
        - player statistics

    This component is informational only. It never modifies
    game state.
    """

    def __init__(self) -> None:

        self.last_snapshot = None

    # ========================================================
    # Helpers
    # ========================================================

    @staticmethod
    def _yes_no(value: bool) -> str:
        return "YES" if value else "NO"

    @staticmethod
    def _format_seconds(
        seconds: float,
    ) -> str:

        seconds = max(
            0,
            int(seconds),
        )

        minutes = seconds // 60
        remaining = seconds % 60

        return (
            f"{minutes:02d}:{remaining:02d}"
        )

    @staticmethod
    def _warning_description(
        warning_ms: int,
    ) -> str:

        if warning_ms == WARNING_OFF:
            return "OFF"

        minutes = int(
            warning_ms / 60_000
        )

        return f"{minutes} min"

    # ========================================================
    # Snapshot
    # ========================================================

    def build_snapshot(
        self,
        turnhub,
    ) -> tuple:

        connected = tuple(
            turnhub.serial.connected_modules()
        )

        joined = tuple(
            turnhub.lobby.player_modules
        )

        player_numbers = tuple(
            (
                module,
                turnhub.lobby.player_number(
                    module
                ),
            )
            for module in MODULE_IDS
        )

        if turnhub.game.players:

            stats = tuple(
                (
                    module,
                    turnhub.game.stats[module]
                    .turns_completed,
                )
                for module
                in turnhub.game.players
            )

        else:
            stats = ()

        return (
            turnhub.state,
            connected,
            joined,
            turnhub.lobby.host_module,
            turnhub.lobby.selected_starter,
            turnhub.game.active_module,
            turnhub.game.winner_module,
            player_numbers,
            stats,
        )

    # ========================================================
    # Conditional Display
    # ========================================================

    def update(
        self,
        turnhub,
        force: bool = False,
    ) -> None:

        snapshot = self.build_snapshot(
            turnhub
        )

        if (
            not force
            and snapshot == self.last_snapshot
        ):
            return

        self.last_snapshot = snapshot

        self.display(
            turnhub
        )

    # ========================================================
    # Display
    # ========================================================

    def display(
        self,
        turnhub,
    ) -> None:

        now = time.monotonic()

        print("")
        print(
            "========== TURNHUB STATUS =========="
        )

        print(
            f"STATE:       {turnhub.state}"
        )

        print(
            "WARNING:     "
            f"{self._warning_description(turnhub.warning_ms)}"
        )

        # ----------------------------------------------------
        # Game-Level Information
        # ----------------------------------------------------

        if turnhub.state in (
            STATE_RUNNING,
            STATE_PAUSED,
            STATE_GAME_OVER,
        ):

            print(
                "GAME TIME:   "
                f"{self._format_seconds(turnhub.game.game_elapsed(now))}"
            )

            if (
                turnhub.game.active_module
                is not None
                and turnhub.state
                != STATE_GAME_OVER
            ):

                print(
                    "ACTIVE:      Module "
                    f"{turnhub.game.active_module}"
                )

                print(
                    "TURN TIME:   "
                    f"{self._format_seconds(turnhub.game.current_turn_elapsed(now))}"
                )

        elif turnhub.state == STATE_STARTING:

            print(
                "COUNTDOWN:    ACTIVE"
            )

        elif turnhub.state == STATE_LOBBY:

            if (
                turnhub.lobby.selected_starter
                is not None
            ):

                print(
                    "STARTER:     Module "
                    f"{turnhub.lobby.selected_starter}"
                )

        print("")

        # ----------------------------------------------------
        # Individual Modules
        # ----------------------------------------------------

        connected_modules = set(
            turnhub.serial.connected_modules()
        )

        for module in MODULE_IDS:

            connected = (
                module in connected_modules
            )

            joined = (
                turnhub.lobby.is_joined(
                    module
                )
            )

            print(
                f"MODULE {module}     "
                f"{'CONNECTED' if connected else 'DISCONNECTED'}"
            )

            print(
                "  Joined:    "
                f"{self._yes_no(joined)}"
            )

            if joined:

                player_number = (
                    turnhub.lobby.player_number(
                        module
                    )
                )

                print(
                    "  Player:    "
                    f"{player_number}"
                )

                print(
                    "  Host:      "
                    f"{self._yes_no(module == turnhub.lobby.host_module)}"
                )

            if (
                turnhub.state
                in (
                    STATE_RUNNING,
                    STATE_PAUSED,
                    STATE_GAME_OVER,
                )
                and module
                in turnhub.game.players
            ):

                if (
                    turnhub.state
                    != STATE_GAME_OVER
                ):

                    role = (
                        "ACTIVE"
                        if module
                        == turnhub.game.active_module
                        else "WAITING"
                    )

                    print(
                        f"  Status:    {role}"
                    )

                stat = (
                    turnhub.game.stats.get(
                        module
                    )
                )

                if stat is not None:

                    print(
                        "  Turns:     "
                        f"{stat.turns_completed}"
                    )

                    print(
                        "  Turn time: "
                        f"{stat.total_turn_seconds:.1f}s"
                    )

            if (
                turnhub.state
                == STATE_GAME_OVER
                and module
                == turnhub.game.winner_module
            ):

                print(
                    "  Result:    WINNER"
                )

            print("")

        print(
            "===================================="
        )

        print("")