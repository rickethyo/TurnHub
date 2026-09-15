"""Persistent TurnHub names, preferences, and recoverable game state."""

from __future__ import annotations

import json
import os
import threading
import time

from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from config import (
    STATE_GAME_OVER,
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    STATE_STARTING,
)
from game_engine import GameEngine, PlayerStats
from player import PlayerSeat


DATA_DIRECTORY = Path(
    os.environ.get(
        "TURNHUB_DATA_DIR",
        str(Path.home() / ".local" / "share" / "turnhub"),
    )
)
SETTINGS_PATH = DATA_DIRECTORY / "settings.json"
SESSION_PATH = DATA_DIRECTORY / "session.json"
ACTIVE_AUTOSAVE_SECONDS = 10.0
MAX_NAME_LENGTH = 40
DEFAULT_STARTING_LIFE = 40
STARTING_LIFE_PRESETS = (20, 25, 30, 40, 50, 2000, 4000, 8000)
MIN_STARTING_LIFE = 0
MAX_STARTING_LIFE = 1_000_000


class PersistentStore:
    """Owns small user settings and recoverable TurnHub session state."""

    def __init__(
        self,
        data_directory: Path | None = None,
    ) -> None:
        self.data_directory = Path(data_directory or DATA_DIRECTORY)
        self.settings_path = self.data_directory / "settings.json"
        self.session_path = self.data_directory / "session.json"

        self._lock = threading.RLock()
        self._module_names: dict[str, str] = {}
        self._seat_names: dict[str, str] = {}
        self._starting_life: int = DEFAULT_STARTING_LIFE

        self._dirty = False
        self._last_session_write_monotonic = 0.0
        self.last_session_save_unix: float | None = None
        self.recovered_session = False

        self._load_settings()

    # ========================================================
    # Atomic JSON
    # ========================================================

    @staticmethod
    def _atomic_write_json(path: Path, payload: dict[str, Any]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(path.name + ".tmp")

        try:
            temporary.write_text(
                json.dumps(payload, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            temporary.replace(path)
        finally:
            try:
                if temporary.exists():
                    temporary.unlink()
            except OSError:
                pass

    @staticmethod
    def _read_json(path: Path) -> dict[str, Any] | None:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return None
        except (OSError, json.JSONDecodeError) as exc:
            print(f"[PERSIST] Could not read {path.name}: {exc}")
            return None

        return data if isinstance(data, dict) else None

    # ========================================================
    # Names / User Settings
    # ========================================================

    @staticmethod
    def _clean_name(value: Any) -> str:
        if value is None:
            return ""

        # Single-line names only. Collapse repeated whitespace so the
        # portal, logs, and future e-ink displays stay predictable.
        text = " ".join(str(value).replace("\x00", "").split())
        return text[:MAX_NAME_LENGTH]

    @staticmethod
    def _seat_key(module_id: int, slot: int) -> str:
        return f"{int(module_id)}:{int(slot)}"

    def _load_settings(self) -> None:
        data = self._read_json(self.settings_path)
        if data is None:
            return

        module_names = data.get("module_names", {})
        seat_names = data.get("seat_names", {})
        try:
            starting_life = int(data.get("starting_life", DEFAULT_STARTING_LIFE))
        except (TypeError, ValueError):
            starting_life = DEFAULT_STARTING_LIFE
        self._starting_life = max(
            MIN_STARTING_LIFE,
            min(starting_life, MAX_STARTING_LIFE),
        )

        if isinstance(module_names, dict):
            self._module_names = {
                str(key): self._clean_name(value)
                for key, value in module_names.items()
                if self._clean_name(value)
            }

        if isinstance(seat_names, dict):
            self._seat_names = {
                str(key): self._clean_name(value)
                for key, value in seat_names.items()
                if self._clean_name(value)
            }

    def settings_snapshot(self) -> dict[str, Any]:
        with self._lock:
            return {
                "module_names": dict(self._module_names),
                "seat_names": dict(self._seat_names),
                "starting_life": self._starting_life,
                "starting_life_presets": list(STARTING_LIFE_PRESETS),
                "storage_directory": str(self.data_directory),
            }

    def update_names(
        self,
        module_names: dict[Any, Any] | None = None,
        seat_names: dict[Any, Any] | None = None,
        starting_life: Any | None = None,
    ) -> dict[str, Any]:
        with self._lock:
            if module_names is not None:
                cleaned: dict[str, str] = {}
                for key, value in module_names.items():
                    name = self._clean_name(value)
                    if name:
                        cleaned[str(key)] = name
                self._module_names = cleaned

            if seat_names is not None:
                cleaned = {}
                for key, value in seat_names.items():
                    name = self._clean_name(value)
                    if name:
                        cleaned[str(key)] = name
                self._seat_names = cleaned

            if starting_life is not None:
                try:
                    value = int(starting_life)
                except (TypeError, ValueError) as exc:
                    raise ValueError("starting_life must be an integer") from exc
                if not MIN_STARTING_LIFE <= value <= MAX_STARTING_LIFE:
                    raise ValueError(
                        f"starting_life must be between {MIN_STARTING_LIFE} and {MAX_STARTING_LIFE}"
                    )
                self._starting_life = value

            payload = {
                "version": 2,
                "saved_at": datetime.now(timezone.utc).isoformat(),
                "module_names": self._module_names,
                "seat_names": self._seat_names,
                "starting_life": self._starting_life,
            }
            self._atomic_write_json(self.settings_path, payload)
            return self.settings_snapshot()


    def starting_life(self) -> int:
        with self._lock:
            return self._starting_life

    def module_name(self, module_id: int) -> str:
        with self._lock:
            return self._module_names.get(
                str(module_id),
                f"Module {module_id}",
            )

    def seat_name(self, module_id: int, slot: int) -> str | None:
        with self._lock:
            name = self._seat_names.get(self._seat_key(module_id, slot), "")
            return name or None

    def player_name(self, player: PlayerSeat | None) -> str:
        if player is None:
            return "None"
        return self.seat_name(player.module_id, player.slot) or (
            f"Player {player.player_number}"
        )

    def player_label(
        self,
        player: PlayerSeat | None,
        include_location: bool = True,
    ) -> str:
        if player is None:
            return "None"

        name = self.player_name(player)
        if not include_location:
            return name

        module_name = self.module_name(player.module_id)
        return (
            f"{name} (Player {player.player_number} / "
            f"{module_name} / Seat {player.slot_name})"
        )

    # ========================================================
    # Session Serialization
    # ========================================================

    @staticmethod
    def _game_elapsed_for_save(game: GameEngine, now: float) -> float:
        if (
            game.state == STATE_PAUSED
            and game.pause_started_at is not None
        ):
            return max(
                0.0,
                game.pause_started_at
                - game.game_started_at
                - game.total_paused_seconds,
            )
        return game.game_elapsed(now)

    @staticmethod
    def _paused_total_for_save(game: GameEngine, now: float) -> float:
        total = game.total_paused_seconds
        if (
            game.state == STATE_PAUSED
            and game.pause_started_at is not None
        ):
            total += max(0.0, now - game.pause_started_at)
        return total

    def _session_payload(self, hub) -> dict[str, Any]:
        now = time.monotonic()

        lobby = {
            "player_modules": list(hub.lobby.player_modules),
            "secondary_modules": sorted(hub.lobby.secondary_modules),
            "selected_starter": (
                list(hub.lobby.selected_starter)
                if hub.lobby.selected_starter is not None
                else None
            ),
        }

        game_payload = None
        if hub.game.players:
            game_payload = {
                "players": [
                    {
                        "player_number": player.player_number,
                        "module_id": player.module_id,
                        "slot": player.slot,
                    }
                    for player in hub.game.players
                ],
                "active_index": hub.game.active_index,
                "starter_player": hub.game.starter_player,
                "winner_player": hub.game.winner_player,
                "eliminated_players": list(hub.game.eliminated_players),
                "win_claim_player": hub.game.win_claim_player,
                "win_claim_required": list(hub.game.win_claim_required),
                "win_claim_confirmed": list(hub.game.win_claim_confirmed),
                "win_claim_restore_state": hub.game.win_claim_restore_state,
                "state": hub.state,
                "game_elapsed_seconds": self._game_elapsed_for_save(
                    hub.game,
                    now,
                ),
                "turn_elapsed_seconds": hub.game.current_turn_elapsed(now),
                "paused_seconds": self._paused_total_for_save(hub.game, now),
                "current_warning_ms": hub.game.current_warning_ms,
                "warning_logged_for_turn": hub.game.warning_logged_for_turn,
                "starting_life": hub.game.starting_life,
                "life_totals": {
                    str(number): total
                    for number, total in hub.game.life_totals.items()
                },
                "stats": {
                    str(number): {
                        "turns_completed": stat.turns_completed,
                        "total_turn_seconds": stat.total_turn_seconds,
                    }
                    for number, stat in hub.game.stats.items()
                },
            }

        web_control = getattr(hub, "web_control", None)
        web_claims = (
            web_control.claim_hashes_snapshot()
            if web_control is not None
            else {}
        )

        return {
            "version": 5,
            "saved_at": datetime.now(timezone.utc).isoformat(),
            "hub_state": hub.state,
            "lobby": lobby,
            "game": game_payload,
            # Only token hashes are persisted. Browser bearer tokens never
            # appear in session.json.
            "web_claims": web_claims,
        }

    def mark_dirty(self) -> None:
        self._dirty = True

    def save_session(self, hub) -> Path:
        payload = self._session_payload(hub)

        with self._lock:
            self._atomic_write_json(self.session_path, payload)
            self._dirty = False
            self._last_session_write_monotonic = time.monotonic()
            self.last_session_save_unix = time.time()

        return self.session_path

    def update_autosave(self, hub, now: float | None = None) -> None:
        """Save state after events and periodically during active play."""
        if now is None:
            now = time.monotonic()

        active = hub.state in (
            STATE_STARTING,
            STATE_RUNNING,
            STATE_PAUSED,
        )

        periodic_due = (
            active
            and now - self._last_session_write_monotonic
            >= ACTIVE_AUTOSAVE_SECONDS
        )

        if not self._dirty and not periodic_due:
            return

        try:
            self.save_session(hub)
        except OSError as exc:
            print(f"[PERSIST] Could not save game state: {exc}")

    # ========================================================
    # Session Recovery
    # ========================================================

    @staticmethod
    def _int_or_none(value: Any) -> int | None:
        if value is None:
            return None
        try:
            return int(value)
        except (TypeError, ValueError):
            return None

    @staticmethod
    def _restore_web_claims(hub, data: dict[str, Any]) -> None:
        web_control = getattr(hub, "web_control", None)
        if web_control is None:
            return

        web_control.restore_claim_hashes(
            data.get("web_claims", {})
        )

    def restore_session(self, hub) -> bool:
        data = self._read_json(self.session_path)
        if data is None:
            return False

        lobby_data = data.get("lobby", {})
        if not isinstance(lobby_data, dict):
            lobby_data = {}

        try:
            hub.lobby.player_modules = [
                int(module)
                for module in lobby_data.get("player_modules", [])
                if int(module) in hub.lobby.module_ids
            ]
            hub.lobby.secondary_modules = {
                int(module)
                for module in lobby_data.get("secondary_modules", [])
                if int(module) in hub.lobby.player_modules
            }
        except (TypeError, ValueError):
            hub.lobby.reset_empty()

        starter = lobby_data.get("selected_starter")
        if (
            isinstance(starter, list)
            and len(starter) == 2
        ):
            try:
                seat_key = (int(starter[0]), int(starter[1]))
                if any(
                    player.seat_key == seat_key
                    for player in hub.lobby.players
                ):
                    hub.lobby.selected_starter = seat_key
            except (TypeError, ValueError):
                pass

        saved_state = str(data.get("hub_state", STATE_LOBBY))
        game_data = data.get("game")

        # An interrupted countdown returns to a safe lobby. There is no
        # reason to start a game merely because the process rebooted.
        if saved_state in (STATE_LOBBY, STATE_STARTING) or not isinstance(game_data, dict):
            hub.state = STATE_LOBBY
            hub.game = GameEngine()
            self.recovered_session = bool(hub.lobby.players)
            self._restore_web_claims(hub, data)
            if self.recovered_session:
                print("[PERSIST] Restored lobby player assignments.")
            return self.recovered_session

        raw_players = game_data.get("players", [])
        players: list[PlayerSeat] = []
        if isinstance(raw_players, list):
            for item in raw_players:
                if not isinstance(item, dict):
                    continue
                try:
                    players.append(
                        PlayerSeat(
                            player_number=int(item["player_number"]),
                            module_id=int(item["module_id"]),
                            slot=int(item.get("slot", 1)),
                        )
                    )
                except (KeyError, TypeError, ValueError):
                    continue

        if len(players) < 2:
            hub.state = STATE_LOBBY
            hub.game = GameEngine()
            self._restore_web_claims(hub, data)
            return False

        game = GameEngine()
        game.players = players
        try:
            game.active_index = max(
                0,
                min(int(game_data.get("active_index", 0)), len(players) - 1),
            )
        except (TypeError, ValueError):
            game.active_index = 0

        game.starter_player = self._int_or_none(game_data.get("starter_player"))
        game.winner_player = self._int_or_none(game_data.get("winner_player"))

        valid_player_numbers = {player.player_number for player in players}
        raw_eliminated = game_data.get("eliminated_players", [])
        game.eliminated_players = []
        if isinstance(raw_eliminated, list):
            for value in raw_eliminated:
                try:
                    number = int(value)
                except (TypeError, ValueError):
                    continue
                if (
                    number in valid_player_numbers
                    and number not in game.eliminated_players
                ):
                    game.eliminated_players.append(number)

        game.normalize_active_player()

        # Restore a pending victory claim if it is internally consistent.
        # Recovery itself always returns paused, so a later denial/cancel also
        # remains paused rather than automatically restarting after a reboot.
        claim_player = self._int_or_none(game_data.get("win_claim_player"))
        raw_required = game_data.get("win_claim_required", [])
        raw_confirmed = game_data.get("win_claim_confirmed", [])
        living_numbers = {
            player.player_number
            for player in players
            if player.player_number not in game.eliminated_players
        }

        if claim_player in living_numbers:
            required: list[int] = []
            if isinstance(raw_required, list):
                for value in raw_required:
                    try:
                        number = int(value)
                    except (TypeError, ValueError):
                        continue
                    if (
                        number in living_numbers
                        and number != claim_player
                        and number not in required
                    ):
                        required.append(number)

            confirmed: list[int] = []
            if isinstance(raw_confirmed, list):
                for value in raw_confirmed:
                    try:
                        number = int(value)
                    except (TypeError, ValueError):
                        continue
                    if number in required and number not in confirmed:
                        confirmed.append(number)

            if required and not all(number in confirmed for number in required):
                game.win_claim_player = claim_player
                game.win_claim_required = required
                game.win_claim_confirmed = confirmed
                game.win_claim_restore_state = STATE_PAUSED

        try:
            game.current_warning_ms = int(game_data.get("current_warning_ms", 0))
        except (TypeError, ValueError):
            game.current_warning_ms = 0
        game.warning_logged_for_turn = bool(
            game_data.get("warning_logged_for_turn", False)
        )

        stats_data = game_data.get("stats", {})
        game.stats = {}
        for player in players:
            raw = stats_data.get(str(player.player_number), {}) if isinstance(stats_data, dict) else {}
            try:
                turns = int(raw.get("turns_completed", 0))
                seconds = float(raw.get("total_turn_seconds", 0.0))
            except (AttributeError, TypeError, ValueError):
                turns = 0
                seconds = 0.0
            game.stats[player.player_number] = PlayerStats(
                turns_completed=max(0, turns),
                total_turn_seconds=max(0.0, seconds),
            )

        try:
            game.starting_life = int(
                game_data.get("starting_life", self._starting_life)
            )
        except (TypeError, ValueError):
            game.starting_life = self._starting_life
        game.starting_life = max(
            MIN_STARTING_LIFE,
            min(game.starting_life, MAX_STARTING_LIFE),
        )

        raw_life_totals = game_data.get("life_totals", {})
        game.life_totals = {}
        for player in players:
            raw_total = (
                raw_life_totals.get(str(player.player_number), game.starting_life)
                if isinstance(raw_life_totals, dict)
                else game.starting_life
            )
            try:
                total = int(raw_total)
            except (TypeError, ValueError):
                total = game.starting_life
            game.life_totals[player.player_number] = max(
                -MAX_STARTING_LIFE,
                min(total, MAX_STARTING_LIFE),
            )

        try:
            game_elapsed = max(0.0, float(game_data.get("game_elapsed_seconds", 0.0)))
            turn_elapsed = max(0.0, float(game_data.get("turn_elapsed_seconds", 0.0)))
            paused_seconds = max(0.0, float(game_data.get("paused_seconds", 0.0)))
        except (TypeError, ValueError):
            game_elapsed = 0.0
            turn_elapsed = 0.0
            paused_seconds = 0.0

        now = time.monotonic()
        game.total_paused_seconds = paused_seconds
        game.game_started_at = now - game_elapsed - paused_seconds
        game.turn_started_at = now - turn_elapsed
        game.win_armed_module = None
        game.win_armed_player = None

        if saved_state == STATE_GAME_OVER:
            game.state = STATE_GAME_OVER
            game.game_ended_at = now
            game.pause_started_at = None
            hub.state = STATE_GAME_OVER
            print("[PERSIST] Restored completed game state.")
        else:
            # A live or paused game always returns paused. Downtime never
            # becomes somebody's turn time, and a human must explicitly resume.
            game.state = STATE_PAUSED
            game.game_ended_at = None
            game.pause_started_at = now
            hub.state = STATE_PAUSED
            print(
                "[PERSIST] Recovered active game PAUSED. "
                "Hold Action to resume when the table is ready."
            )

        hub.game = game
        self._restore_web_claims(hub, data)
        self.recovered_session = True
        return True
