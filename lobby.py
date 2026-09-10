"""Player joining and lobby state."""

from __future__ import annotations

from dataclasses import (
    dataclass,
    field,
)

from config import MODULE_IDS


@dataclass
class Lobby:
    module_ids: list[int] = field(
        default_factory=lambda: list(MODULE_IDS)
    )

    player_modules: list[int] = field(
        default_factory=list
    )

    selected_starter: int | None = None

    held_modules: set[int] = field(
        default_factory=set
    )

    start_armed_by: int | None = None

    @property
    def host_module(self) -> int | None:
        if not self.player_modules:
            return None

        return self.player_modules[0]

    @property
    def player_count(self) -> int:
        return len(
            self.player_modules
        )

    def is_joined(
        self,
        module: int,
    ) -> bool:

        return (
            module
            in self.player_modules
        )

    def player_number(
        self,
        module: int,
    ) -> int | None:

        try:
            return (
                self.player_modules.index(module)
                + 1
            )

        except ValueError:
            return None

    def join(
        self,
        module: int,
    ) -> int | None:

        if module not in self.module_ids:
            return None

        if module in self.player_modules:
            return self.player_number(module)

        self.player_modules.append(module)

        return len(
            self.player_modules
        )

    def select_starter(
        self,
        module: int,
    ) -> bool:

        if module not in self.player_modules:
            return False

        self.selected_starter = module

        return True

    def starter_or_default(
        self,
    ) -> int | None:

        if (
            self.selected_starter
            in self.player_modules
        ):
            return self.selected_starter

        if self.player_modules:
            return self.player_modules[0]

        return None

    def reset_empty(self) -> None:
        self.player_modules.clear()
        self.selected_starter = None
        self.held_modules.clear()
        self.start_armed_by = None

    def reset_for_rematch(self) -> None:
        self.selected_starter = None
        self.held_modules.clear()
        self.start_armed_by = None

    def cancel_start_arm(self) -> None:
        self.start_armed_by = None