# Secure Atlas-Sigil Link (Planned)

Owner direction, 2026-09-28: encrypt the Atlas-Sigil radio link now, before
Sigil OTA, because OTA must send the Atlas Wi-Fi password to a Sigil (see
[Sigil OTA](SIGIL_OTA.md)). It must support at least 8 Sigils. Status of
everything below: *Planned* unless marked otherwise.

## Decision: application-layer encryption, not ESP-NOW's built-in encryption

Decided 2026-09-28 (owner requirement: at least 8 Sigils). ESP-NOW can
encrypt unicast frames itself: each peer gets a 16-byte local master key
(LMK) and the radio applies CCMP. We decided against it for these reasons.

**1. It can't serve 8 Sigils on the core we build with.** The installed
Arduino core (framework-arduinoespressif32 2.0.17) is compiled with
`CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM=7`. *Verified* from its sdkconfig,
2026-09-28. The value is fixed inside the prebuilt Wi-Fi libraries, not a
build flag. Atlas must hold at least 8 Sigils (`MAX_SIGILS = 8`, owner
requirement), and the test harness pairs as two more peers on top. With 7
slots, at least one paired Sigil would always talk in the clear, which defeats
the purpose. Atlas is the side that runs out: each Sigil needs only one
encrypted peer (Atlas), but Atlas needs one per Sigil.

**2. It doesn't give us a key, only a way to use one.** Built-in encryption
assumes both sides already share the LMK. We would still need the key
agreement at pairing (X25519 below) and somewhere safe to keep the keys, so it
saves only the per-packet cipher step, which mbedTLS gives us anyway.

**3. It protects less than TurnHub needs.**
- Replay: we need an old recorded PASS or win claim to be rejected even after
  a reboot. Whether ESP-NOW's CCMP replay counter survives peer re-adds and
  restarts isn't documented well enough to rely on (*Needs verification*); our
  own session nonces and counters make it explicit and host-testable.
- Pairing and broadcast frames can't be encrypted by ESP-NOW either, so the
  pairing exchange has to be designed by us in any case.

**Alternatives considered:**
- *Rebuild the Arduino core with a higher limit* (ESP-IDF allows up to 17):
  means maintaining a custom core build and lib-builder setup for every core
  update, and CI would need it too. Rejected as ongoing maintenance.
- *Rotate which Sigils hold the 7 encrypted slots:* removing and re-adding
  peers costs latency and complexity at exactly the moments the table is
  busy. Rejected.
- *Encrypt 7, leave the rest in the clear:* fails the requirement. Rejected.
- *Switch the Arduino core / framework to raise the limit:* too broad a change
  for this feature; it affects both firmwares, display and SD libraries.

**What our choice costs:**
- 15 bytes per packet (the 110-byte display packet becomes 125 of ESP-NOW's
  250) and a little CPU; the ESP32's hardware AES makes that negligible.
- We own the envelope, nonce and counter code. We use standard mbedTLS
  primitives only (X25519, AES-128-CCM, HMAC-SHA256), never home-made
  algorithms, and the design needs review like any security code.
- Host tests can't link mbedTLS, so real crypto is checked by an on-device
  known-answer self-test and hardware tests.

**Revisit if:** the project moves to a core or framework whose ESP-NOW allows
enough encrypted peers for all Sigils plus the harness, and its replay
behavior is verified. The key agreement and sessions would stay; only the
envelope would change.

The result: TurnHub encrypts its own packets with keys agreed at pairing.
ESP-NOW peers stay unencrypted at the radio level. There is no peer limit, and
the same code runs on Atlas, Sigils, the harness and the Wokwi fake Atlas.

## Today (*Verified* from source, 2026-09-28)

- Every packet is cleartext, and Atlas identifies a Sigil only by its MAC
  address, which anyone can copy. A nearby device could send PASS or a win
  claim as any Sigil, or read everything Atlas sends.
- Pairing: the Sigil broadcasts `PairRequest` with a random token during its
  window; Atlas, if its own window is open, stores the MAC and answers
  `PairAccept` (Sigil ID + token). The Sigil stores Atlas's MAC and its ID in
  NVS. Both are 7-byte `Packet`s.

## Design

### Pairing key agreement

1. The Sigil generates a fresh X25519 key pair and broadcasts a new
   `PairRequest2` with its public key and token (Pair button pressed, window
   open, as today).
2. Atlas (window open) generates its own fresh key pair and answers
   `PairAccept2` with its public key, the Sigil ID and the token.
3. Both compute the X25519 shared secret and derive, with HMAC-SHA256 over
   the secret, both MACs and both public keys:
   - the 16-byte **pair key** (stored in NVS: on Atlas in the Sigil's pairing
     record, on the Sigil with Atlas's MAC); and
   - a 4-digit **pairing code**.
4. The private keys are discarded. Someone who only listens to pairing learns
   nothing that lets them compute the pair key.
5. **Code check (owner decision, 2026-09-28).** Both the Sigil display and the
   Atlas screen show the code. Nothing is stored yet: Atlas holds the Sigil as
   *awaiting confirmation*. The owner checks the codes match and confirms
   with a `PairConfirm` Intent from either:
   - the web portal (Device Settings, and the Android app later over the same
     HTTP route): signed-in Admin, through the same presence-verified
     device-management path as Forget and Factory reset; or
   - a **Confirm** button on the Atlas screen, the fallback for anyone not
     using the portal or app (`AtlasHardware` origin, as `PairRequest`).

   The portal shows the code as text too, so a screen reader user can compare
   it with the Sigil (or have someone read the Sigil).
   On Confirm, Atlas stores the pair key and sends the Sigil a `PairConfirmed`
   sealed with the new key; the Sigil stores the key only when that checks
   out. **Reject** (both places), a mismatch, or no confirmation within 60 s
   discards the key on both sides and nothing is stored. A Sigil that
   impersonator traffic tricked would show a different code, which is what
   the comparison catches. Several Sigils can await confirmation at once;
   each is listed with its own code.

### Sessions (stop replayed packets)

Each time a Sigil connects (boot, Atlas restart, lost link), the Hello
exchange starts a session: the Sigil's Hello carries an 8-byte random nonce
and an HMAC made with the pair key; Atlas answers with its own nonce and HMAC.
Session key = HMAC(pair key, both nonces). A packet recorded in an earlier
session can't be replayed into a new one, and a forged Hello fails its HMAC.

### Secure frame

After the session starts, every packet in both directions (control, display,
picker, menu, life, OTA offer) travels inside one envelope:

```
version | type = Secure | sigilId | counter (4, per direction)
| AES-128-CCM ciphertext of the existing packet | tag (8)
```

The CCM nonce combines the direction, Sigil ID, counter and session, so no
nonce repeats under one key. The receiver requires the counter to rise,
so duplicates and replays inside a session are dropped. Overhead is 15 bytes:
the 110-byte display packet becomes 125, well under ESP-NOW's 250.

Only `PairRequest2`/`PairAccept2` stay cleartext. Atlas drops any other
cleartext or failed-tag packet from a paired MAC and logs it (counts only,
never key material; `printlnRedacted` where needed).

### Compatibility

`TurnHubProtocol::VERSION` goes from 1 to 2. Every paired Sigil must be
reflashed over USB and **paired again once**, because old pairings have no
key; Atlas clears keyless records on first boot of the new firmware.
The harness and the Wokwi fake Atlas get the same code (shared header).

## Feature gate

1. **State owner.** Atlas owns each Sigil's pair key and session state in
   `SigilBus`; each Sigil owns its Atlas pair key. No game state changes.
2. **Intent.** Bind the reserved `PairConfirm` (payload: Sigil slot and
   accept/reject) from the Atlas touchscreen and the portal's
   device-management path. Key agreement and sessions themselves are
   transport, below the adapters.
3. **Validator.** `SigilBus` (and the Sigil receive path): tag, counter,
   session and Sigil ID checks before anything reaches `IntentDispatcher`.
4. **Persistence owner.** NVS: Atlas's existing pairing store (record grows by
   16 bytes) and the Sigil's pairing binding. Keys never leave NVS/RAM, are
   never logged and are erased by forget/unpair/factory reset. NVS isn't
   flash-encrypted, so someone holding a board can read its keys; physical
   extraction is out of scope for now.
5. **Rendering clients.** Sigil display: the pairing code, then paired or
   rejected. Atlas screen: pending Sigils with their codes and Confirm/Reject
   buttons; "pair again" for Sigils cleared by the upgrade. Portal: the same
   pending list with Confirm/Reject, and per-Sigil link status (secure /
   needs pairing). Android: confirmation later, over the portal's route.
6. **Protocol/contract change.** `protocol.h` VERSION 2, `PairRequest2`,
   `PairAccept2`, secure Hello fields and the `Secure` envelope. Both device
   types and the harness must be reflashed together. No HTTP client-contract
   change.
7. **Third-party dependencies.** mbedTLS (Apache-2.0), already inside the
   Arduino-ESP32 core, for X25519, AES-CCM and HMAC-SHA256. Record its use in
   `Documentation/legal/`. Host tests can't link mbedTLS; they test framing,
   counters and session logic through a crypto interface, and firmware runs
   a known-answer self-test at boot.
8. **Accessibility.** A pairing code must also appear in the portal (screen
   reader friendly), not only on the Atlas screen and Sigil display.

## Progress and resume point

Commit after each step and tick it here.

- [x] Design record (2026-09-28).
- [x] Shared `secure_link.h`: envelope layout, nonce, counter/session logic,
      crypto interface; host tests with a test crypto backend (2026-09-28,
      `Sigil/tests/host/secure_link_scenarios.cpp`; Atlas and Sigil host suites pass).
- [x] mbedTLS backend and boot-time known-answer self-test (Atlas and Sigil),
      `shared/include/secure_link_mbedtls.h` and `knownAnswerTest()` in
      `secure_link.h`. *Verified* 2026-09-28 on all three bench boards (Atlas
      `B4:BF:E9:12:85:74`, OLED Sigil `20:E7:C8:94:49:80`, e-ink Sigil
      `F4:65:0B:C4:FF:38`; CI build of `a010dd2`): `SECURE_LINK|SELF_TEST|PASS`
      in 1614-1615 ms. Nearly all of that is 8 X25519 operations (about 200 ms
      each in mbedTLS's generic bignum code), so pairing costs about 0.4 s per
      side. Follow-up: trim the boot test (for example to one RFC agreement)
      or run it off the boot path, since it delays every start by 1.6 s.
- [x] **Bench result, 2026-09-29 (*Verified*):** CI build `be7ec45` on Atlas
      `B4:BF:E9:12:85:74`, e-ink Sigil `F4:65:0B:C4:FF:38` and OLED Sigil
      `20:E7:C8:94:49:80`. Both Sigils booted `PAIR|LOADED|KEYLESS`, then
      re-paired: Atlas `PAIRING|V2|CODE_SHOWN`, Sigil `PAIR|V2|CODE_SHOWN`,
      owner tapped Codes match on the Atlas screen, Atlas
      `PAIRING|V2|CONFIRMED` (slots 0 and 1) and each Sigil
      `PAIR|SUCCESS|SECURE`. The Sigil only accepts a confirmation whose MAC
      checks with its own new key, so both ends derived the same key (the MAC
      inputs to the derivation are right). Not yet exercised on hardware: Reject,
      the 60 s timeout, the portal's Codes match, and forged or replayed verdicts.
- [x] Pairing v2 on Atlas and Sigil; pair key in NVS (2026-09-29, branch
      `secure-link-pairing-v2`; host-tested and CI-built, *Needs
      verification* on hardware). Shared state machines in
      `shared/include/pairing_v2.h` (`Sigil/tests/host/pairing_scenarios.cpp`;
      the host stand-in crypto is `Sigil/tests/host/test_crypto.h`). Atlas:
      `SigilBus` takes 38-byte `PairRequest2` in its window and answers
      `PairAccept2`; the key goes in NVS `th_pair_v1/k<slot>` beside the MAC
      (`s<slot>`), so records from before load as keyless. Sigil: Pair
      broadcasts `PairRequest2`; the key goes in `th_pair_v1/atlas_k` beside the
      7-byte binding. "Keyless records cleared" moves to the envelope step
      (below), which is when they stop working.
- [x] Pairing code on the Sigil and Atlas screens; `PairConfirm` Intent with
      Confirm/Reject on the Atlas screen and in the portal; 60 s timeout
      (2026-09-29; host-tested, *Needs verification* on hardware). Atlas
      screen: a `PairCode` screen over the lobby (after any presence code),
      **Codes match** / **Reject**, one Sigil at a time. Portal: Device
      Settings lists waiting Sigils with their codes in a live region;
      `POST /api/device/pair-confirm` needs an Admin verified at the table.
      Sigils show the code (e-ink: "Code 0427 / Confirm on Atlas"; OLED: big
      Oswald digits) with the pairing light on, and give up after 65 s.
      Atlas's on-board LED blinks red while its pairing window is open.
      **Transition (decided 2026-09-29):** `VERSION` stays 1 and all other
      traffic stays cleartext until the envelope step. Atlas still accepts the
      old 7-byte `PairRequest` (stored keyless), so the TestHarness keeps
      pairing the old way until it learns v2; Sigils built from this branch
      send only v2. The portal marks keyless Sigils "pair again for the secure
      link".
- [ ] Secure Hello/session and the envelope on every packet, both directions.
      **In progress, branch `secure-link-envelope` (from 2026-09-29).** Plan,
      in commit order (tick as done; resume from the first unticked):
      1. [ ] Shared `secure_session.h`: Sigil and Atlas session endpoints over
         `Channel` (SecureHello with a fresh nonce and MAC, SecureHelloAck,
         session key), host-tested.
      2. [ ] `VERSION` 1 -> 2; receive buffers take sealed frames (22, 66 and
         125 bytes) and the 23/27-byte handshake.
      3. [ ] Atlas: a session per paired Sigil; SecureHello starts or replaces
         it (and counts as a Hello); every send to a Sigil is sealed in the app
         task (counters stay in order); every non-pairing packet from a Sigil
         must open. Keyless records are forgotten at boot.
      4. [ ] Sigil: SecureHello when there is no session or Atlas went quiet
         (> 2 Hello intervals), otherwise a sealed Hello as the keep-alive;
         everything it sends is sealed, and only sealed frames from its Atlas
         are handled. A keyless binding counts as unpaired.
      5. [ ] TestHarness and the Wokwi fake Atlas do the same.
      6. [ ] Remove the old `PairRequest`/`PairAccept` path.
      7. [ ] Docs and manual; CI; flash all four boards; bench test (play, Atlas
         restart recovery, forged/replayed/cleartext packets rejected).
- [ ] Harness and Wokwi shim updated; portal link status; manual and docs.
      Done so far: the Wokwi fake Atlas speaks v2 (`confirm` / `reject` on the
      console); portal shows secure vs keyless; engineering docs and the manual's
      pairing section; the TestHarness (2026-09-29, `6ad6eb4`): `pair` runs v2
      for each virtual Sigil without a key and stores the keys
      (`th_harness/pairk`). *Verified* on the bench the same day: harness
      `D4:E9:F4:B4:27:3C` re-paired V1 (station MAC, slot 2, code 1587) and V2
      (soft-AP MAC `...:3D`, slot 3, code 6902) with two pending at once, both
      confirmed on the Atlas screen one after the other and logged
      `HARNESS|PAIR|ACCEPTED|...|SECURE`. That also proves the derivation with
      a sender on the soft-AP interface. Nothing still uses the old
      `PairRequest`; it can go with the envelope step.
- [ ] Hardware: 8 Sigils (or harness plus Sigils) paired and playing; forged,
      replayed and cleartext packets rejected.

## Open decisions

1. **Pairing code check.** *Decided 2026-09-28:* code on the Sigil and Atlas
   screens, confirmed in the portal/app or with the Atlas screen button (see
   step 5 of the key agreement). Listening can't break the key agreement; the
   code check stops a device actively impersonating both sides during the
   window.
2. **Upgrade path.** Re-pair every Sigil once after the reflash (planned
   above), or keep accepting cleartext from old pairings for a while.
   *Planned: re-pair once*, since every Sigil needs a USB flash anyway.
