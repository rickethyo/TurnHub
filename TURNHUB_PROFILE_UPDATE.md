# TurnHub Profile + Virtual Sigil Feedback Update

Implemented against the uploaded TurnHub(6) snapshot.

## Added
- Persistent local player profiles (`profiles.json` under TurnHub data directory)
- Optional 4-12 digit profile PINs stored with salted PBKDF2-SHA256 hashes
- Saved profile selection when joining a table
- Guest players remain supported
- Physical profile join still requires pressing Action on the selected Sigil
- Virtual profile join verifies the profile PIN when one is configured
- Profile association survives controller switches and session autosave/recovery
- Profile-linked game history summary and completed-game history records
- Home avatar upload from browser, resized/cropped client-side to 256x256 JPEG
- Avatar files stored locally under the TurnHub data directory
- Venue mode disables player avatar uploads when TURNHUB_VENUE_MODE=1
- Virtual Sigil sound, vibration, and volume preferences per profile
- Browser feedback for turn pass, pause/resume, game over, targeted nudge, and table nudge
- Avatars render on player cards when present

## Validation
- All Controller Python files compile successfully.
- Embedded portal JavaScript passes node --check.
- git diff --check passes.
- PlayerProfileStore create/auth/update/avatar/history/venue-policy smoke test passes.
- Full runtime HTTP smoke test was blocked in this build sandbox because pyserial is not installed here.
