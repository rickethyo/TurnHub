from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import requests


@dataclass(frozen=True)
class AtlasEventBatch:
    boot_id: str
    latest: int
    events: list[dict[str, Any]]


class AtlasClient:
    def __init__(self, base_url: str, timeout_seconds: float = 2.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout_seconds = timeout_seconds

    def events_after(self, event_id: int) -> AtlasEventBatch:
        response = requests.get(
            f"{self.base_url}/api/debug/events",
            params={"after": event_id},
            timeout=self.timeout_seconds,
        )
        response.raise_for_status()
        payload = response.json()
        return AtlasEventBatch(
            boot_id=str(payload["boot_id"]),
            latest=int(payload.get("latest", event_id)),
            events=list(payload.get("events", [])),
        )
