# TurnHub Privacy Policy

Effective 16 October 2026. This policy covers the TurnHub Android app
(`com.turnhub.android`) and the TurnHub table hardware it controls.

## The short version

The app has no ads, analytics, tracking or automatic crash reporting, and
TurnHub runs no app servers. Game and player data stays on your phone or on
your own TurnHub table controller (the "Atlas"), except for sharing you choose.
You can play on one phone or tablet without an Atlas at all. Optional bug
reports are described below.

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

## Planechase planes

When a player turns on Planechase with the digital deck, the app downloads the
list of planes and phenomena (names, rules text, release membership and art) from
[Scryfall](https://scryfall.com) once, and each face-up plane's art when it
is first shown or previewed in the deck browser. Custom deck names and card
selections are saved only on the phone and are not sent to Scryfall. Nothing
about the players is sent; Scryfall receives the
requests (including your IP address) under
[Scryfall's privacy policy](https://scryfall.com/docs/privacy). The cards are
kept on the phone. Playing with your own cards downloads nothing.

## Optional bug reports

Choosing **Report a bug** opens a Google Form in your browser. Its editable
defaults include the installed app version/build, phone/tablet type,
manufacturer/model, Android version, display size and the current screen/game
context when known. An Atlas report can include its last known firmware version.
The app does not include player names, profile identifiers, passwords, session
tokens or device serial numbers.

Google receives the prefilled details when the browser opens the link. You can
review and change the answers before explicitly submitting them. The form
collects your signed-in Google-account email address, your answers and any file
you choose to upload; these are available to the TurnHub developer for reviewing
the report. The app sends no report automatically. Google handles the form under
[Google's privacy policy](https://policies.google.com/privacy).

## Children

TurnHub is not directed at children under 13 and does not knowingly collect
personal information from anyone.

## Changes and contact

Changes to this policy are published here, with a new effective date. Questions
go to the developer contact email on TurnHub's Google Play listing, or open an
issue at <https://github.com/rickethyo/TurnHub/issues>.
