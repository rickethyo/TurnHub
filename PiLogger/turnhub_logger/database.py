from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Iterable, Mapping


SCHEMA = """
CREATE TABLE IF NOT EXISTS events (
    row_id INTEGER PRIMARY KEY AUTOINCREMENT,
    received_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    atlas_boot_id TEXT NOT NULL,
    event_id INTEGER NOT NULL,
    atlas_uptime_ms INTEGER NOT NULL,
    level TEXT NOT NULL,
    category TEXT NOT NULL,
    message TEXT NOT NULL,
    UNIQUE(atlas_boot_id, event_id)
);
CREATE INDEX IF NOT EXISTS idx_events_received_at ON events(received_at);
CREATE INDEX IF NOT EXISTS idx_events_category ON events(category);
"""


class EventStore:
    def __init__(self, path: str) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.path)
        self.connection.executescript(SCHEMA)
        self.connection.commit()

    def close(self) -> None:
        self.connection.close()

    def add_events(self, boot_id: str, events: Iterable[Mapping[str, object]]) -> int:
        inserted = 0
        for event in events:
            cursor = self.connection.execute(
                """
                INSERT OR IGNORE INTO events
                    (atlas_boot_id, event_id, atlas_uptime_ms, level, category, message)
                VALUES (?, ?, ?, ?, ?, ?)
                """,
                (
                    boot_id,
                    int(event["id"]),
                    int(event.get("uptime_ms", 0)),
                    str(event.get("level", "INFO")),
                    str(event.get("category", "GENERAL")),
                    str(event.get("message", "")),
                ),
            )
            inserted += cursor.rowcount
        self.connection.commit()
        return inserted
