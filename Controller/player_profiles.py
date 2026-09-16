"""Persistent local player profiles for TurnHub.

Profiles identify people across games. A PlayerSeat still identifies a seat in one
specific game, and WebControlManager still owns the controller/session binding.
No cloud account is required.
"""
from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import secrets
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

MAX_NAME_LENGTH = 40
MAX_AVATAR_BYTES = 512 * 1024
ALLOWED_AVATAR_TYPES = {"image/png": ".png", "image/jpeg": ".jpg", "image/webp": ".webp"}


class PlayerProfileStore:
    """Own local profiles, PIN hashes, avatar metadata, preferences and history."""

    def __init__(self, data_directory: Path, venue_mode: bool = False) -> None:
        self.data_directory = Path(data_directory)
        self.path = self.data_directory / "profiles.json"
        self.avatar_directory = self.data_directory / "avatars"
        self.venue_mode = bool(venue_mode)
        self._lock = threading.RLock()
        self._profiles: dict[str, dict[str, Any]] = {}
        self._load()

    @staticmethod
    def _clean_name(value: Any) -> str:
        return " ".join(str(value or "").replace("\x00", "").split())[:MAX_NAME_LENGTH]

    @staticmethod
    def _pin_hash(pin: str, salt: bytes | None = None) -> str:
        salt = salt or secrets.token_bytes(16)
        digest = hashlib.pbkdf2_hmac("sha256", pin.encode("utf-8"), salt, 180_000)
        return f"pbkdf2_sha256$180000${base64.b64encode(salt).decode()}${base64.b64encode(digest).decode()}"

    @staticmethod
    def _verify_pin(pin: str, encoded: str) -> bool:
        try:
            scheme, rounds, salt64, digest64 = encoded.split("$", 3)
            if scheme != "pbkdf2_sha256": return False
            salt = base64.b64decode(salt64)
            expected = base64.b64decode(digest64)
            actual = hashlib.pbkdf2_hmac("sha256", pin.encode("utf-8"), salt, int(rounds))
            return hmac.compare_digest(actual, expected)
        except (ValueError, TypeError):
            return False

    def _load(self) -> None:
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except (FileNotFoundError, OSError, json.JSONDecodeError):
            return
        profiles = raw.get("profiles", {}) if isinstance(raw, dict) else {}
        if isinstance(profiles, dict):
            self._profiles = {str(k): v for k, v in profiles.items() if isinstance(v, dict)}

    def _save(self) -> None:
        self.data_directory.mkdir(parents=True, exist_ok=True)
        temp = self.path.with_suffix(".tmp")
        temp.write_text(json.dumps({"version": 1, "profiles": self._profiles}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        temp.replace(self.path)

    @staticmethod
    def _public(profile: dict[str, Any]) -> dict[str, Any]:
        result = dict(profile)
        result.pop("pin_hash", None)
        result["has_pin"] = bool(profile.get("pin_hash"))
        return result

    def list_profiles(self) -> list[dict[str, Any]]:
        with self._lock:
            return [self._public(v) for v in sorted(self._profiles.values(), key=lambda p: str(p.get("name", "")).casefold())]

    def get(self, profile_id: str) -> dict[str, Any] | None:
        with self._lock:
            profile = self._profiles.get(str(profile_id))
            return self._public(profile) if profile else None

    def create(self, name: str, pin: str = "") -> dict[str, Any]:
        clean = self._clean_name(name)
        if not clean: raise ValueError("profile name is required")
        if pin and (not pin.isdigit() or len(pin) < 4 or len(pin) > 12):
            raise ValueError("PIN must be 4 to 12 digits")
        profile_id = uuid.uuid4().hex
        now = datetime.now(timezone.utc).isoformat()
        profile = {"id": profile_id, "name": clean, "created_at": now, "updated_at": now,
                   "avatar": None, "preferences": {"browser_sound": True, "browser_vibration": True, "volume": "medium"},
                   "history": [], "pin_hash": self._pin_hash(pin) if pin else ""}
        with self._lock:
            self._profiles[profile_id] = profile
            self._save()
        return self._public(profile)

    def update(self, profile_id: str, *, name: Any = None, pin: Any = None, preferences: Any = None) -> dict[str, Any]:
        with self._lock:
            profile = self._profiles.get(str(profile_id))
            if not profile: raise KeyError("unknown profile")
            if name is not None:
                clean = self._clean_name(name)
                if not clean: raise ValueError("profile name is required")
                profile["name"] = clean
            if pin is not None:
                text = str(pin)
                if text and (not text.isdigit() or len(text) < 4 or len(text) > 12): raise ValueError("PIN must be 4 to 12 digits")
                profile["pin_hash"] = self._pin_hash(text) if text else ""
            if isinstance(preferences, dict):
                prefs = profile.setdefault("preferences", {})
                if "browser_sound" in preferences: prefs["browser_sound"] = bool(preferences["browser_sound"])
                if "browser_vibration" in preferences: prefs["browser_vibration"] = bool(preferences["browser_vibration"])
                if preferences.get("volume") in ("low", "medium", "high"): prefs["volume"] = preferences["volume"]
            profile["updated_at"] = datetime.now(timezone.utc).isoformat()
            self._save()
            return self._public(profile)

    def authenticate(self, profile_id: str, pin: str) -> bool:
        with self._lock:
            profile = self._profiles.get(str(profile_id))
            if not profile: return False
            encoded = str(profile.get("pin_hash", ""))
            return bool(encoded) and self._verify_pin(str(pin), encoded)

    def set_avatar(self, profile_id: str, mime_type: str, payload: bytes, *, allow_player_upload: bool = True) -> dict[str, Any]:
        if self.venue_mode and allow_player_upload:
            raise PermissionError("player avatar uploads are disabled in venue mode")
        if mime_type not in ALLOWED_AVATAR_TYPES: raise ValueError("avatar must be PNG, JPEG, or WebP")
        if not payload or len(payload) > MAX_AVATAR_BYTES: raise ValueError("avatar must be 512 KB or smaller")
        with self._lock:
            profile = self._profiles.get(str(profile_id))
            if not profile: raise KeyError("unknown profile")
            self.avatar_directory.mkdir(parents=True, exist_ok=True)
            suffix = ALLOWED_AVATAR_TYPES[mime_type]
            filename = f"{profile_id}{suffix}"
            for old in self.avatar_directory.glob(f"{profile_id}.*"):
                try: old.unlink()
                except OSError: pass
            (self.avatar_directory / filename).write_bytes(payload)
            profile["avatar"] = {"mime_type": mime_type, "filename": filename}
            profile["updated_at"] = datetime.now(timezone.utc).isoformat()
            self._save()
            return self._public(profile)

    def append_history(self, profile_id: str, entry: dict[str, Any]) -> None:
        with self._lock:
            profile = self._profiles.get(str(profile_id))
            if not profile: return
            history = profile.setdefault("history", [])
            history.append(dict(entry))
            if len(history) > 250: del history[:-250]
            profile["updated_at"] = datetime.now(timezone.utc).isoformat()
            self._save()
