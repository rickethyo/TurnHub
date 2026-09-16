"""Persistent TurnHub game statistics with optional USB archiving."""

from __future__ import annotations

import shutil
import threading
import time

from datetime import datetime
from pathlib import Path

from game_engine import GameEngine


USB_DEVICE_MARKER = Path("/dev/disk/by-label/TURNHUB")
USB_MOUNT_DIRECTORY = Path("/mnt/turnhub-usb")
USB_SYNC_INTERVAL_SECONDS = 2.0


def format_duration(seconds: float) -> str:
    """Format elapsed seconds as minutes:seconds."""

    total_seconds = max(0, int(seconds + 0.5))
    minutes = total_seconds // 60
    remaining = total_seconds % 60
    return f"{minutes:02d}:{remaining:02d}"


class GameLogWriter:
    """Writes game logs locally and mirrors them to TURNHUB USB."""

    def __init__(
        self,
        statistics_directory: Path | None = None,
        usb_mount_directory: Path | None = None,
        usb_device_marker: Path | None = None,
        start_usb_monitor: bool = True,
        name_resolver=None,
    ) -> None:

        if statistics_directory is None:
            statistics_directory = (
                Path(__file__).resolve().parent.parent
                / "statistics"
            )

        if usb_mount_directory is None:
            usb_mount_directory = USB_MOUNT_DIRECTORY

        if usb_device_marker is None:
            usb_device_marker = USB_DEVICE_MARKER

        self.statistics_directory = Path(
            statistics_directory
        )

        self.usb_mount_directory = Path(
            usb_mount_directory
        )

        self.usb_device_marker = Path(
            usb_device_marker
        )

        self._usb_lock = threading.Lock()
        self._usb_monitor_thread: threading.Thread | None = None
        self.name_resolver = name_resolver

        if start_usb_monitor:
            self._usb_monitor_thread = threading.Thread(
                target=self._usb_monitor_loop,
                daemon=True,
                name="TurnHub-USB-Log-Sync",
            )
            self._usb_monitor_thread.start()

    # ========================================================
    # Labels / Formatting
    # ========================================================

    def _player_label(
        self,
        game: GameEngine,
        player_number: int | None,
    ) -> str:

        player = game.player_by_number(
            player_number
        )

        if player is None:
            return "None"

        if self.name_resolver is not None:
            return self.name_resolver(player)

        return player.label(
            include_slot=True
        )

    def _seat_label(self, player) -> str:
        if self.name_resolver is not None:
            return self.name_resolver(player)
        return player.label(include_slot=True)

    # ========================================================
    # Local File Naming
    # ========================================================

    def _unique_path(
        self,
        timestamp: datetime,
    ) -> Path:

        stem = timestamp.strftime(
            "game_%Y-%m-%d_%H-%M-%S"
        )

        path = (
            self.statistics_directory
            / f"{stem}.txt"
        )

        suffix = 2

        while path.exists():
            path = (
                self.statistics_directory
                / f"{stem}_{suffix}.txt"
            )
            suffix += 1

        return path

    # ========================================================
    # USB Detection / Mount Verification
    # ========================================================

    def _usb_device_present(self) -> bool:
        """Return True when a device labeled TURNHUB is attached."""

        try:
            return self.usb_device_marker.exists()

        except OSError:
            return False

    def _actual_usb_mounted(self) -> bool:
        """
        Confirm that a real filesystem is mounted at the USB path.

        This intentionally rejects an autofs/systemd automount placeholder
        so TurnHub never writes into the bare /mnt directory by mistake.
        """

        try:
            mounts = Path(
                "/proc/mounts"
            ).read_text(
                encoding="utf-8",
                errors="replace",
            )

        except OSError:
            return False

        target = str(
            self.usb_mount_directory
        )

        for line in mounts.splitlines():

            fields = line.split()

            if len(fields) < 3:
                continue

            mountpoint = (
                fields[1]
                .replace("\\040", " ")
                .replace("\\011", "\t")
                .replace("\\134", "\\")
            )

            filesystem = fields[2]

            if (
                mountpoint == target
                and filesystem != "autofs"
            ):
                return True

        return False

    def _usb_ready(self) -> bool:
        """
        Return True when the labeled USB is mounted.

        Accessing the mount directory triggers the configured systemd
        automount, but only after the TURNHUB-labeled device is detected.
        """

        if not self._usb_device_present():
            return False

        try:
            # Trigger x-systemd.automount if it has not mounted yet.
            iterator = self.usb_mount_directory.iterdir()

            try:
                next(iterator)

            except StopIteration:
                pass

        except OSError:
            return False

        return self._actual_usb_mounted()

    # ========================================================
    # USB Copy Helpers
    # ========================================================

    @staticmethod
    def _needs_copy(
        source: Path,
        destination: Path,
    ) -> bool:
        """Return True when destination is missing or incomplete."""

        try:
            if not destination.exists():
                return True

            return (
                source.stat().st_size
                != destination.stat().st_size
            )

        except OSError:
            return True

    @staticmethod
    def _copy_atomic(
        source: Path,
        destination: Path,
    ) -> None:
        """Copy through a temporary file, then rename into place."""

        temporary = destination.with_name(
            destination.name + ".tmp"
        )

        try:
            shutil.copy2(
                source,
                temporary,
            )

            temporary.replace(
                destination
            )

        finally:
            try:
                if temporary.exists():
                    temporary.unlink()

            except OSError:
                pass

    def sync_to_usb(
        self,
        announce: bool = False,
    ) -> int:
        """
        Copy all missing local game logs to the TURNHUB USB.

        Returns the number of files copied. USB errors are contained here
        so they can never prevent the normal local game log from succeeding.
        """

        with self._usb_lock:

            if not self._usb_ready():
                return 0

            try:
                usb_statistics = (
                    self.usb_mount_directory
                    / "statistics"
                )

                usb_statistics.mkdir(
                    parents=True,
                    exist_ok=True,
                )

                copied = 0

                for source in sorted(
                    self.statistics_directory.glob(
                        "game_*.txt"
                    )
                ):

                    destination = (
                        usb_statistics
                        / source.name
                    )

                    if not self._needs_copy(
                        source,
                        destination,
                    ):
                        continue

                    self._copy_atomic(
                        source,
                        destination,
                    )

                    copied += 1

                if announce and copied > 0:
                    print(
                        "[STATS] USB archive synced: "
                        f"{copied} game log"
                        f"{'' if copied == 1 else 's'}."
                    )

                return copied

            except OSError as exc:

                if announce:
                    print(
                        "[STATS] USB archive unavailable: "
                        f"{exc}"
                    )

                return 0

    # ========================================================
    # USB Insertion Monitor
    # ========================================================

    def _usb_monitor_loop(self) -> None:
        """
        Watch for insertion of the TURNHUB-labeled USB.

        A sync runs once when the device transitions from absent to present.
        This lets older locally stored game logs appear on a USB inserted
        after those games were already completed.
        """

        was_present = False

        while True:

            present = (
                self._usb_device_present()
            )

            if (
                present
                and not was_present
            ):
                # Give udev/systemd a brief moment to settle.
                time.sleep(0.5)

                self.sync_to_usb(
                    announce=True
                )

            was_present = present

            time.sleep(
                USB_SYNC_INTERVAL_SECONDS
            )

    # ========================================================
    # Game Log Writing
    # ========================================================

    def write(
        self,
        game: GameEngine,
    ) -> Path:
        """
        Write a completed game's statistics locally.

        The local file is authoritative. After it is safely written,
        TurnHub attempts to mirror all missing logs to the USB archive.
        """

        self.statistics_directory.mkdir(
            parents=True,
            exist_ok=True,
        )

        timestamp = datetime.now()

        path = self._unique_path(
            timestamp
        )

        lines = [
            "TurnHub Game Log",
            "================",
            (
                "Saved: "
                + timestamp.strftime(
                    "%Y-%m-%d %I:%M:%S %p"
                )
            ),
            (
                "Starting player: "
                + self._player_label(
                    game,
                    game.starter_player,
                )
            ),
            (
                "Winner: "
                + self._player_label(
                    game,
                    game.winner_player,
                )
            ),
            (
                "Game time: "
                + format_duration(
                    game.game_elapsed()
                )
            ),
            (
                "Paused time: "
                + format_duration(
                    game.total_paused_seconds
                )
            ),
            f"Game profile: {game.game_profile}",
            f"Starting life: {game.starting_life}",
            (
                "Elimination order: "
                + (
                    " -> ".join(
                        self._player_label(game, number)
                        for number in game.eliminated_players
                    )
                    if game.eliminated_players
                    else "None"
                )
            ),
            "",
            "Player Statistics",
            "-----------------",
        ]

        for player in game.players:

            stats = game.stats[
                player.player_number
            ]

            lines.extend(
                [
                    (
                        self._seat_label(player)
                        + (
                            " [ELIMINATED]"
                            if game.is_eliminated(player.player_number)
                            else ""
                        )
                    ),
                    (
                        "  Final life: "
                        f"{game.life_totals.get(player.player_number, game.starting_life)}"
                    ),
                    (
                        "  Completed turns: "
                        f"{stats.turns_completed}"
                    ),
                    (
                        "  Completed-turn time: "
                        + format_duration(
                            stats.total_turn_seconds
                        )
                    ),
                ]
            )

            if game.game_profile == "mtg_commander":
                damage_map = game.commander_damage.get(player.player_number, {})
                if damage_map:
                    lines.append("  Commander damage received:")
                    for source_number, damage in sorted(damage_map.items()):
                        lines.append(
                            "    from "
                            + self._player_label(game, source_number)
                            + f": {damage}"
                        )

            if stats.turns_completed > 0:

                average = (
                    stats.total_turn_seconds
                    / stats.turns_completed
                )

                lines.append(
                    "  Average completed turn: "
                    + format_duration(
                        average
                    )
                )

            lines.append("")

        if (
            game.game_ended_at is not None
            and game.active_player is not None
        ):

            lines.extend(
                [
                    "Final partial turn",
                    "------------------",
                    (
                        self._seat_label(game.active_player)
                        + ": "
                        + format_duration(
                            game.current_turn_elapsed()
                        )
                    ),
                    (
                        "This partial turn is not included "
                        "in completed-turn totals."
                    ),
                    "",
                ]
            )

        # The local copy is always written first and remains authoritative.
        path.write_text(
            "\n".join(lines),
            encoding="utf-8",
        )

        # USB problems never escape this method.
        self.sync_to_usb(
            announce=True
        )

        return path
