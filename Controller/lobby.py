"""Player joining and lobby state."""

from __future__ import annotations

import secrets

from dataclasses import dataclass, field

from config import MODULE_IDS
from player import PlayerSeat


@dataclass
class Lobby:
    module_ids: list[int] = field(
        default_factory=lambda: list(MODULE_IDS)
    )

    # Physical modules that have joined, in table order.
    player_modules: list[int] = field(default_factory=list)

    # A module in this set controls two adjacent logical players.
    secondary_modules: set[int] = field(default_factory=set)

    # Stable physical seat identity: (module_id, slot 1/2).
    # Player numbers are derived and may shift during lobby setup.
    selected_starter: tuple[int, int] | None = None

    held_modules: set[int] = field(default_factory=set)
    start_armed_by: int | None = None

    # State used to consume the deliberate Action+Pass lobby chord.
    # Firmware sends ACTION UP followed by ACTION SHORT for a short
    # Action press, so TurnHub must suppress that trailing SHORT.
    shared_chord_modules: set[int] = field(default_factory=set)
    action_long_modules: set[int] = field(default_factory=set)
    suppress_next_short_modules: set[int] = field(default_factory=set)

    @property
    def host_module(self) -> int | None:
        if not self.player_modules:
            return None
        return self.player_modules[0]

    @property
    def players(self) -> list[PlayerSeat]:
        players: list[PlayerSeat] = []
        number = 1

        for module in self.player_modules:
            players.append(
                PlayerSeat(
                    player_number=number,
                    module_id=module,
                    slot=1,
                )
            )
            number += 1

            if module in self.secondary_modules:
                players.append(
                    PlayerSeat(
                        player_number=number,
                        module_id=module,
                        slot=2,
                    )
                )
                number += 1

        return players

    @property
    def player_count(self) -> int:
        return len(self.players)

    @property
    def selected_starter_player(self) -> PlayerSeat | None:
        if self.selected_starter is None:
            return None

        for player in self.players:
            if player.seat_key == self.selected_starter:
                return player

        return None

    def is_joined(self, module: int) -> bool:
        return module in self.player_modules

    def has_secondary(self, module: int) -> bool:
        return module in self.secondary_modules

    def players_for_module(self, module: int) -> list[PlayerSeat]:
        return [
            player
            for player in self.players
            if player.module_id == module
        ]

    def primary_player(self, module: int) -> PlayerSeat | None:
        players = self.players_for_module(module)
        return players[0] if players else None

    def player_number(
        self,
        module: int,
        slot: int = 1,
    ) -> int | None:
        for player in self.players:
            if (
                player.module_id == module
                and player.slot == slot
            ):
                return player.player_number
        return None

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

    def join(self, module: int) -> int | None:
        if module not in self.module_ids:
            return None

        if module in self.player_modules:
            return self.player_number(module)

        self.player_modules.append(module)
        return self.player_number(module)

    def leave(self, module: int) -> bool:
        """Remove a physical module and all logical seats on it from the lobby."""
        if module not in self.player_modules:
            return False

        self.player_modules.remove(module)
        self.secondary_modules.discard(module)
        if self.selected_starter is not None and self.selected_starter[0] == module:
            self.selected_starter = None
        self.held_modules.discard(module)
        self.shared_chord_modules.discard(module)
        self.action_long_modules.discard(module)
        self.suppress_next_short_modules.discard(module)
        if self.start_armed_by == module:
            self.start_armed_by = None
        return True

    def toggle_secondary(
        self,
        module: int,
    ) -> tuple[bool, PlayerSeat] | None:
        """Toggle Slot B. Returns (added, affected seat) on success."""

        if module not in self.player_modules:
            return None

        if module in self.secondary_modules:
            secondary = next(
                (
                    player
                    for player in self.players_for_module(module)
                    if player.slot == 2
                ),
                None,
            )

            self.secondary_modules.remove(module)

            if self.selected_starter == (module, 2):
                self.selected_starter = None

            # Capture the removed seat using its number before removal.
            if secondary is None:
                secondary = PlayerSeat(
                    player_number=0,
                    module_id=module,
                    slot=2,
                )

            return (False, secondary)

        self.secondary_modules.add(module)

        secondary = next(
            player
            for player in self.players_for_module(module)
            if player.slot == 2
        )

        return (True, secondary)

    def select_starter(
        self,
        module: int,
    ) -> PlayerSeat | None:
        """Select, or cycle between, the logical players on a module."""

        module_players = self.players_for_module(module)
        if not module_players:
            return None

        selected = self.selected_starter_player

        if (
            selected is not None
            and selected.module_id == module
            and len(module_players) > 1
        ):
            next_index = (
                module_players.index(selected) + 1
            ) % len(module_players)
            selected = module_players[next_index]
        else:
            selected = module_players[0]

        self.selected_starter = selected.seat_key
        return selected

    def random_starter(self) -> PlayerSeat | None:
        players = self.players
        if not players:
            return None

        selected = secrets.choice(players)
        self.selected_starter = selected.seat_key
        return selected

    def starter_or_default(self) -> PlayerSeat | None:
        selected = self.selected_starter_player
        if selected is not None:
            return selected

        players = self.players
        return players[0] if players else None

    def reset_empty(self) -> None:
        self.player_modules.clear()
        self.secondary_modules.clear()
        self.selected_starter = None
        self.held_modules.clear()
        self.start_armed_by = None
        self.shared_chord_modules.clear()
        self.action_long_modules.clear()
        self.suppress_next_short_modules.clear()

    def reset_for_rematch(self) -> None:
        self.selected_starter = None
        self.held_modules.clear()
        self.start_armed_by = None
        self.shared_chord_modules.clear()
        self.action_long_modules.clear()
        self.suppress_next_short_modules.clear()

    def cancel_start_arm(self) -> None:
        self.start_armed_by = None
