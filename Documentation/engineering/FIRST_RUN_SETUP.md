# Atlas setup

Atlas provisioning is separate from first-time app setup. The app works without
an Atlas. Choose **Set up an Atlas** during app onboarding or later under
More > Atlas connection.

## Provisioning

1. Scan the Wi-Fi QR on Atlas Welcome with your phone camera. It joins
   TurnHub-Atlas without typing a password.
2. Open TurnHub and choose Set up an Atlas. The app connects to
   `https://192.168.4.1` using a restricted TLS bootstrap: compare the
   fingerprint shown on the phone with the code on Atlas, tap Allow on Atlas,
   then confirm the match in the app. Only then does the app save the key and
   continue with account setup or sign-in.
3. Pair Sigils now or later. Prototype firmware updates use USB; choose Later.
4. Enter the home network name and password, or leave the password blank for
   an open network. Choose upload approval for images.
5. Atlas joins the home network while its setup hotspot stays available.
   The app learns the home address and HTTPS key pin over the already pinned
   setup connection.
6. Finish saves the setup stage and restarts Atlas with the hotspot off.
   The app releases the temporary setup connection and reconnects over home
   Wi-Fi. If needed, join that network in Android settings and Find Atlas on Wi-Fi.

Failed joins or missing HTTPS keys leave setup open. Skip postpones setup;
it does not complete it. There is no browser portal.

All network access to Atlas uses HTTPS, including the setup hotspot. Atlas's
self-signed certificate is not trusted automatically: the app limits first
contact to discovery and phone pairing, requires the physical fingerprint
comparison above, and rejects a later key change. Plain HTTP is not an
external endpoint.

## Network policy

Only SetupStage::Welcome starts the hotspot. Finished and Complete use station
mode, even if home Wi-Fi is unset, disabled or unreachable. Router outages
never reopen a hotspot. Sigils use ESP-NOW and find Atlas on the router channel.
Factory reset at the device returns Atlas to Welcome; it removes accounts,
records, settings and pairings. A replacement network can be saved while the
app is connected. An inaccessible Atlas needs physical reset to provision again.

## State and API

NVS turnhub/setup stores Welcome (0), Finished (1) or Complete (2). A missing
record with an Admin boots Complete; without an Admin it boots Welcome.
Unreadable storage boots Complete. setup_stage.h owns storage and
handleAdvanceSetupIntent in device_intents.cpp validates transitions.

- GET /api/setup is public: stage, Admin existence, SSID, passwordIsDefault
  and upload approval. It never returns a password.
- POST /api/network/home saves the home network using verified Admin access.
  GET /api/network reports connection and HTTPS readiness. GET /api/v1/info
  carries the home endpoint and key for pinning.
- POST /api/setup/finish accepts uploadApprovalRequired=0|1. A verified Admin,
  Welcome, no running game, and enabled/configured/connected home Wi-Fi with
  a ready HTTPS key are required. It advances to Finished (Complete with a
  paired Sigil), responds with restarting:true, and restarts with the hotspot
  off. It does not change the hotspot password.
- AdvanceSetup to Complete acknowledges Finished from the screen or an Admin.
  Stages never go backwards except through factory reset.

The standard WPA Wi-Fi QR includes the current setup password, escaped for
punctuation; the shipped password is TurnHub-Setup. Host tests cover boot
network policy, QR, authorization and incomplete networking. Android tests
cover provisioning, pin-before-finish, reconnect and failed joins. Physical
camera scanning and router-outage checks remain hardware acceptance.
