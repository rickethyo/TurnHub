# TurnHub Flight Recorder

Development telemetry companion for TurnHub Atlas.

The Flight Recorder runs on a Raspberry Pi connected as a Wi-Fi client to the Atlas access point. It is intentionally non-authoritative: Atlas and Sigils must continue to operate normally if the Pi is absent, offline, rebooting, or loses telemetry.

## Initial architecture

- `turnhub_logger/atlas_client.py` polls Atlas for debug events.
- `turnhub_logger/database.py` stores received events in SQLite.
- `turnhub_logger/main.py` runs the polling loop.
- `config.example.toml` documents local configuration.
- `systemd/turnhub-flight-recorder.service` provides an eventual Pi service unit.

The expected Atlas endpoint is intentionally provisional while the Atlas telemetry API is implemented:

`GET /api/debug/events?after=<event_id>`

Expected response shape:

```json
{
  "boot_id": "A31F",
  "latest": 12349,
  "events": [
    {
      "id": 12346,
      "uptime_ms": 193821,
      "level": "INFO",
      "category": "TURN",
      "message": "P1 -> P2"
    }
  ]
}
```

The Pi adds its own wall-clock receive timestamp. `boot_id` separates Atlas boot sessions so event IDs may safely restart after an Atlas reboot.

## Safety rules

- Telemetry is best-effort and must never be required for gameplay.
- The recorder must tolerate Atlas disappearing or rebooting.
- Do not store PINs, passwords, session tokens, Wi-Fi credentials, or other secrets in event payloads.
- A telemetry failure must not trigger gameplay actions.

## Quick start

1. Copy `config.example.toml` to `config.toml` and adjust it for the Atlas AP.
2. Create a virtual environment.
3. Install `requirements.txt`.
4. Run `python -m turnhub_logger.main --config config.toml`.

This scaffold does not yet add the Atlas telemetry endpoint or a web dashboard.