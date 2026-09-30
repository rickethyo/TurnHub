# First-run guided setup (out-of-box experience)

Branch `oobe-guided-setup`, started 2026-09-30. Status: *Planned*, then
*Experimental* as each step below lands; nothing here is hardware-verified.

This is the first implementation slice of [Atlas OOBE](../../Atlas/OOBE.md)
("3. Setup wizard"). It builds on pieces that already exist: first-Admin
setup (`/api/accounts/setup`), table presence codes (`front_panel.cpp`), the
Wi-Fi password endpoint, and factory reset.

## What it should feel like

The model is a small consumer device such as a smart speaker, streaming stick
or Wi-Fi router. You power it on, the device itself tells you the one thing
to do next, your phone walks you through a few short screens, and the device
then says "You're all set" and offers the obvious next step. The rules this
product adds:

- **Local-first.** No cloud account and no internet. Everything runs on
  Atlas's own Wi-Fi.
- **Physical presence.** Only someone at the table can claim a new Atlas. The
  existing six-digit presence code proves it.
- **The public default Wi-Fi password must be replaced** (OOBE.md): setup
  can't finish without an owner-chosen password.
- **Never block play.** A table that wants to play first can tap **Skip for
  now**. Sigils and phones join as usual, and setup comes back at the next
  start-up and under Menu.
- **One flow, app first** (owner, 2026-09-30). Atlas defines the steps and
  client-neutral endpoints; the Android app and the portal both run through
  the same flow. The app is built first because it is much easier to connect:
  it joins Atlas's Wi-Fi by itself (`TargetedAtlasWifiLink`, default
  passphrase first). The portal follows with the same steps on the same
  endpoints; until then its existing first-Admin banner still works.
- **No QR codes needed.** Owner, 2026-09-30: the QR codes have seen little
  use. The Atlas screen gives setup as typed text (network name, password,
  address). QR codes stay where they already are, but setup doesn't depend on
  them.
- **Accessibility.** Every step is text on the Atlas screen and an accessible
  web form on the phone (labels, live status region, keyboard, WCAG 2.2 AA
  target). Nothing relies on color, sound or LEDs.

## The flow

```text
Atlas powers on, setup stage = Welcome
  Atlas screen: "Welcome to TurnHub"
    1. Open the TurnHub app and tap Connect
       (it joins the table's Wi-Fi itself)
    2. No app? Wi-Fi TurnHub-Atlas,
       password TurnHub-Setup, then 192.168.4.1
    [Skip for now] [Menu]

Android app, "Set up this table" (one step per screen, "Step n of 5"):
  1. Your account   create one (name + PIN) or sign in to an existing one
  2. At the table   Atlas shows a six-digit code; type it on the phone.
                    That makes this account the Admin.
  3. Sigils         pair each Sigil (the usual code check on the Atlas
                    screen); "I have no Sigils yet" skips it
  4. Updates        one prompt for every device: Atlas and each paired Sigil,
                    running and available versions. Install all (recommended)
                    or Later. Atlas first, then the Sigils one at a time.
  5. Secure Wi-Fi   choose the table's own Wi-Fi password (8-63 characters);
                    Finish saves it, marks setup finished and restarts Atlas

Atlas restarts, setup stage = Finished
  Atlas screen: "You're all set"
    Rejoin TurnHub-Atlas with the new password.
    Next: pair your Sigils.
    [Pair a Sigil] [Done]   (either one leaves setup: stage = Complete)
```

After Connect, the app reads `GET /api/setup`. While the stage is Welcome it
opens the setup steps instead of the table view. After the restart, the app
rejoins with the password it just set (it saves it, like any password it
used), so the owner doesn't retype it.

## Checking for updates during setup

Owner, 2026-09-30: check for updates during setup, with GitHub hosting the
firmware "at least for now". This reuses the Sigil OTA delivery design
([Sigil OTA](SIGIL_OTA.md), "Release feed" and "App delivery"):

- **Host:** GitHub Releases on the public `rickethyo/TurnHub` repository. A
  `v*` tag runs `release.yml`, which builds, signs and publishes the `.thfw`
  packages and `turnhub-firmware.json`. The app reads
  `https://github.com/rickethyo/TurnHub/releases/latest/download/turnhub-firmware.json`
  with no token (public repository). Moving to another host later only
  changes that URL. *Planned:* no release has been published yet, so until
  the first one the step reports "no release published yet" and moves on.
- **Where in setup:** after the table code (installing needs a verified
  Admin) and after pairing, so one prompt updates every device and gets it
  out of the way (owner, 2026-09-30: "update all devices at the same time
  initially ... if the user accepts the update prompt"). Atlas updates first
  and the app waits for it to come back, then each Sigil in turn (the Sigil
  OTA order). All of it comes before the Wi-Fi password, so every restart
  still uses the printed password and the app reconnects by itself. Atlas
  sessions and table verification are RAM-only, so after Atlas restarts the
  app signs in again with the PIN from step 1 and asks for one new table code
  before updating the Sigils.
- **Internet:** the app joins Atlas with a network specifier, which leaves
  the phone's own internet as the default network, so the feed and package
  download go over the phone's connection while the install goes to Atlas.
  With no internet, the step says so and offers Skip; setup never depends on
  it.
- **Trust:** the app checks each download's size and SHA-256 against the
  feed; Atlas checks the package signature itself on `/api/firmware` and
  refuses anything unsigned or for another board. A tampered feed can only
  point at a package that fails those checks.
- **Never forced:** Later always works; an out-of-date Atlas still plays.

## Setup stages

`SetupStage` is stored in NVS (`turnhub` namespace, key `setup`, schema 1,
two bytes `{1, stage}`):

| Stage | Meaning | Atlas screen (lobby, nothing else open) |
|---|---|---|
| `Welcome` (0) | New or factory-reset Atlas | Welcome instructions |
| `Finished` (1) | The phone finished the wizard | "You're all set" once |
| `Complete` (2) | Normal operation | The ordinary lobby |

- **No record at boot:** Complete if an Admin already exists (an Atlas set up
  before this feature: no surprise wizard after a firmware update), otherwise
  Welcome. The result is saved.
- **Factory reset** erases NVS, so the Atlas starts again at Welcome.
- **Storage failure** reads as Complete, so a broken NVS can never lock the
  table in setup. It is logged `ATLAS|SETUP|STAGE|LOAD_FAILED`.

## Feature gate

1. **State owner:** Atlas. The new `first_run_setup.cpp` holds the stage.
   **Skip for now** is RAM-only presentation state on the touchscreen
   (Invariant 6), not a table decision.
2. **Intent:** `IntentType::AdvanceSetup`, `payload.value` = target stage.
   Welcome to Finished comes from the portal (a presence-verified Admin,
   `payload.moderatorId`). Finished to Complete comes from the touchscreen
   (`IntentOrigin::AtlasHardware`) or a portal Admin. Stages never go
   backwards; only factory reset returns to Welcome.
3. **Validator:** `handleAdvanceSetupIntent`. To Finished: an Admin, verified
   at the table, between games (Lobby or GameOver), with an owner-set Wi-Fi
   password already stored. To Complete: the current stage is Finished.
4. **Persistence owner:** `first_run_setup.cpp` through the `turnhub`
   `NvsBlobStore`, like the pairing window and speaker volume. The Wi-Fi
   password keeps its own store (`wifi_password_store.h`).
5. **Rendering clients:** the Atlas touchscreen (`ScreenKind::Setup`) and the
   Android app's setup steps, both reading `GET /api/setup`. The portal is
   unchanged apart from what the new stage needs; its first-Admin banner
   still works.
6. **Protocol/contract:** no radio change. New portal-private HTTP routes
   `GET /api/setup` and `POST /api/setup/finish`. They are not part of the
   `/api/v1` client contract, so no schema change.
7. **Third-party dependencies:** none.
8. **Accessibility:** covered above. The touchscreen uses the existing 60 px
   button rows and text lines.

## HTTP

- `GET /api/setup`, no sign-in: `{"stage":"welcome|finished|complete",
  "adminExists":bool,"passwordIsDefault":bool,"ssid":"TurnHub-Atlas"}`. It
  reveals nothing `/api/accounts/setup` and `/api/network` don't already
  reveal to their callers, and never the password.
- `POST /api/setup/finish` (`password=`), a presence-verified Admin, stage
  Welcome: saves the Wi-Fi password, dispatches `AdvanceSetup` to Finished,
  answers, then restarts Atlas so the new password takes effect. If
  `AdvanceSetup` is refused, the answer is the refusal and Atlas does not
  restart.

The password is written before the stage. If power is cut in between, Atlas
comes back at Welcome with an Admin and a private password, and the wizard
simply runs again: sign in, verify, re-enter a password.

## Resume checklist

- [x] Design and feature gate (this file); branch created.
- [x] `setup_stage.h`: stage codec, load/save (`game_settings_store.cpp`),
      boot migration (`beginFirstRunSetup` in `table_intents.cpp`).
- [x] `IntentType::AdvanceSetup`, handler, binding, host scenario
      `firstRunSetup`.
- [x] Touchscreen `ScreenKind::Setup` (Welcome with Pair a Sigil, Skip and
      Menu; "You're all set" with Pair a Sigil and Done), Setup under Menu.
      Drawn by the generic text-lines layout; no `atlas_art.cpp` layout change.
- [x] `GET /api/setup`, `POST /api/setup/finish` (host scenario).
- [x] Android: `AtlasSetupAssistant`, `SetupScreen`, `HomeViewModel` wiring,
      account registration, `GitHubFirmwareReleases`, `UpdatePlan`,
      multipart package upload; JVM tests (`AtlasSetupAssistantTest`,
      `FirmwareReleasesTest`).
- [x] Source lists: no new Atlas `.cpp` files, so the runners are unchanged.
- [ ] Firmware build through CI on this branch (started 2026-09-30).
- [ ] Portal: the same steps on the same endpoints (after the app).
- [ ] Owner: publish a first signed GitHub release (tag `v*`) so the update
      step has something to find; needs the `TURNHUB_FIRMWARE_SIGNING_KEY`
      secret.
- [ ] User manual: a "Setting up a new table" section (not written yet).
- [ ] Size history snapshot once CI reports the Atlas image size.
- Later: Atlas name.

## Verification

- *Verified on the host (2026-09-30):* all Atlas host scenarios including
  `firstRunSetup`, the adapter audit and the client-contract check (MinGW
  runner); all 129 Android JVM tests; `assembleDebug` builds.
- *Needs verification* on hardware, from a factory-reset Atlas:
  1. Welcome shows on the Atlas screen; Skip hides it until restart; Setup
     under Menu brings it back; Pair a Sigil opens pairing and the countdown.
  2. The app: Connect shows setup; create an account; the code screen on
     Atlas makes it the Admin.
  3. Pair two Sigils (one of each display); the app lists them.
  4. With a published release: install all. Atlas restarts, the app
     reconnects and signs in by itself, one new code, then each Sigil
     updates; interrupt one (power) and confirm the app reports it and setup
     continues. Without internet: the step says so and Continue works.
  5. Finish with a new password: Atlas restarts, the app rejoins by itself,
     Atlas shows "You're all set"; Done returns to the lobby; a restart
     afterwards shows the ordinary lobby.
  6. An Atlas already set up before this firmware boots straight to the
     lobby (it has an Admin).
