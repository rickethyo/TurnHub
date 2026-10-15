# TurnHub Privacy Policy

Effective 15 October 2026. This policy covers the TurnHub Android app
(`com.turnhub.android`) and the TurnHub table hardware it controls.

## The short version

TurnHub does not collect your data. The app has no ads, analytics, tracking
or crash reporting, and TurnHub runs no servers. Everything you enter stays on
your phone or on your own TurnHub table controller (the "Atlas"). You can play
on one phone or tablet without an Atlas at all.

## What the app stores and where

- **On your Atlas.** Your profile name, a PIN or password (stored hashed),
  game settings, accessibility preferences and game statistics live on the
  Atlas, which sits at your table. The app reaches it only over the Atlas's own
  Wi-Fi network. Nothing on the Atlas is sent to us or to anyone else. Whoever
  administers that Atlas can remove profiles or factory-reset it, which erases
  this data.
- **On your phone, without an Atlas.** Players you add on the device (name,
  color, avatar or a picture you choose, an optional PIN or password stored
  hashed), game settings, the game in progress, game history and statistics
  are saved in the app's private storage. They leave the phone only in two
  cases you control: Android's own backup, if you have it turned on, may copy
  them to your Google account (saved Atlas passwords and sign-in secrets are
  excluded); and you can choose to copy a player and their results to an
  Atlas.
- **On your phone, with an Atlas.** The app remembers the Atlas Wi-Fi password and, if you
  turn on automatic sign-in, your profile's PIN or password. That secret is
  encrypted with a key Android releases only after your fingerprint, face or
  screen lock.
- Uninstalling the app or clearing its storage removes everything it saved on
  the phone.

## Permissions

- **Camera:** scans the QR codes TurnHub shows during setup. Images are
  processed on the phone and never stored or sent.
- **Player pictures:** chosen with Android's file picker, so the app sees only
  the picture you pick. It is cropped, compressed and stored with that player.
- **Nearby devices / local network and Wi-Fi state:** finds and joins your
  Atlas's Wi-Fi network.
- **Biometrics:** unlocks your saved sign-in, if you turn that on.
- **Notifications and foreground service:** shows table status and keeps the
  connection to the Atlas while a game is running.
- **Internet:** used to download TurnHub firmware updates and, if you name a
  commander, to fetch that card's art (see below).

## Firmware updates

To check for and download firmware updates, the app requests public files from
TurnHub's GitHub releases. GitHub receives the request (including your IP
address) under [GitHub's privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).
No account or personal data is included.

## Commander card art

When a player names their commander in a Commander game, the app asks
[Scryfall](https://scryfall.com) for that card's art and shows it on the
player's tile. Only the commander name typed in is sent; Scryfall receives the
request (including your IP address) under
[Scryfall's privacy policy](https://scryfall.com/docs/privacy). The art is
kept in the app's cache so each name is looked up once. Naming a commander is
optional, and **Commander card art** in Settings turns the lookups off. While
you type a commander name, what you have typed so far is sent to Scryfall to
suggest card names, and choosing a card lists its printings so you can pick the
art.

## Children

TurnHub is not directed at children under 13 and does not knowingly collect
personal information from anyone.

## Changes and contact

Changes to this policy are published here, with a new effective date. Questions
go to the developer contact email on TurnHub's Google Play listing, or open an
issue at <https://github.com/rickethyo/TurnHub/issues>.
