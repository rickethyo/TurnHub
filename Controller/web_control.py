"""Secure local web-controller seat pairing for TurnHub."""

from __future__ import annotations

import hashlib
import hmac
import secrets
import threading
import time

from dataclasses import dataclass
from typing import Any

from player import PlayerSeat
from lobby import VIRTUAL_MODULE_BASE

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
        # Current-game association between a logical seat and persistent profile.
        self._profile_by_seat: dict[tuple[int, int], str] = {}

        # Short-lived confirmation requests. The raw bearer token exists only
        # here long enough for the requesting browser to collect it.
        self._pending: dict[str, PendingWebClaim] = {}
        # Browser-first physical join. The next unclaimed Sigil Action press
        # joins that module and binds this browser to it.
        self._pending_physical_join: dict[str, Any] | None = None

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

    def profile_assignments_snapshot(self) -> dict[str, str]:
        with self._lock:
            return {self.seat_key_text(k): v for k, v in self._profile_by_seat.items()}

    def restore_profile_assignments(self, raw: Any) -> None:
        if not isinstance(raw, dict):
            return
        valid = {p.seat_key for p in self._current_players()}
        restored: dict[tuple[int, int], str] = {}
        for key, profile_id in raw.items():
            seat_key = self.parse_seat_key(key)
            if seat_key in valid and self.turnhub.profiles.get(str(profile_id)) is not None:
                restored[seat_key] = str(profile_id)
        with self._lock:
            self._profile_by_seat = restored

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

    def profile_id_for_seat(self, seat_key: tuple[int, int]) -> str | None:
        with self._lock:
            return self._profile_by_seat.get(seat_key)

    def profile_for_seat(self, seat_key: tuple[int, int]) -> dict[str, Any] | None:
        profile_id = self.profile_id_for_seat(seat_key)
        return self.turnhub.profiles.get(profile_id) if profile_id else None

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
            "profile_id": self.profile_id_for_seat(seat_key),
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

    def create_virtual_player(self, name: str, profile_id: str | None = None, pin: str = "") -> tuple[bool, dict[str, Any], int]:
        """Join a browser-native player without requiring physical hardware."""
        if self.turnhub.state != STATE_LOBBY:
            return False, {"error": "players can only join in the lobby"}, 409
        if profile_id:
            profile = self.turnhub.profiles.get(profile_id)
            if profile is None:
                return False, {"error": "unknown player profile"}, 404
            if profile.get("has_pin") and not self.turnhub.profiles.authenticate(profile_id, pin):
                return False, {"error": "incorrect profile PIN"}, 401
            name = profile.get("name", name)
        player = self.turnhub.lobby.add_virtual_player()
        if player is None:
            return False, {"error": "could not create virtual player"}, 500
        clean = str(name or "").strip()[:40]
        if clean:
            snap = self.turnhub.persistence.settings_snapshot()
            seats = dict(snap.get("seat_names", {}))
            seats[self.seat_key_text(player.seat_key)] = clean
            self.turnhub.persistence.update_names(seat_names=seats)
        token = secrets.token_urlsafe(32)
        with self._lock:
            self._claims[player.seat_key] = self._token_hash(token)
            if profile_id:
                self._profile_by_seat[player.seat_key] = str(profile_id)
        self.turnhub.persistence.mark_dirty()
        return True, {"token": token, "seat_key": list(player.seat_key), "mode": "virtual", "profile_id": profile_id}, 200

    def _next_virtual_module(self) -> int:
        used = {p.module_id for p in (self.turnhub.game.players or self.turnhub.lobby.players)}
        module = VIRTUAL_MODULE_BASE
        while module in used:
            module += 1
        return module

    def switch_to_virtual(self, token: str | None) -> tuple[bool, dict[str, Any], int]:
        """Move the authenticated active-game player from physical to virtual control."""
        if self.turnhub.state not in (STATE_RUNNING, STATE_PAUSED):
            return False, {"error": "controller switching is available during an active game"}, 409
        with self._lock:
            old_key = self._token_seat_locked(token)
            if old_key is None:
                return False, {"error": "invalid web-controller token"}, 401
            old_hash = self._claims.get(old_key)
        player = self._player_for_seat(old_key)
        if player is None:
            return False, {"error": "player is not part of this game"}, 409
        if player.module_id >= VIRTUAL_MODULE_BASE:
            return False, {"error": "player is already virtual"}, 409
        if player.slot != 1 or any(p.module_id == player.module_id and p.slot == 2 for p in self.turnhub.game.players):
            return False, {"error": "shared physical Sigils cannot switch controllers mid-game yet"}, 409
        new_module = self._next_virtual_module()
        replacement = PlayerSeat(player.player_number, new_module, 1)
        self.turnhub.game.players = [replacement if p.player_number == player.player_number else p for p in self.turnhub.game.players]
        # Keep rematch assignments aligned with the controller change.
        try:
            idx = self.turnhub.lobby.player_modules.index(player.module_id)
            self.turnhub.lobby.player_modules[idx] = new_module
            if new_module not in self.turnhub.lobby.module_ids:
                self.turnhub.lobby.module_ids.append(new_module)
        except ValueError:
            pass
        snap = self.turnhub.persistence.settings_snapshot()
        seats = dict(snap.get("seat_names", {}))
        old_text, new_text = self.seat_key_text(old_key), self.seat_key_text(replacement.seat_key)
        if old_text in seats:
            seats[new_text] = seats.pop(old_text)
            self.turnhub.persistence.update_names(seat_names=seats)
        new_token = secrets.token_urlsafe(32)
        with self._lock:
            self._claims.pop(old_key, None)
            self._claims[replacement.seat_key] = self._token_hash(new_token)
            profile_id = self._profile_by_seat.pop(old_key, None)
            if profile_id:
                self._profile_by_seat[replacement.seat_key] = profile_id
        self.turnhub.persistence.mark_dirty()
        return True, {"token": new_token, "seat_key": list(replacement.seat_key), "mode": "virtual"}, 200

    def request_switch_to_physical(self, token: str | None) -> tuple[bool, dict[str, Any], int]:
        """Arm an active virtual player's browser to claim an available physical Sigil."""
        if self.turnhub.state not in (STATE_RUNNING, STATE_PAUSED):
            return False, {"error": "controller switching is available during an active game"}, 409
        if not self.turnhub.hardware_enabled:
            return False, {"error": "physical Sigils are disabled in Virtual Only mode"}, 409
        with self._lock:
            seat_key = self._token_seat_locked(token)
            if seat_key is None:
                return False, {"error": "invalid web-controller token"}, 401
            player = self._player_for_seat(seat_key)
            if player is None or player.module_id < VIRTUAL_MODULE_BASE:
                return False, {"error": "this player is not using a virtual Sigil"}, 409
            if self._pending_physical_join is not None:
                return False, {"error": "another physical pairing is already in progress"}, 409
            request_id = secrets.token_urlsafe(18)
            self._pending_physical_join = {"request_id":request_id,"created_at":time.monotonic(),"status":"pending","mode":"switch","old_seat_key":seat_key,"player_number":player.player_number}
        return True, {"request_id":request_id,"status":"pending","message":"Press Action on an available physical Sigil."}, 202

    def request_physical_join(self, name: str, profile_id: str | None = None) -> tuple[bool, dict[str, Any], int]:
        """Arm browser-first pairing; next available physical Sigil Action wins."""
        if self.turnhub.state != STATE_LOBBY:
            return False, {"error": "players can only join in the lobby"}, 409
        if profile_id:
            profile = self.turnhub.profiles.get(profile_id)
            if profile is None:
                return False, {"error": "unknown player profile"}, 404
            name = profile.get("name", name)
        if not self.turnhub.hardware_enabled:
            return False, {"error": "physical Sigils are disabled in Virtual Only mode"}, 409
        with self._lock:
            if self._pending_physical_join is not None:
                return False, {"error": "another physical join is already waiting for a Sigil"}, 409
            request_id = secrets.token_urlsafe(18)
            self._pending_physical_join = {"request_id": request_id, "name": str(name or "").strip()[:40], "profile_id": profile_id, "created_at": time.monotonic(), "status": "pending"}
        return True, {"request_id": request_id, "status": "pending", "message": "Press Action on an available physical Sigil."}, 202

    def confirm_physical_join(self, module_id: int) -> dict[str, Any] | None:
        with self._lock:
            pending = self._pending_physical_join
            if pending is None:
                return None
            if pending.get("mode") == "switch":
                if self.turnhub.state not in (STATE_RUNNING, STATE_PAUSED):
                    return None
                if any(p.module_id == module_id for p in self.turnhub.game.players):
                    return None
                old_key = tuple(pending["old_seat_key"])
                player = self._player_for_seat(old_key)
                if player is None:
                    return None
                replacement = PlayerSeat(player.player_number, module_id, 1)
                self.turnhub.game.players = [replacement if p.player_number == player.player_number else p for p in self.turnhub.game.players]
                try:
                    idx = self.turnhub.lobby.player_modules.index(player.module_id)
                    self.turnhub.lobby.player_modules[idx] = module_id
                except ValueError:
                    pass
                snap = self.turnhub.persistence.settings_snapshot()
                seats = dict(snap.get("seat_names", {}))
                old_text, new_text = self.seat_key_text(old_key), self.seat_key_text(replacement.seat_key)
                if old_text in seats:
                    seats[new_text] = seats.pop(old_text)
                    self.turnhub.persistence.update_names(seat_names=seats)
                token = secrets.token_urlsafe(32)
                self._claims.pop(old_key, None)
                self._claims[replacement.seat_key] = self._token_hash(token)
                profile_id = self._profile_by_seat.pop(old_key, None)
                if profile_id:
                    self._profile_by_seat[replacement.seat_key] = profile_id
                pending.update({"status":"confirmed","token":token,"seat_key":replacement.seat_key,"module_id":module_id})
                self.turnhub.persistence.mark_dirty()
                return {"player": replacement, "module_id": module_id}
            if self.turnhub.state != STATE_LOBBY:
                return None
            if self.turnhub.lobby.is_joined(module_id):
                return None
            self.turnhub.lobby.join(module_id)
            player = self.turnhub.lobby.primary_player(module_id)
            if player is None:
                return None
            token = secrets.token_urlsafe(32)
            self._claims[player.seat_key] = self._token_hash(token)
            if pending.get("profile_id"):
                self._profile_by_seat[player.seat_key] = str(pending["profile_id"])
            pending.update({"status":"confirmed", "token":token, "seat_key":player.seat_key, "module_id":module_id})
            name = pending.get("name", "")
            if name:
                snap = self.turnhub.persistence.settings_snapshot()
                seats = dict(snap.get("seat_names", {}))
                seats[self.seat_key_text(player.seat_key)] = name
                self.turnhub.persistence.update_names(seat_names=seats)
            self.turnhub.persistence.mark_dirty()
            return {"player": player, "module_id": module_id}

    def physical_join_status(self, request_id: str) -> tuple[dict[str, Any], int]:
        with self._lock:
            p = self._pending_physical_join
            if p is None or p.get("request_id") != request_id:
                return {"status":"expired", "error":"join request not found"}, 404
            if time.monotonic() - p["created_at"] > WEB_CLAIM_TIMEOUT_SECONDS:
                self._pending_physical_join = None
                return {"status":"expired", "error":"join request expired"}, 404
            out = {k:v for k,v in p.items() if k not in {"created_at","name"}}
            if isinstance(out.get("seat_key"), tuple): out["seat_key"] = list(out["seat_key"])
            if p.get("status") == "confirmed":
                self._pending_physical_join = None
            return out, 200

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
