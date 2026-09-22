# Profile login and virtual participation

Priority: profile login and fully virtual/mixed play precede game-scoped statistics.

Implementation: Atlas `0.6.0-dev`; automated verification passed. The owner
reports a successful compile/flash and working behavior on 2026-09-20, with the
physical reuse gap below. Native Android UI remains future work; browser phones
use the shared Atlas authorization and Intent services now.

Known remaining gap: physical join still uses the Sigil's saved profile binding.
If that person has joined by phone, duplicate prevention blocks physical join;
the Sigil cannot select another person using its buttons yet. Browser-assisted
attachment remains available. [Physical Profile Selection](PHYSICAL_PROFILE_SELECTION.md)
records the next implementation; saved preference must be separated from live
assignment rather than weakening duplicate protection.

## Required behavior

- A person can create a profile and sign in without any physical Sigil.
- Phones can form a complete table, select a starter, start, play, finish and rematch.
- Logging in does not join a game. Joining uses the authenticated profile.
- One profile has at most one participant at the current table. Multiple browser
  sessions and a physical Sigil may all control that same participant.
- Reauthentication reconnects to participation; it does not create another player.
- Profile name, credentials and statistics remain owned by Atlas independently of
  every controller. Logout removes authorization, not the profile or participant.
- Physical controller attachment is an explicit, physically confirmed lobby action.
  It cannot silently replace an active game's player. Phones can accompany an
  already attached physical Sigil throughout a game.

## Ownership and feature gate

1. Profiles/credentials belong to the profile repository; browser tokens belong to
   authentication. Participation and controller assignments belong to Atlas's
   application layer. The engine owns all game transitions.
2. Registration/login/logout are authentication requests. Join/leave, attachment,
   starter/start/reset/rematch and gameplay enter the Intent dispatcher.
3. The HTTP adapter authenticates the token and obtains its profile, never trusting
   a client-supplied player identity. Atlas resolves that profile to current
   participation and performs state/ownership/capacity checks.
4. Profiles retain the existing NVS repository and statistics format. Browser
   sessions and live participation remain RAM state; reboot requires login and
   resets the table, consistent with current ESP32 game recovery limitations.
5. Portal and Sigils render canonical state. The browser does not advance turns,
   resolve victory, or independently count completed games.
6. Browser endpoints expand; ESP-NOW packet layout remains compatible. Internal
   controller handles are not physical device IDs. Browser controllers never
   appear as invented radio devices.
7. Use existing framework cryptography/storage; do not add firmware dependencies.
8. New flows use labeled forms, keyboard-accessible controls, persistent text
   feedback and explicit confirmations. Physical possession remains an optional
   attachment path, never a requirement for phone-only participation.

## Verification gate

Test zero-hardware registration/login/complete game/rematch; mixed tables; two
phone sessions plus a Sigil controlling one profile; duplicate joins; logout and
re-login during a match; unauthorized/other-profile controls; host restrictions;
capacity; PIN failure throttling; statistics once per participant; existing
physical/shared-seat regression scenarios. Hardware radio/flash/browser behavior
requires a separate bench acceptance run after automated checks.

## Implemented boundaries and compatibility

Local follow-up (2026-09-21): Settings adds owner-authenticated
`POST /api/session/policy` with explicit `0`/`1` choices for
`allowPhysicalWithoutPin` and `hideStatsWithoutAuthentication`.
`/api/session/me` reports `policyAvailable` and the two choices when readable.
PIN-protected profiles require PIN authentication to obtain a browser session;
physical confirmation alone remains available only to bootstrap PIN-less profiles.
New physical joins respect the owner policy without disrupting existing matches.
See [physical-selection implementation scope](PHYSICAL_PROFILE_SELECTION.md).

- `profile_store` owns profile creation/listing and persistence. Credential
  initialization publishes the existing marker last. Existing IDs and v1 stats
  remain unchanged. New profiles require a name and PIN.
- `web_api` authenticates profile sessions, throttles PIN entry, and translates
  authorized requests through callbacks. Multiple tokens may reference one
  profile. Token expiry/logout do not alter the game roster. PIN changes revoke
  other tokens. The current PIN hash representation is retained; strengthening
  password derivation requires its own backward-compatible credential migration.
- `controller_profiles` translates physical controller bindings and allocates
  independent browser registrations. It does not change game state or create
  fake radio records. Browser handles occupy a distinct internal range.
- Atlas application handlers implement JoinProfile/LeaveProfile/BindProfile and
  resolve authenticated profiles to canonical participation. Existing gameplay
  requests still use the same handlers as physical input. A profile cannot join
  twice. New physical attachment requires real Action input and a lobby state;
  it targets an unjoined primary Sigil seat. Existing shared-seat participation
  and companion browser sessions remain supported.
- Lobby issues an Atlas-local numeric `participantId`, retained while at the
  table and through phone-to-physical attachment. This runtime handle is not the
  future durable 128-bit match ParticipantId contract. ProfileId remains durable.
- `PlayerSeat`/`IntentActor` now use the internal name `controllerId`. Existing
  browser `module` fields remain compatibility coordinates, not authentication
  authority; ordinary phone controls never accept these as the requesting actor.
- The game-start boundary captures each participant's profile ID. Completed-game
  statistics use that capture, not the current radio MAC or browser token.
- `GameEngine` imports no profile repository, authentication service, NVS, HTTP
  or radio service. Its controller handles are transport-independent; physical
  LED/audio adapters continue to address only real radio controller handles.

Browser additions: `GET /api/profiles`, `POST /api/profiles/register`, profileId+PIN
on `POST /api/session/login`, `POST /api/session/join` and `/leave`, plus lifecycle
controls `/api/control/start`, `/cancel-start`, `/rematch` and `/reset`.
`/api/session/me` returns authentication separately from `participating`, `host`
and `virtual`. The authenticated profile always determines control/statistics
scope. The login directory exposes names/IDs/PIN-presence, never credentials or
performance statistics. Registration is available to local network clients,
bounded at 64 profiles; broader administration policy remains future work.

## Validation record

- Existing six native gameplay/storage-policy groups pass.
- Real HTTP handlers plus actual Intent handlers/engine pass profile creation,
  hardware-free login/game/rematch/reset, duplicate join prevention, concurrent
  browser sessions, logout/re-login, host/actor checks, PIN throttling/revocation,
  mixed attachment, phone/physical pass parity and statistics counted once.
- A 16-player phone-controller scenario verifies roster bounds and win responses.
- Existing identity/statistics persistence scenarios pass.
- A focused browser smoke check passes profile creation, join/start controls,
  mobile fit, wider desktop layout and absence of JavaScript errors. Its HTTP
  responses are fixtures; native tests exercise the real API implementation.
- Native tests substitute storage, radio, clock and SHA implementation. They
  verify flow/ownership, not physical persistence or cryptographic strength.

Bench acceptance: flash Atlas without erasing NVS; check an old profile, run a
two-phone game with Sigils off, check statistics/re-login, then attach a Sigil
in the lobby and exercise that same player from both phone and buttons. Reboot
to confirm profile/PIN/statistics persistence. No claim of active-game recovery
or seamless cross-device/Atlas restart is made.
