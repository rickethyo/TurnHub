from __future__ import annotations

import argparse
import logging
import time
import tomllib
from pathlib import Path

import requests

from .atlas_client import AtlasClient
from .database import EventStore


def load_config(path: str) -> dict:
    with Path(path).open("rb") as handle:
        return tomllib.load(handle)


def run(config: dict) -> None:
    atlas = config["atlas"]
    storage = config["storage"]
    recorder = config.get("recorder", {})

    logging.basicConfig(
        level=getattr(logging, str(recorder.get("log_level", "INFO")).upper()),
        format="%(asctime)s %(levelname)s %(message)s",
    )

    client = AtlasClient(
        str(atlas["base_url"]),
        float(atlas.get("request_timeout_seconds", 2.0)),
    )
    store = EventStore(str(storage["database"]))
    interval = float(atlas.get("poll_interval_seconds", 1.0))

    boot_id: str | None = None
    last_event_id = 0

    logging.info("TurnHub Flight Recorder starting")
    try:
        while True:
            try:
                batch = client.events_after(last_event_id)
                if boot_id != batch.boot_id:
                    logging.info("Atlas boot session: %s", batch.boot_id)
                    boot_id = batch.boot_id
                    last_event_id = 0
                    if batch.events and int(batch.events[0].get("id", 0)) > 1:
                        batch = client.events_after(0)

                inserted = store.add_events(batch.boot_id, batch.events)
                if batch.events:
                    last_event_id = max(int(event["id"]) for event in batch.events)
                else:
                    last_event_id = max(last_event_id, batch.latest)
                if inserted:
                    logging.info("Stored %d Atlas event(s)", inserted)
            except (requests.RequestException, ValueError, KeyError, TypeError) as exc:
                logging.warning("Atlas telemetry unavailable: %s", exc)

            time.sleep(interval)
    except KeyboardInterrupt:
        logging.info("TurnHub Flight Recorder stopping")
    finally:
        store.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="TurnHub Raspberry Pi flight recorder")
    parser.add_argument("--config", default="config.toml")
    args = parser.parse_args()
    run(load_config(args.config))


if __name__ == "__main__":
    main()
