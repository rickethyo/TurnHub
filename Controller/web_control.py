"""Secure local web-controller seat pairing for TurnHub."""

from __future__ import annotations

import hashlib
import hmac
import secrets
import threading
import time

from dataclasses import dataclass
from typing import Any

from config import (
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    WEB_CLAIM_TIMEOUT_SECONDS,
)


@dataclass
class PendingWebClaim:
    request_id: str
    seat_key: tuple[int, int]
    confirm_module: int
    mode: str
    created_at: float
    expires_at: float
    status: str = "pending"
    token: str | None = None
    message: str = ""


class WebControlManager:
    """Owns browser-to-seat claims and server-side pass authorization."""

    def __init__(self, turnhub) -> None:
        self.turnhub = turnhub
        self._lock = threading.RLock()

        # Physical seat key -> SHA-256 bearer-token hash.
        self._claims: dict[tuple[int, int], str] = {}

        # Short-lived confirmation requests. The raw bearer token exists only
        # here long enough for the requesting browser to collect it.
        self._pending: dict[str, PendingWebClaim] = {}

    # ========================================================
    # Helpers
    # ========================================================

    @staticmethod
    def seat_key_text(seat_key: tuple[int, int]) -> str:
        return f"{int(seat_key[0])}:{int(seat_key[1])}"

    @staticmethod
    def parse_seat_key(value: Any) -> tuple[int, int] | None:
        if isinstance(value, (list, tuple)) and len(value) == 2:
            try:
                module_id = int(value[0])
                slot = int(value[1])
            except (TypeError, ValueError):
                return None
            if slot not in (1, 2):
                return None
            return (module_id, slot)

        if isinstance(value, str) and ":" in value:
            left, right = value.split(":", 1)
            try:
                module_id = int(left)
                slot = int(right)
            except ValueError:
                return None
            if slot not in (1, 2):
                return None
            return (module_id, slot)

        return None

    @staticmethod
    def _token_hash(token: str) -> str:
        return hashlib.sha256(token.encode("utf-8")).hexdigest()

    def _current_players(self):
        if self.turnhub.game.players:
            return list(self.turnhub.game.players)
        return list(self.turnhub.lobby.players)

    def _player_for_seat(self, seat_key: tuple[int, int]):
        for player in self._current_players():
            if player.seat_key == seat_key:
                return player
        return None

    def _cleanup_pending_locked(self, now: float | None = None) -> None:
        if now is None:
            now = time.monotonic()

        expired = [
            request_id
            for request_id, pending in self._pending.items()
            if now >= pending.expires_at
        ]

        for request_id in expired:
            self._pending.pop(request_id, None)

    def _pending_for_confirm_module_locked(
        self,
        module_id: int,
    ) -> PendingWebClaim | None:
        self._cleanup_pending_locked()

        candidates = [
            pending
            for pending in self._pending.values()
            if (
                pending.status == "pending"
                and pending.confirm_module == module_id
            )
        ]

        if not candidates:
            return None

        return min(candidates, key=lambda item: item.created_at)

    def _token_seat_locked(self, token: str | None) -> tuple[int, int] | None:
        if not token:
            return None

        candidate = self._token_hash(token)

        for seat_key, stored_hash in self._claims.items():
            if hmac.compare_digest(candidate, stored_hash):
                return seat_key

        return None

    # ========================================================
    # Public Claim State
    # ========================================================

    def claim_hashes_snapshot(self) -> dict[str, str]:
        """Return only hashes for persistence, never browser bearer tokens."""
        with self._lock:
            return {
                self.seat_key_text(seat_key): token_hash
                for seat_key, token_hash in self._claims.items()
            }

    def restore_claim_hashes(self, raw: Any) -> None:
        """Restore persisted seat claims, discarding seats no longer present."""
        if not isinstance(raw, dict):
            return

        valid_seats = {
            player.seat_key
            for player in self._current_players()
        }

        restored: dict[tuple[int, int], str] = {}

        for key, value in raw.items():
            seat_key = self.parse_seat_key(key)
            if seat_key is None or seat_key not in valid_seats:
                continue

            token_hash = str(value).strip().lower()
            if len(token_hash) != 64:
                continue

            try:
                int(token_hash, 16)
            except ValueError:
                continue

            restored[seat_key] = token_hash

        with self._lock:
            self._claims = restored
            self._pending.clear()

    def reconcile_claims(self) -> None:
        """Drop claims/pending work for physical seats that no longer exist."""
        valid_seats = {
            player.seat_key
            for player in self._current_players()
        }

        changed = False

        with self._lock:
            for seat_key in list(self._claims):
                if seat_key not in valid_seats:
                    self._claims.pop(seat_key, None)
                    changed = True

            for request_id, pending in list(self._pending.items()):
                if pending.seat_key not in valid_seats:
                    self._pending.pop(request_id, None)

        if changed:
            self.turnhub.persistence.mark_dirty()

    def clear_all_claims(self) -> None:
        with self._lock:
            changed = bool(self._claims)
            self._claims.clear()
            self._pending.clear()

        if changed:
            self.turnhub.persistence.mark_dirty()

    def claimed_seats(self) -> set[tuple[int, int]]:
        with self._lock:
            return set(self._claims)

    # ========================================================
    # Browser Identity
    # ========================================================

    def identity_for_token(self, token: str | None) -> dict[str, Any] | None:
        with self._lock:
            seat_key = self._token_seat_locked(token)

        if seat_key is None:
            return None

        player = self._player_for_seat(seat_key)
        if player is None:
            return None

        return {
            "seat_key": list(seat_key),
            "module_id": player.module_id,
            "slot": player.slot,
            "player_number": player.player_number,
            "can_pass": (
                self.turnhub.state == STATE_RUNNING
                and self.turnhub.game.active_player is not None
                and self.turnhub.game.active_player.seat_key == seat_key
            ),
        }

    # ========================================================
    # Claim / Reassignment Requests
    # ========================================================

    def _new_request_locked(
        self,
        seat_key: tuple[int, int],
        confirm_module: int,
        mode: str,
        message: str,
    ) -> PendingWebClaim:
        now = time.monotonic()
        request_id = secrets.token_urlsafe(24)

        pending = PendingWebClaim(
            request_id=request_id,
            seat_key=seat_key,
            confirm_module=confirm_module,
            mode=mode,
            created_at=now,
            expires_at=now + WEB_CLAIM_TIMEOUT_SECONDS,
            message=message,
        )

        self._pending[request_id] = pending
        return pending

    def request_lobby_claim(
        self,
        seat_key: tuple[int, int],
    ) -> tuple[bool, dict[str, Any], int]:
        """Begin a lobby seat claim requiring Action on that seat's module."""
        if self.turnhub.state != STATE_LOBBY:
            return False, {"error": "player claims are only available in the lobby"}, 409

        player = self._player_for_seat(seat_key)
        if player is None:
            return False, {"error": "that player seat is not currently joined"}, 404

        with self._lock:
            self._cleanup_pending_locked()

            if seat_key in self._claims:
                return False, {"error": "that player already has a web controller"}, 409

            if self._pending_for_confirm_module_locked(player.module_id) is not None:
                return False, {"error": "that module already has a pending web pairing"}, 409

            virtual_mode = not self.turnhub.hardware_enabled
            pending = self._new_request_locked(
                seat_key=seat_key,
                confirm_module=player.module_id,
                mode="claim",
                message=(
                    "Pairing virtual player controller."
                    if virtual_mode
                    else (
                        f"Press Action briefly on Module {player.module_id} "
                        "to confirm this browser."
                    )
                ),
            )

        # Virtual Mode has no physical Sigil to prove possession of.  The seat
        # itself is the software controller, so complete the same claim through
        # the existing authorization path immediately.
        if virtual_mode:
            self.confirm_physical_action(player.module_id)

        return True, {
            "request_id": pending.request_id,
            "status": pending.status,
            "mode": pending.mode,
            "confirm_module": pending.confirm_module,
            "expires_in_seconds": WEB_CLAIM_TIMEOUT_SECONDS,
            "message": pending.message,
        }, 202

    def request_paused_reassignment(
        self,
        seat_key: tuple[int, int],
    ) -> tuple[bool, dict[str, Any], int]:
        """Begin a paused-game takeover requiring host physical approval."""
        if self.turnhub.state != STATE_PAUSED:
            return False, {"error": "web-controller reassignment requires a paused game"}, 409

        if self.turnhub.game.has_win_claim:
            return False, {"error": "resolve the pending victory claim before reassigning a web controller"}, 409

        player = self._player_for_seat(seat_key)
        if player is None:
            return False, {"error": "that player seat is not part of this game"}, 404

        if self.turnhub.game.is_eliminated(player.player_number):
            return False, {"error": "eliminated players do not need a web controller"}, 409

        host_module = self.turnhub.lobby.host_module
        if host_module is None:
            return False, {"error": "no host module is available to approve reassignment"}, 409

        with self._lock:
            self._cleanup_pending_locked()

            if self._pending_for_confirm_module_locked(host_module) is not None:
                return False, {"error": "the host already has a pending web-controller approval"}, 409

            pending = self._new_request_locked(
                seat_key=seat_key,
                confirm_module=host_module,
                mode="reassign",
                message=(
                    f"Host approval required: press Action briefly on Module "
                    f"{host_module}."
                ),
            )

        return True, {
            "request_id": pending.request_id,
            "status": pending.status,
            "mode": pending.mode,
            "confirm_module": pending.confirm_module,
            "expires_in_seconds": WEB_CLAIM_TIMEOUT_SECONDS,
            "message": pending.message,
        }, 202

    def claim_status(
        self,
        request_id: str,
    ) -> tuple[dict[str, Any], int]:
        with self._lock:
            self._cleanup_pending_locked()
            pending = self._pending.get(request_id)

            if pending is None:
                return {"status": "expired", "error": "pairing request expired or was cancelled"}, 404

            payload: dict[str, Any] = {
                "request_id": pending.request_id,
                "status": pending.status,
                "mode": pending.mode,
                "confirm_module": pending.confirm_module,
                "seat_key": list(pending.seat_key),
                "message": pending.message,
            }

            if pending.status == "confirmed" and pending.token is not None:
                payload["token"] = pending.token

            return payload, 200

    def confirm_physical_action(self, module_id: int) -> dict[str, Any] | None:
        """Consume a matching short Action press and approve one pending request."""
        with self._lock:
            pending = self._pending_for_confirm_module_locked(module_id)
            if pending is None:
                return None

            if pending.mode == "claim":
                if self.turnhub.state != STATE_LOBBY:
                    self._pending.pop(pending.request_id, None)
                    return None

                if pending.seat_key in self._claims:
                    self._pending.pop(pending.request_id, None)
                    return None

            elif pending.mode == "reassign":
                if self.turnhub.state != STATE_PAUSED:
                    self._pending.pop(pending.request_id, None)
                    return None
            else:
                self._pending.pop(pending.request_id, None)
                return None

            token = secrets.token_urlsafe(32)
            self._claims[pending.seat_key] = self._token_hash(token)

            pending.status = "confirmed"
            pending.token = token
            pending.expires_at = time.monotonic() + WEB_CLAIM_TIMEOUT_SECONDS
            pending.message = "Web controller paired."

            player = self._player_for_seat(pending.seat_key)

        self.turnhub.persistence.mark_dirty()

        return {
            "mode": pending.mode,
            "seat_key": pending.seat_key,
            "player": player,
            "confirm_module": module_id,
        }

    def cancel_pending(self) -> None:
        with self._lock:
            self._pending.clear()

    # ========================================================
    # Release / Authorization
    # ========================================================

    def release_token(self, token: str | None) -> tuple[bool, dict[str, Any], int]:
        if self.turnhub.state != STATE_LOBBY:
            return False, {"error": "web-controller identity is locked after the game starts"}, 409

        with self._lock:
            seat_key = self._token_seat_locked(token)
            if seat_key is None:
                return False, {"error": "invalid web-controller token"}, 401

            self._claims.pop(seat_key, None)

        self.turnhub.persistence.mark_dirty()
        return True, {"released": True, "seat_key": list(seat_key)}, 200

    def authorize_living_player(
        self,
        token: str | None,
    ) -> tuple[bool, Any | None, str]:
        """Validate a browser token and return its living logical player."""
        with self._lock:
            seat_key = self._token_seat_locked(token)

        if seat_key is None:
            return False, None, "invalid web-controller token"

        player = self._player_for_seat(seat_key)
        if player is None:
            return False, None, "that player seat is not part of this game"

        if (
            self.turnhub.game.players
            and self.turnhub.game.is_eliminated(player.player_number)
        ):
            return False, player, "eliminated players cannot control the game"

        return True, player, ""

    def authorize_pass(
        self,
        token: str | None,
    ) -> tuple[bool, tuple[int, int] | None, str]:
        """Validate token, exact active seat, and current running state."""
        if self.turnhub.state != STATE_RUNNING:
            return False, None, "game is not running"

        with self._lock:
            seat_key = self._token_seat_locked(token)

        if seat_key is None:
            return False, None, "invalid web-controller token"

        active = self.turnhub.game.active_player
        if active is None or active.seat_key != seat_key:
            return False, seat_key, "this browser does not control the active player"

        return True, seat_key, ""
