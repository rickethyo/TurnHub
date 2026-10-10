# First-run Setup

The guided setup for a new or factory-reset Atlas (2026-09-30), in Atlas, the
Android app and the embedded portal, and in use (owner, 2026-10-02). It is the
implemented part of the broader provisioning ideas in `Atlas/OOBE.md`.

## Principles

- **Local-first:** no cloud account or internet; everything runs on Atlas's
  own Wi-Fi.
- **Physical presence:** a new Atlas is claimed by the first account set up on
  its own Wi-Fi (in range of the table, with the printed password). The
  six-digit presence code is an optional extra step, off by default since
  2026-10-08; with it on, the table code step asks for it
  ([Players and Accounts](PLAYERS_AND_ACCOUNTS.md)). The app skips that step
  when Atlas reports the account verified.
- **The public default Wi-Fi password must be replaced** before setup can
  finish. The default `TurnHub-Setup` lives in `Atlas/include/config.h`
  (`WIFI_DEFAULT_PASSWORD`) and `WifiCredentials.DEFAULT_ATLAS_PASSPHRASE`;
  keep them in sync.
- **Never block play:** **Skip for now** on the Atlas screen lets a table
  play first; setup comes back at the next start-up and under Menu.
- **One flow, app first:** Atlas defines the steps and endpoints; the app and
  the portal both follow them. The app is easiest because it joins Atlas's
  Wi-Fi by itself. Setup gives the network as typed text and doesn't depend
  on QR codes.

## The flow

```text
Atlas, stage Welcome: "Welcome to TurnHub"
  1. Open the TurnHub app and tap Connect (it joins the table's Wi-Fi)
  2. No app? Wi-Fi TurnHub-Atlas, password TurnHub-Setup, then 192.168.4.1
  [Pair a Sigil] [Skip for now] [Menu]

Phone, "Set up this table" (Step n of 5):
  1. Your account   create one or sign in
  2. At the table   type the code Atlas shows; this account becomes the Admin
  3. Sigils         pair each one (code check on the Atlas screen), or skip
  4. Updates        Atlas and every Sigil: running vs available; Install all
                    or Later. Atlas first, then each Sigil
  5. Secure Wi-Fi   choose the table's password (8-63 characters);
                    Finish saves it and restarts Atlas

Atlas restarts, stage Finished: "You're all set. Next: pair your Sigils."
  [Pair a Sigil] [Done]  -> stage Complete
  (skipped straight to Complete if a real Sigil was already paired)
```

The app reads `GET /api/setup` after Connect and opens setup while the stage
is Welcome. After Atlas restarts on the new password, the app rejoins with the
password it just set. On launch it rejoins its saved table; a phone with none,
or whose table no longer answers, offers "Set up a new table". The portal
pack runs steps 1, 2 and 5; pairing and updates stay on the Atlas screen and
in the app.

**Updates during setup** use the GitHub release feed
([Firmware Updates](FIRMWARE_UPDATES.md)). They come after the table code
(installing needs a verified Admin) and pairing (one prompt for every
device), and before the password change (so every restart still uses the
printed password). The app's network specifier leaves the phone's own
internet as the default network, so downloads go over the phone's connection.
With no internet the step says so and Continue works. Atlas sessions and
presence are RAM only, so after Atlas restarts the app signs in again and asks
for one new code before updating the Sigils.

## Stages and storage

`SetupStage` in NVS `turnhub/setup` (`setup_stage.h`):

| Stage | Meaning | Atlas screen |
|---|---|---|
| `Welcome` (0) | New or factory-reset | Welcome |
| `Finished` (1) | The phone finished | "You're all set" once |
| `Complete` (2) | Normal operation | Ordinary lobby |

No record at boot reads as Complete if an Admin exists, otherwise Welcome. A
storage failure reads as Complete (logged `ATLAS|SETUP|STAGE|LOAD_FAILED`),
so a broken NVS never locks the table in setup. Factory reset returns to
Welcome. Stages never go backwards otherwise.

## Feature gate

1. **State owner:** Atlas (`setup_stage.h`; `beginFirstRunSetup` in `table_intents.cpp` loads it at boot). Skip
   for now is RAM-only touchscreen state.
2. **Intent:** `AdvanceSetup`, `value` = target stage. To Finished: a
   presence-verified Admin from a phone. To Complete: the touchscreen or an
   Admin.
3. **Validator:** `handleAdvanceSetupIntent`: to Finished needs a verified
   Admin, Lobby or Game Over, and an owner-set Wi-Fi password already saved
   (with a real Sigil paired it stores Complete instead); to Complete needs
   stage Finished.
4. **Persistence:** `turnhub/setup`; the Wi-Fi password keeps its own store.
5. **Rendering:** the touchscreen (`ScreenKind::Setup`), the app's setup
   steps and the embedded portal.
6. **Contract:** portal-private routes, not part of `/api/v1`:
   `GET /api/setup` (no sign-in; stage, `adminExists`, `passwordIsDefault`,
   SSID, never the password) and `POST /api/setup/finish` (`password=`;
   saves the password, then the stage, then restarts; a refused
   `AdvanceSetup` means no restart). If power is cut between the two writes,
   setup simply runs again.
7. **Dependencies:** none.
8. **Accessibility:** every step is text on the Atlas screen and an
   accessible form on the phone; nothing relies on color, sound or LEDs.

## Status

Host scenario `firstRunSetup`, Android JVM tests (`AtlasSetupAssistantTest`,
`FirmwareReleasesTest`) and a full owner run on hardware (2026-09-30; an app
crash on the final reconnect was fixed the same day, log in
[history/logs](history/logs/README.md)). Still open: a "Setting up a new
table" section in the user manual, and an Atlas name step later.
