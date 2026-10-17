<!-- Generated from TurnHub App Manual V0.21.docx and TurnHub Hardware Manual V0.21.docx by Android/tools/export_manual.py. Do not edit by hand. -->

# TurnHub App Manual

## Prototype Edition v0.21

TurnHub keeps track of the bookkeeping at a tabletop game: whose turn it is, life totals, Commander damage, counters, the turn timer and who won. The TurnHub app runs a whole game on one Android phone or tablet, with saved players, personalization and detailed history. No Atlas, account, internet connection or permission is needed to play.

An Atlas (the TurnHub table controller) adds what one device cannot: a phone for every player, physical Sigil controllers, the Atlas screen and speaker, and shared accounts and statistics for the table. This manual covers the app and the Atlas administration portal. Atlas and the Sigils have their own manual, the **TurnHub Hardware Manual**, which the app carries after this one.

**Prototype Notice**
TurnHub is a prototype. Screens, controls and names may change before the final version.

# 1. Getting the App

The TurnHub Android app is on Google Play through a closed testing track. Ask the TurnHub owner to add you as a tester, then install it from the Play Store link in your invitation. Updates arrive through Google Play like any other app.

The app needs no account and asks for no permission to play on one device. It asks for **Nearby devices** (local network access) only when you choose to connect to an Atlas, for notifications to tell you when it is your turn at an Atlas table, and for the camera only when you scan a QR code.

# 2. Finding Your Way Around

The app has the same tabs with or without an Atlas:

- **Game:** set up the next game: players, game mode, starting life, turn timer and options.

- **Table:** the game's table inside the app. Before a game starts it is a practice table built from the lobby's seats; you can play on it freely and nothing on it is saved. Once a game starts it shows that game.

- **Planes:** appears when the game is a Magic format, for Planechase (section 6).

- **Players:** the saved players on this device, and when an Atlas is connected, the players at its table.

- **History:** finished games and statistics.

- **Settings:** appearance, sound, Commander card art, Atlas connection, and administration when you have permission on a connected Atlas.

Tabs slide sideways as you move between them, unless reduced motion is on. **Full screen** gives a started game the whole screen; this is the way to play with one phone or tablet in the middle of the table.

At the top of the app are two small buttons: the bug button reports a bug (section 15) and the **?** button opens this manual. Long-press either to see its name. The app version and build number are shown in the lobby, at the bottom of Settings and in the table menu; mention them when you report a problem.

# 3. Players and Accounts

Every player at the table is an account saved on this device. Type a new name in the lobby, or choose **Add player** under Players, to create one; choose a saved player to reuse them. Each account has an eight-character account ID (such as 1A2B3C4D), so two players with the same name stay separate.

On a phone every player is equal: local accounts carry no permissions, and anyone at the table can set up, play and finish a game.

## PIN or Password

A PIN (4 to 8 digits) or password (8 to 64 characters) is optional. It protects that player's settings, not their play: anyone can still seat the player and play their turns. After the app restarts, a protected account's settings stay locked until the PIN or password is entered. Setting or changing it keeps the settings open until then. **Remove PIN or password** takes it off again.

## Personalization

Open a player (**Edit player** under Players) to change:

- **Name** and **color:** named swatches or a custom color picker. Color changes save immediately.

- **Avatar:** a preset icon or your own cropped image.

- **Tile color & photo:** the background of the player's panel at the table: Default, Solid, Gradient or a cropped image. The background follows the player across seats and games and is separate from the avatar.

- **Reduced motion:** a table uses reduced motion when any seated player has it on.

Images are cropped and compressed on this device to at most 512 by 512 pixels and 48 KiB, and stay on this device.

## Deleting an Account

**Delete account** removes a player from this device. It is not available while the player is in a game in progress. Their finished games stay in History under the name they played with.

# 4. Setting Up a Game

Open **Game**. The table preview shows the seats as players join, even before there are two.

1. Add everyone who is playing: type a name, or pick a saved player.

2. Arrange the seats: drag a player onto another seat, or tap two seats to swap them. Seats run clockwise in number order. **Clear all** empties the lobby without deleting any accounts, history or settings.

3. Optionally tap **First?** next to a player to choose who goes first; tap it again to clear it. Otherwise a random player starts.

4. Choose the game mode, starting life and turn timer.

5. Tap **Start full-screen game**. A three-second countdown runs before the clocks start; Cancel or Back stops it.

## Game Modes

- **Generic:** 40 life, any number of players.

- **Magic:** 20 life, poison limit 10.

- **Commander:** 40 life, with Commander damage and commander tax.

- **Two-Headed Giant:** teams of two (Magic or Commander).

- **Star Commander:** exactly five players.

- **Yu-Gi-Oh!:** 8000 life, two players.

Official modes (Magic, Commander, Two-Headed Giant and Yu-Gi-Oh!) keep their official starting life, poison limit and player count. Turn on **House rules** in setup to change them; any whole number from 0 to 1,000,000 is allowed. Two-Headed Giant Commander, Star Commander and Generic are custom modes. **? Rules**, in setup and in the table menu, summarizes the mode and links to its official rules.

## Death May Die

- Cthulhu: Death May Die is a cooperative, phone-hosted game. Pick each player’s investigator in setup. Everyone wins or loses together, and wounds and stress are reminders; the physical game determines their effects.

- The active investigator uses 70% of the play area. Their card scrolls independently, with the other players in a smaller list below. Wide screens put abilities beside the counters. Open another player’s menu to edit their counters and skills. Tap Pass to open Pass to? and choose an investigator who still needs a turn. After the last turn, choose any surviving investigator to start the next round, including the current player. Cancel or Back keeps the current turn. A player still to go can also choose Take the next turn from their panel.

- Each investigator has three skill markers, with levels 1 to 4. Tap the level to move a marker; earlier effects still apply unless a later effect says instead. Preview upgrades shows future level effects without moving a marker. The ten core-box investigators include ability summaries. Edit abilities lets you enter or correct the names and all four level effects for any investigator. Apply effects to the physical game yourself.

- The Insanity counter records sanity loss. It stops at each insanity threshold and shows bonus dice. Resolve your insanity card and choose a skill upgrade on the physical dashboard when prompted. The minus button corrects an entry; it does not mean sanity can normally be healed. Reaching the end of the track does not automatically end a player’s game: use their menu to resolve the outcome.

- Before starting, assign a Sanity condition to every investigator using Assign sanity condition beneath their name. Relics are optional: turn on Use relics in setup when playing with them, then assign a relic to every investigator. Start stays unavailable until the required cards are assigned. Turning Use relics off clears relic assignments and hides their slots. Enter a name and optional rules/notes; Save applies the assignment, Cancel keeps the current card, and Remove clears that slot. During play, Edit on the card or in the player menu lets you correct a reference. Apply card effects to the physical game.

- Skill markers, sanity loss, edited ability text and assigned cards survive closing the app. Back to setup keeps assignments. Set up rematch returns to setup with empty card slots and reset markers, while keeping investigators, edited ability text and the Use relics option. Assign fresh cards before starting the next game. Choosing a different investigator clears their dashboard.

## Two-Headed Giant

Players 1 and 2 are a team, then 3 and 4, and so on; change the seats to change the teams. It needs an even number of players, at least 4 (official Two-Headed Giant is two teams of two).

- Teammates share one life total (30 for Magic, 60 for Commander) and one poison count. Either teammate can change it.

- Teammates take their turn together: either can pass, cancel the pass or claim the win.

- Commander damage still counts per player, and each hit comes off the team's life. Teammates can't deal each other Commander damage.

- A team leaves together: the More card has one **Our team concedes** button. The last team left wins, and both teammates get the win.

- The starting team skips its first draw; the table reminds you during that first turn.

## Star Commander

Each of the five players has two allies, the players seated on either side, and two enemies, the two players across the star. The star in setup shows who is allied; change the seats to change it. Alliances are fixed once the game starts.

- Everyone keeps their own life (40), turns, counters and statistics. Allies are not a team.

- Don't attack or target your allies. TurnHub can't check the cards, so the table keeps this rule.

- Allies can't deal each other Commander damage. Ordinary life changes always work.

- You win when both of your enemies are out, however they went out. If one event takes out the enemies of two players, both win.

- Each panel shows Enemies out 1/2. **Star layout** (in the lobby, or Sound & display in the table menu) seats the game in the star itself, each panel facing its player; it suits a tablet.

- When the game ends, **Sean Mode** offers a rematch that reseats the same five players so your allies become your enemies.

## Naming Your Commander

In any Commander mode, **Name commander** next to a player lets them name their commander. The name shows under theirs, names their commander-tax counter and labels the Commander damage others take from it. As you type, matching card names are suggested from Scryfall; pick one, and choose a printing whose art fills the player's tile, credited to its artist. The original commander-card icon takes the printing's rarity color. Only what you type is sent to Scryfall. **Commander card art** in Settings turns the suggestions and art off.

## Turn Timer

- **Off:** no countdown. After five minutes the panel shows a gentle Long turn reminder.

- **Presets:** 1, 2, 3, 5 or 10 minutes, or a custom length from 15 seconds to 60 minutes.

With a timer set, each turn counts down from the full time and warns at 10 seconds. Pausing stops the clock, and passing starts a fresh one. By default an expired turn simply runs over; **Timer rules** can instead pass automatically, add a demerit, or do both.

Earned extensions are optional. From a player's fourth turn, passing early (before 75% of the limit by default) earns one, up to two per game. A player can spend one extension per turn, before time runs out, to add half the turn limit; an extended turn earns nothing.

# 5. Playing at the Table

Each player has a panel facing their seat with their name, life, counters and turn clock. The player whose turn it is has a glowing edge.

- **Life:** tap the top half of a panel to add life and the bottom half to subtract; hold to repeat the bigger step. Steps are 1 and 5 (100 and 1000 for Yu-Gi-Oh!). On rotated panels, minus is always on the player's left and plus on their right.

- **Pass:** the active player taps **Pass**. While it says **Passing… tap to keep**, a tap undoes the pass.

Death May Die uses the Pass to? menu described under Game Modes.

- **Planar die:** in Planechase, beside Pass (section 6).

## The Game Menu

The control in the middle of the table opens the game menu. **Pause game** (or **Resume game**) stays at the top. It also holds:

- **Table tools:** dice from d4 to d20 (more under **More dice**), a coin and a random player.

- **Game rules:** **? Rules** and automatic elimination (below).

- **Seating & alliances:** the star, in Star Commander. Tap a player to highlight their allies and enemies.

- **Sound & display:** game sounds, volume, **Turn panels around** and, in Star Commander, Star layout.

- **Help & app:** report a bug, or **Back to the app** to leave the full-screen table without ending the game.

- **Finish game:** **End without a winner** (a draw) and, during the first three minutes, **Back to setup**, which returns to Game with the same players and settings and saves nothing.

## A Player's More Card

**More** on a panel holds everything else for that player. It stays available while the game is paused.

- **Turn, counters & status:** poison, energy and experience (minus corrects or spends a counter); I'm the monarch; I have the initiative; I have the city's blessing.

- **Commander tax:** count each commander's casts from the command zone. The next cast costs two more generic mana per earlier cast; minus corrects a mistake.

- **Commander damage:** record damage this player received from each opponent's commander. Each hit also comes off their life, and a correction gives it back. Turn on **Partner commanders** for a second commander's casts and damage.

- **Pick an opponent:** chooses a random opponent, never a teammate, an ally or a player who is out.

- **Claim the win**, **Concede** and **Eliminate:** tap again to confirm.

Only one player can be monarch, and only one can have the initiative; claiming either moves it. Any number of players can have the city's blessing. When a holder leaves the game, the monarch and the initiative pass to the player whose turn it is, or the next player in turn order. Players resolve the cards' own effects themselves.

## Automatic Elimination

Separate switches eliminate a player at zero life, at the poison limit (10, or 15 shared in Two-Headed Giant) and at 21 damage from one commander. All are on by default. Change them, and the poison limit, from the game menu at any time, including while paused. Turning a rule on checks the existing totals at once; turning it off does not bring anyone back. Switch the relevant rule off before a card changes how a player loses.

Concede records a voluntary exit and Eliminate records a loss; both take the player out of turn order, and statistics count them separately.

# 6. Planechase

In a Magic or Commander game a **Planes** tab appears. Before the game, turn on **Play Planechase** there, or with the Planechase switch in Game setup, and choose **Digital deck** or **Physical cards**.

## The Digital Deck

The first time, TurnHub downloads the planes and phenomena from Scryfall: names, rules text, release collections and art, each credited to its artist. After that it works offline, except art previews not yet downloaded. **Refresh library** fetches new cards.

- Choose **All planes**, or a **Collection** such as Planechase Anthology.

- Or build your own: **Browse & build** lets you search, filter by collection or Selected only, and check the cards to include. **Add shown** and **Remove shown** change every filtered card at once. **Preview** opens a card's art and rules; Previous and Next move through the list.

- Name the deck and choose **Save & use deck**. Up to 20 decks are kept on this device; saving the same name replaces that deck. **Use** selects a saved deck and **Delete** removes it.

- Turn on **Include phenomena** to shuffle in up to two selected phenomena per player.

The game keeps its deck when you close and reopen the app, and a rematch reshuffles the same pool.

## During the Game

The Planes tab shows the face-up plane. The planar die button rolls for the player whose turn it is and shows what the next roll costs; the cost goes back to {0} when the turn passes. **Planeswalk** turns over the next plane. A phenomenon waits until you tap **Done: planeswalk away**. On chaos, the plane's chaos ability is highlighted.

On the table, **Planar die** beside Pass rolls with a short animation. A blank keeps you on the table; chaos or planeswalk opens Planes with the effect highlighted. **Counters on this plane** and the buttons under **When a card says so** cover cards that roll the die, planeswalk, cause chaos or move the top card.

With physical cards, TurnHub rolls the die, counts the cost and tells you when to turn over the next plane. Rolls and deck moves wait while the game is paused.

# 7. Ending a Game

The game ends when one player or team is left, when a win claim is confirmed, or with **End without a winner** (a draw). If nobody survives automatic elimination, it is a draw.

The game-over card offers:

- **Rematch:** the same players again. The starting player is chosen at random.

- **New players:** back to setup.

- **Share result:** a picture of the final table with the result and a game overview, sent to any app on the share sheet.

- **Count this game:** a game that ends within a minute isn't added to history or statistics unless you tap this.

# 8. History and Statistics

**History** lists every finished game with its winner, mode, starting life, and each player's final life, turns and how they went out. Tap a game for its details.

**Statistics** lists every saved player, most wins first. Anyone at the table can open it; no PIN is needed. Tap a player for games played and won, draws, starts, eliminations, concessions, turns, total and average game and turn times, fastest and longest turns and their last game. **Share** sends their statistics as text.

The current game, players, personalization and history survive closing the app, and history is never pruned. Clearing the app's data or uninstalling it removes them all; there is no backup or export yet.

# 9. Sound, Appearance and Accessibility

- **Game sounds:** short sounds for a confirmed pass, game start, pause, resume, victory and draw. **Game sounds** and **Sound volume** are in setup, the table menu and Settings, and default to on at 50%. The phone's silent mode and media volume still apply. Sounds never replay when you return to the app.

- **Appearance:** Settings > Appearance changes the app's theme at once.

- **Reduced motion:** in Settings, or on a player's account, turns off sliding tabs and other animation.

- The app follows Android's text size, contrast and TalkBack screen-reader settings. Every turn, timer and decision is shown in words, so nothing depends on sound, vibration or color alone.

- The screen stays on during play.

# 10. Playing with an Atlas

An Atlas lets every player use their own phone, adds Sigils and the Atlas screen, and keeps shared accounts and statistics for its table. The Atlas runs that game and decides every result; the app shows its state and sends your requests. A game on this device and a game on Atlas stay separate: nothing in one changes the other.

## Connecting

Open **Settings > Atlas connection** and choose **Connect**. The app joins the Atlas's Wi-Fi network (TurnHub-Atlas) by itself: it tries a saved password, then the default TurnHub-Setup, and asks only if neither works. Allow **Nearby devices** when Android asks; without it Android blocks the connection. A new Atlas shows **Welcome to TurnHub**; tap **Set up a new table** in the app and follow the steps (the Hardware Manual describes them).

Atlas discovery stays on in the background and reconnects to a known Atlas when the phone is on its network. Turning it off disconnects. With an Atlas connected, **Game** lets you choose **This device** or **Atlas**, and Players adds Table and Me sections.

## Your Atlas Account

Sign in with your Atlas account's PIN or password, or choose **New here? Create an account**. An account made without a PIN, for example on a shared tablet, asks for one the first time it signs in on a phone.

Signed in, you can join the table, choose to go first, pass, pause or resume, change life and Commander damage, claim or answer a win, and concede. Atlas keeps the turn clock.

- **Life changes for another player** need their approval. If they don't answer, the change is accepted after 15 seconds (30 or 60 if they chose longer).

- **Win claims** are confirmed by the other players one at a time; one denial cancels the claim.

- **Nudge:** any player whose turn it is not can nudge the active player, once every 30 seconds. Their phone shows who nudged them and vibrates, and their Sigil plays a sound.

- **Sigil accessibility:** your Sigil sound, light style, hold times and the time you have to answer life changes. They are saved with your Atlas account and follow you to any Sigil.

- **Personalization:** upload a player image to Atlas (it needs a microSD card in Atlas, and an Admin approves it unless approval is turned off).

If Atlas stops answering, the app keeps the last known table under an **Offline: reconnecting to Atlas** banner and carries on when Atlas answers.

## Shared Tablet Mode

One tablet or phone in the middle of the table can act for everyone at an Atlas game. Sign in, open **Game** and tap **Open tablet mode**. The device switches to the Shared tablet account; your own phone can stay signed in. If the table code is on, tap **Show a code on Atlas** and type the six digits from the Atlas screen; an account with the Tablet access role skips this.

Seat players with **Add**: type a name to create an Atlas account with no PIN, or pick a saved player, who types their PIN unless they allowed Sigil and tablet use without one. Seating, First?, Clear all and the panels work as in section 4 and 5. Players seated by the tablet can also use their own Sigil or phone.

If Atlas stops answering, the panels keep taking life and Commander damage and send them when Atlas answers. Passing, pausing, conceding and win claims wait for Atlas. **Leave tablet mode** signs the shared account out.

## Two Games on One Atlas

An Atlas can run Game 1 and Game 2 at once. The **Game on this Atlas** selector chooses which one you join. A player is in one game at a time, and you can't switch during a game you are playing in.

## Bringing Local Players and Games to Atlas

A player on this device can be linked to an Atlas account. Connect, open **Players**, load the Atlas accounts and open the local player. **Create this account on Atlas** makes a matching Atlas account with the same account ID and personalization (it needs a PIN or password). To merge with an existing Atlas account instead, pick it and enter its PIN or password. Names alone never link or merge accounts.

Once every player in a finished local game is linked to an account on that Atlas, the game is added to their Atlas statistics automatically. Other games can be matched to Atlas accounts by hand in History. A local game is never deleted by importing it.

## Roles on an Atlas

Accounts on an Atlas can be given roles by an Admin: **Admin** (administration), **Game Master** (fixes unusual game states at the table), **Developer** (diagnostics) and **Tablet access** (tablet mode without a code). Roles need a PIN or password. A Game Master's pass is recorded as a master pass, not as the player's own. When a Game Master resets a player's connections or removes them from a game, only that player can see it, on their own statistics.

Some features need a newer Atlas than the current prototype: Star Commander, official rules and House rules, and creating an account on Atlas from a local player. The Hardware Manual lists them.

# 11. Administering an Atlas

An Admin manages an Atlas from **Settings** in the app or from the administration portal in a browser. Play, tablet mode, personal settings and statistics are in the app; the portal is for setup, maintenance and recovery.

To open the portal, join the Atlas's Wi-Fi and go to http://192.168.4.1/portal, then sign in as an Admin. It works without a microSD card.

- **Manage people:** roles, archive and restore accounts, image approval.

- **Wi-Fi:** the table's own password, 8 to 63 characters. Saving restarts Atlas.

- **Table code:** when on, actions that change the table need a code from the Atlas screen.

- **Sigils and devices:** pairing decisions, the pairing window, names, Forget, Forget all Sigils and factory reset.

- **Speaker volume**, **Return table to lobby** and **Factory reset Atlas**.

- **Developer:** diagnostics and hardware tests, for accounts with the Developer role.

The portal offers System, Dark, Light and High contrast appearance, saved in that browser.

## Verify at the Table

When the table code is on, changing the Wi-Fi password, naming devices, Return table to lobby and factory reset need proof that you are at the table. The app or browser asks for a code; the Atlas screen shows a six-digit code and a QR code for 90 seconds, which only works for the account that asked. Type it or scan it with the same phone. You stay verified for 10 minutes; **Stop** ends it early. Five wrong codes cancel it. If a code appears that nobody asked for, tap **Cancel** on the Atlas screen.

# 12. Privacy

Everything you do in the app stays on this device: players, PINs (stored only as secure verifiers), images, games and statistics. There are no ads, analytics or tracking. The app talks to:

- **An Atlas**, over the Atlas's own Wi-Fi, only when you connect.

- **Scryfall**, only for commander names and art you look up and for the Planechase library. Only what you type is sent; turn Commander card art off in Settings to stop it.

- **Google Forms**, only when you choose to send a bug report.

The privacy policy is PRIVACY.md in the TurnHub repository.

# 13. Troubleshooting

## The Game Disappeared After Closing the App

It is still there: open Table to carry on. Only clearing the app's data or uninstalling removes it.

## A Player's Settings Ask for a PIN

The player set a PIN or password. Enter it to unlock their settings until the app restarts. Play never needs it.

## I Can't Delete a Player

A player can't be deleted while they are in a game in progress. Finish or end the game first.

## A Game Is Missing From History or Statistics

A game that ended within a minute isn't saved unless someone tapped **Count this game**. Back to setup also saves nothing.

## Planechase Art Doesn't Load

Art not yet downloaded needs internet. The rest of the deck works offline after the first download.

## The App Can't Connect to Atlas

Check that Atlas is on and the phone is nearby, and that the app has the Nearby devices permission (Android Settings > Apps > TurnHub > Permissions). Choose Connect in Settings > Atlas connection again.

## The App Says Offline: Reconnecting to Atlas

Atlas has stopped answering. Check that it is on and in range; the app rejoins by itself.

## A Local Game Is Missing From Atlas Statistics

Connect and sign in, then open History. Games whose players are all linked to accounts on that Atlas import by themselves; match the others by hand.

## Tablet Mode Asks for a Code Every Time

The table code is on. An Admin can turn it off, or give the account the Tablet access role.

# 14. Prototype Limitations

The app is still in active development. Expect changing names and screens, and features that move between versions. History has no backup yet. There is no iPhone app yet. Playing on several phones at once needs an Atlas.

# 15. Reporting a Bug

Tap the bug button at the top of the app. It explains what it does and opens a short report form with the app version and your device filled in; check them, describe what happened and submit. Nothing is sent until you submit it.

Helpful details: what you were doing, what you expected, what happened, and whether it happens again.

# TurnHub Hardware Manual

An Atlas is the TurnHub table controller. It runs the game for the whole table: whose turn it is, life totals, the turn timer, win claims and statistics. Players take part with Sigils, small controllers with a thumbstick and a ring of lights, or with the TurnHub app on their own phones. Sigils, phones, the Atlas touchscreen and a shared tablet all send requests to Atlas, which decides the result and shares the new state with everyone.

This manual covers the current prototype hardware: the Atlas on a 2.8-inch touchscreen board with a small backup battery, and the e-ink and OLED Sigils, running Atlas firmware 0.7.14. The app, shared tablet mode and the administration portal are described in the **TurnHub App Manual**.

**Prototype Notice**
These boards are prototypes and are frozen: they receive fixes over USB only. New hardware is in development (see section 18). Controls, menus and names may change before the final version.

# 1. Meet the Hardware

## Atlas

Atlas is the central controller for the table. It has a 2.8-inch touchscreen, a small **BOOT** button, a status light and a speaker. It:

- Runs the game and keeps its state

- Hosts its own Wi-Fi network for phones, and the administration portal

- Talks to the Sigils by radio, encrypted once they are paired

- Keeps accounts, settings and statistics

- Shows the table on its touchscreen

## The Atlas Screen

The top line shows what the table is doing (Lobby, Starting, Playing, Paused or Game Over) and how many Sigils are online; during and after a game it also shows the round and how long the match has run. Below it, Atlas shows whose turn it is, a turn clock, and a card for each player with their name, life total, their total time on their own turns and a label: TURN, OUT, WINNER, STARTS, or CONFIRM for the player a win claim is waiting on.

The buttons along the bottom change with the game:

- **Lobby:** Start (once two players have joined), Clear (hold, to empty the lobby) and Menu.

- **Countdown:** Cancel start.

- **Game:** Pause (or Resume) and Table. **Table** holds the less-used controls, **Master pass** and **End match**.

- **Game over:** Rematch, Reset and Menu.

Menu holds Pair a Sigil (lobby only), QR codes, Device, Tests (only while a developer test tool is connected) and Info; Back steps out one screen at a time. A pressed button changes color and gains a heavier border, and buttons that must be held say so and count down while you hold them.

A red **NO SD CARD** label appears when no microSD card is in use. The game works without one (section 14).

## Sigils

Sigils are the player controllers placed around the table. There are two prototype models:

- **E-ink Sigil:** a paper-like screen, a thumbstick that also clicks when pressed in, and a ring of lights.

- **OLED Sigil:** a small bright screen, the same clicking thumbstick and the same ring of lights.

Each also has a buzzer and a small **BOOT** (Pair) button, which a case may hide. A Sigil shows who is playing on it, whether it is their turn, their life total, the game status, prompts, and what each of its controls does right now. One Sigil can be shared by two players.

## What You Need

- One Atlas, with power. A microSD card is optional, for detailed statistics and player images.

- A Sigil, or the TurnHub app on a phone, for each player; or one shared tablet in tablet mode.

- Power or enough battery charge for each Sigil.

Play can begin without accounts, an administrator or the portal. Players without an account are shown as Player 1, Player 2 and so on, and their statistics are not saved. Atlas needs no internet connection.

# 2. Starting Up

## Power On Atlas

Connect power to Atlas. It shows the TurnHub logo for a moment, then the lobby. Wait for the lobby before pairing anything.

The first time Atlas starts, and after a factory reset, it asks you to calibrate the touchscreen: press and release the center of each cross as it appears, four in all. To calibrate again later, press and hold anywhere on the lobby screen for 10 seconds.

## Setting Up a New Table

A new Atlas, or one that was factory reset, shows **Welcome to TurnHub** instead of the lobby. Setup takes a few minutes and is easiest with the app: open it and tap **Set up a new table**; it joins Atlas's Wi-Fi by itself. Without the app, join the Wi-Fi network TurnHub-Atlas (password TurnHub-Setup) and open 192.168.4.1 in a browser.

1. **Your account:** create one or sign in.

2. **At the table:** type the code the Atlas screen shows. This account becomes the table's Admin.

3. **Sigils:** pair each one (section 4), or skip.

4. **Updates:** choose Later or Continue; the prototype boards are updated over USB only (section 16).

5. **Secure Wi-Fi:** choose the table's own Wi-Fi password, 8 to 63 characters, and whether uploaded player images need Admin approval (on by default). Finish saves it and restarts Atlas, and the app rejoins with the new password.

The browser covers the account, the code and the Wi-Fi password; pairing is on the Atlas screen and in the app. To play first, tap **Skip for now** on the Atlas screen: setup comes back at the next start-up and under Menu. When setup is done, Atlas says You're all set; tap **Pair a Sigil** or **Done**.

## Power On Your Sigils

Turn on each Sigil. It shows the TurnHub logo, then either its Sigil number (it is paired and ready) or Unpaired.

# 3. Joining Atlas from a Phone

Atlas creates its own Wi-Fi network, **TurnHub-Atlas**. Its password is **TurnHub-Setup** until an administrator changes it. The TurnHub app joins it by itself (App Manual, section 10).

## QR Codes on the Atlas Screen

Tap **Menu**, then QR codes, in the lobby or after a game, and choose a code:

- **Wi-Fi:** joins the TurnHub-Atlas network.

- **Portal:** opens the administration portal.

- **Sign in:** opens the account sign-in page.

If an administrator has set their own Wi-Fi password, the Wi-Fi code only appears while an administrator is verified at the table, so the password is not shown to everyone.

## Info

**Info**, under Menu, shows the Wi-Fi name, the portal address, the firmware version, how many Sigils are online, the microSD card status and how long Atlas has been running.

# 4. Pairing Sigils

Pairing starts from the Atlas touchscreen. A paired Sigil stays paired through restarts, and everything it and Atlas say to each other is encrypted.

## To Pair a Sigil

1. In the lobby, tap **Menu**, then Pair a Sigil. Atlas opens a pairing window of 60 seconds and counts it down; its status light blinks red. An administrator can lengthen the window to 90 or 120 seconds.

2. Within that time, put the Sigil into pairing mode: hold its joystick in for 3 seconds (the light ring fills as you hold), or press its BOOT button. Its light ring blinks while it looks for Atlas. The Sigil's own window is always 60 seconds, so with a longer Atlas window start pairing on Atlas first.

3. The Sigil and the Atlas screen each show the same 4-digit code. If they match, tap **Codes match** on Atlas. If they differ, tap **Reject**: something else answered the Sigil. The Sigil then shows its number.

4. If nobody answers within a minute, nothing is saved; pair again. Repeat for each Sigil.

Do not hold the BOOT button while plugging a Sigil in or restarting it: that puts the board into its programming mode instead of starting TurnHub.

## The BOOT Buttons

On a Sigil, a quick press of BOOT pairs, holding it for 3 seconds makes it forget Atlas, and holding it for 10 seconds factory resets it. On Atlas, BOOT is a backup for the touchscreen: a quick press opens pairing, 3 seconds forgets every Sigil, and 10 seconds erases Atlas. A tone sounds at 3 and at 10 seconds, so let go at the tone to stop there.

## Forgetting a Sigil

- **On the Sigil:** open its Device menu (section 5) and hold **Unpair** for 3 seconds, or hold its BOOT button for 3 seconds. The Sigil forgets Atlas, its lights go off and it shows Unpaired.

- **From the app or portal:** an administrator chooses Forget next to a Sigil, or Forget all Sigils. This works in the lobby, for Sigils nobody is seated on.

A forgotten Sigil can be paired again at any time. A Sigil paired with a different Atlas must be forgotten before it pairs with this one.

# 5. Device Menus, Sleep and Factory Reset

## On Atlas

Tap **Menu**, then **Device**, between games:

- **Unpair Sigils:** hold for 3 seconds, in the lobby with nobody seated on a Sigil, to forget every Sigil.

- **Factory reset:** hold for 10 seconds to erase Atlas.

- **Sleep:** Atlas says Going to sleep. Touch the screen to wake, and its screen goes dark. Touch the screen (or press BOOT) to wake it. Atlas restarts: accounts, pairings and settings are kept, but the lobby empties and phones sign in again.

- **Theme:** see section 6.

Like the BOOT button, these need no administrator. An administrator can also factory reset Atlas or a Sigil from the app or portal; that needs verification at the table when the table code is on, and neither works during a game.

A factory reset of Atlas asks you to type RESET in the app or portal. It erases every account, PIN, statistic, Sigil pairing and setting, puts the Wi-Fi password back to TurnHub-Setup, empties the microSD card, and restarts as new with calibration and setup. This cannot be undone.

## On a Sigil

Every Sigil has its own **Device** menu that works without Atlas, even during a game. It is the last entry in Menu (push Up); when Atlas cannot be reached, Menu opens straight on it.

- **Unpair:** hold for 3 seconds.

- **Sleep:** the Sigil shows how to wake it and its lights go off. Click the joystick (or press BOOT) to wake it; it restarts and reconnects by itself.

- **Factory reset:** hold for 5 seconds. The Sigil erases everything it has saved, including its pairing, and restarts as new.

- **Theme** and **Invert:** see section 6.

- **Back:** push Left, or choose Back at the end of the OLED list. The menu also closes by itself after 10 seconds.

# 6. Screen Themes

Each Atlas and Sigil remembers its own screen theme: **Graphite** (the default), **Daylight**, **Brass** or **High contrast**, and a separate **Invert** setting that flips black and white on a Sigil and reverses the colors on Atlas. Changing one device does not change another, the app or the portal. Unpairing keeps the choice; a factory reset clears it.

- **On Atlas:** between games, tap Menu, then Device, then Theme. Tap a theme to preview and save it; tap Invert to toggle it.

- **On a Sigil:** push Up for Menu, choose Device, then Theme; each choice moves to the next theme. Choose Invert to toggle it. On the e-ink Sigil, choose More on the first Device page to reach them.

Each theme has its own emblem and icon style, and a default calm color for the light ring; a player's own ring color overrides it. The screens always show turn and warning states in words.

# 7. Players at the Table

Players can sign in to an account on Atlas, or play without one. An account has a name, usually a PIN or password, and the player's Sigil accessibility choices. Accounts are separate from Sigils: anyone can use any Sigil, and changing Sigils never moves a player's statistics.

## Joining

Choose **Join game** on a Sigil, or join from the app. Any player at the table can set up and start the game; there is no host.

## Two Players on One Sigil

After joining, push Left in the lobby (**Add seat B**) for a second player; Left again drops seat B. The screen shows both names, prompts say which seat (A or B) they are for, and Down switches between the seats. Both seats sit next to each other in turn order. In the lobby, tap either player on the Atlas screen to set the order: Earlier and Later can swap A and B, and moving past their pair moves the whole Sigil. The order is kept for a rematch.

The same Player screen on Atlas has **Remove**: hold it for 2 seconds to take that player out of the lobby. Removing seat A of a shared Sigil removes seat B too.

## Moving onto a Sigil During a Game

Nobody new joins a game once it has started, but a player using a phone or the tablet can move onto a Sigil. On a Sigil that is not in the game, choose **Join**: it lists only the players in the game who have no Sigil. Choose one, and that player carries on from the Sigil with their place, life and turn clock.

# 8. Starting a Game

1. Each player joins.

2. Optionally choose who goes first: **Next starter** or **Random start** on a Sigil, or I go first in the app.

3. Choose the game, starting life and turn timer in the app's Game tab, and save them. They apply from the next game that starts.

4. Choose **Start game** on a Sigil or in the app, or tap **Start** on the Atlas screen. A 3-second countdown begins, which any seated player, or **Cancel start** on Atlas, can cancel.

Games and their starting life on this Atlas: Generic 40, Magic 20, Commander 40 and Yu-Gi-Oh! 8000, or any whole number from 0 to 1,000,000.

**Two-Headed Giant** (Magic and Commander) makes teams of two: players 1 and 2, then 3 and 4, and so on. It needs an even number of players, at least 4. Teammates share one life total (30 for Magic, 60 for Commander, which you can change), take their turn together, and win or go out together. Commander damage still counts per player and comes off the team's life. The starting team skips its first draw; the Atlas screen reminds you.

# 9. Taking Turns

The active player's Sigil shows YOUR TURN, and the Atlas screen shows their name and turn clock.

To pass, choose **Pass turn** on the Sigil (the click, on your turn) or in the app. For 3 seconds you can change your mind with **Undo pass** (on the OLED Sigil, click again). Meanwhile your Sigil shows PASSING and its ring counts down in green; every other Sigil shows who is passing with an amber countdown, and the Atlas screen shows Passing in 3s. The table hears two falling ticks when a pass starts and two rising ticks if it is undone.

Passing a turn to yourself, when the game calls for it, plays a distinct alert so it is not mistaken for a normal pass.

## A Stuck Turn: Master Pass

If the active player has stepped away or their Sigil is not responding, anyone at the table can move the game on: tap **Table** on the Atlas screen, then hold **Master pass** for 2 seconds. The turn passes at once, with no undo, and is recorded as a master pass. It is not available while the game is paused or while a win claim or elimination is waiting.

## Turn Timer

Any seated player can choose the turn timer in the lobby, in the app: Off, 1, 2, 3 or 5 minutes, or a custom length from 15 seconds to 60 minutes. It applies from the next game.

- With the timer off, the active player's Sigil shows a gentle long-turn reminder after five minutes.

- With a timer, each turn counts down on the Atlas screen as a clock and a bar. Pausing stops it, and passing starts a fresh countdown.

- At 10 seconds left, the active Sigil chirps and its light pulses slowly; the Atlas clock turns red.

- When time runs out, the Sigil plays two low notes. The turn does not end by itself: the player passes as usual.

# 10. Sigil Controls

A Sigil offers a short menu of what you can do right now. Atlas decides the choices, so they change with the game.

## The Keys

- **Click** (press the stick straight in): the obvious next step, such as Join, Start game, **Pass turn**, Undo pass, Resume, Rematch, Confirm win or Confirm out.

- **Left and Right:** in a game, change your life (section 11). In the lobby, Left adds or drops seat B and Right picks the next starter.

- **Down:** switches seat on a shared Sigil.

- **Up:** opens **Menu**, with every other action Atlas offers right now (such as Cmd damage, Pause, Claim win, I'm out, Partner, Random start, Leave lobby, Switch game or Link phone), then Device.

The two Sigils show Menu differently:

- **E-ink Sigil:** pages of up to four choices, one each on the click, Up, Right and Down, listed at the bottom of the screen. While more choices follow, Down is **More**; Left goes back a page, then out.

- **OLED Sigil:** a scrolling list. Push Up or Down to move, click (or push Right) to choose, and Left to go back. Lines you must hold say (hold). The bottom line of the game screen names one key at a time.

## Actions You Hold

Actions that are hard to undo must be held: **Claim win**, **Confirm out**, **Reset table**, and the Device menu's **Unpair** and **Factory reset**. The light ring fills as you hold; let go early to cancel. Players can change their hold times in Sigil accessibility (section 12).

## The Light Ring

The ring shows the same thing as the screen: in the lobby, your player number as lit lights; in a game, your turn, waiting, paused, warnings and prompts. The words on the screen always carry the meaning, so nothing depends on color alone.

If a Sigil cannot hear Atlas for about 7 seconds, its screen says Atlas lost, Searching… (NO ATLAS on the OLED Sigil) and one orange light sweeps around the ring (two steady lights with reduced motion). Game actions are hidden until Atlas answers; the Device menu and BOOT button still work. When Atlas answers, the Sigil returns to its normal screen by itself.

# 11. Life and Commander Damage

The Atlas screen shows every player's life total.

- **On a Sigil:** Left and Right change your own life during a game; hold to repeat. The ring shows the change, and the Sigil sends it 2 seconds after your last press. In games that start at 1000 or more, a Sigil steps by 100.

- **On the Atlas screen:** tap a player's card for -5, -1, +1 and +5, or Concede.

- **In the app or tablet mode:** see the App Manual.

A change to another player's life must be approved by that player: Right approves and Left denies on a Sigil, or use the buttons in the app. If they don't answer, it is accepted after 15 seconds, or 30 or 60 if they chose a longer time. Pausing does not stop that time.

## Commander Damage from a Sigil

Commander damage is recorded by the player who received it, from each opponent's commander and partner. Each hit also comes off their life (or the team's), and a correction gives it back. Recording 21 damage does not eliminate anyone by itself on this Atlas; the table decides.

On the receiving player's Sigil (on a shared Sigil, show the right seat with Down first), push Up for Menu and choose **Cmd damage**. Use Left and Right to pick who hit you and click; with partners on, choose Commander 1 or 2; then enter the amount. Click to review the new totals and click again to apply. Up goes back; Down cancels. This works on anyone's turn without pausing. **Undo hit** reverses the last hit recorded this way for that seat. Choose **Partner** from Menu to turn on a second commander.

# 12. Pausing, Alerts and Accessibility

Any player in the game can pause from their Sigil menu, the app, or **Pause** on the Atlas screen; Resume works the same way. The game stays exactly where it was.

Alerts come from the Sigil's lights and buzzer, the Atlas speaker and the app. When a decision is waiting on you, such as confirming a win or approving a life change, your Sigil plays two short tones and shows the request. Players can nudge the active player from the app; their Sigil plays the nudge sound.

## Sigil Accessibility

Each player chooses how their Sigil behaves in the app (sign in, then Sigil accessibility). The choices are saved with their Atlas account and follow them to any Sigil.

- **Sigil sound:** turn the tones off. Everything a tone means still appears as text in the app.

- **Sigil lights:** Standard; Reduced motion (steady lights and slow blinks only); or Monochrome-safe (no two signals differ by color alone).

- **Hold times:** for deliberate actions (1 to 4 seconds, normally 2) and to claim a win (3 to 10 seconds, normally 5).

- **Time to answer life changes:** 15, 30 or 60 seconds.

On a shared Sigil, the more accommodating choice wins: quiet if either player turned sound off, reduced motion if either chose it, and the longer hold times.

# 13. Winning, Conceding and Draws

A player claims a win by holding **Claim win** on their Sigil, or from the app. The game does not end yet: the other players still in the game are asked one at a time to **Confirm win** or **Deny win**, and the Atlas screen marks the player it is waiting on with CONFIRM. One denial cancels the claim.

To concede, choose **I'm out** from Menu, then hold **Confirm out** (on a shared Sigil, choose the other seat first if needed). Concede and elimination both take the player out of turn order; the last player or team left wins. Automatic elimination at zero life is on by default and can be changed from the app.

If a match can't be finished, for example after a power cut, tap **Table** on the Atlas screen and hold **End match** for 5 seconds. It ends with no winner, and every player's statistics count a draw.

After the game, choose **Rematch** to play again with the same players, or **Reset** to empty the table, on the Atlas screen, a Sigil or the app.

# 14. Two Games, Statistics and the microSD Card

## Two Games on One Atlas

One Atlas can run **Game 1** and **Game 2** at once, each with its own lobby, players and settings. Everyone starts in Game 1. On a Sigil not in a game, choose **Switch game** from Menu; a shared Sigil takes both seats with it. Phones choose a game in the app. The Atlas screen and pairing show Game 1 for now. While both games are in progress the Atlas speaker stays quiet; firmware updates, factory reset and Sleep wait until neither game is running.

## Statistics

Atlas keeps statistics for each account. Without a microSD card it keeps games played and won, the last result and the last game type. With a card it also keeps detailed turn statistics, last-game details and uploaded player images. Players see their statistics in the app.

You can insert or remove the card while Atlas is running; it notices within a few seconds. Avoid removing it just as a game ends, while Atlas saves statistics. Play never depends on the card, and Atlas never formats one.

# 15. Power

## Restarting

To restart Atlas, disconnect its power and connect it again. Atlas also restarts by itself after a Wi-Fi password change or a factory reset. A game in progress comes back **paused**: resume it, or end it with End match. Avoid cutting power while Atlas is saving settings.

## Shutting Down

There is no shut-down command. End or pause the game, disconnect Atlas's power, then switch off the Sigils. An unfinished game is restored, paused, the next time Atlas starts.

## The Backup Battery

Atlas's small battery keeps the game, radio and portal alive when USB is pulled; keep USB connected for normal play.

- On battery the screen turns off. A touch lights it for 15 seconds, and that first touch does not press anything. USB lights it again.

- Between games, five minutes without player activity on battery puts Atlas to sleep.

- The header shows an estimated charge, and a lightning bolt on USB. Both are estimates, not measurements, and may be unavailable for about five seconds after start-up.

- **LOW BATTERY** appears when the voltage is low. Very low voltage puts Atlas to sleep even if the percentage still looks high; plug in USB to wake it. An interrupted game returns paused.

# 16. Firmware Updates

The current prototype boards are retired from over-the-air updates: Atlas and the Sigils refuse update packages, and Atlas no longer shows Update available. The TurnHub owner updates them over USB. Keep a board powered until a USB update finishes; the board restarts and reconnects by itself.

# 17. Troubleshooting

## My Sigil Will Not Pair

- Check that Atlas is on and showing the lobby, then tap Pair a Sigil again.

- Hold the Sigil's joystick in for 3 seconds (or press BOOT) within the window. An administrator can lengthen Atlas's window.

- Move the Sigil closer to Atlas, or restart it.

- If it was paired with another Atlas, make it forget (Unpair, or hold BOOT for 3 seconds) and pair again.

## My Sigil Says Atlas Lost

Give it a moment; it reconnects by itself. If it doesn't, check that Atlas is on, restart the Sigil, and pair it again if asked. The game is safe on Atlas.

## Touches on the Atlas Screen Land in the Wrong Place

In the lobby, press and hold anywhere on the screen for 10 seconds, then press and release each cross as it appears.

## The Atlas Screen Shows a Code Nobody Asked For

Tap Cancel. A code only works for the account that asked for it, and it expires after 90 seconds.

## The Atlas Screen Says NO SD CARD

The game works normally. For detailed statistics, insert a microSD card; Atlas starts using it within a few seconds. If the label stays, try another card.

## The Wrong Player Has the Turn

If the active player can't pass, use **Master pass** in the Atlas screen's Table controls (section 9). A Game Master can correct other unusual states from the app.

## The Turn Timer Ran Out but the Turn Did Not Change

This is expected: TurnHub never passes a turn on its own on this Atlas. The active player passes when ready.

## I Cannot Change the Turn Timer

Join the table first. Any seated player can change it, in the lobby before the game starts.

## My Sigil Needs a Longer Press Than Expected

Hold times can be changed in Sigil accessibility. On a shared Sigil, the longer of the two players' times applies.

## A Tests Button Appeared in the Atlas Menu

It appears only while a developer test tool is connected. Players can ignore it.

## The Atlas Speaker Went Quiet

Atlas silences its speaker while two games are in progress at once. Its volume is set in the app or portal.

## The Browser No Longer Shows Play Controls

The portal is for administration only. Play from a Sigil or the TurnHub app.

# 18. What's Next

New TurnHub hardware is in development on the ESP32-S3:

- **A new Atlas:** almost entirely capacitive touch, with at most one physical button, and a richer, animated table screen.

- **One unified Sigil:** a single model with the thumbstick and light ring and a larger color screen, replacing the e-ink and OLED versions.

Newer Atlas firmware already adds Star Commander, official rules with House rules, a random starter when nobody is chosen, Back to setup during the first three minutes, not counting very short games, and creating an Atlas account from a player saved in the app. They need the new Atlas; the current prototype boards keep the behavior in this manual. This manual will be revised when the new hardware arrives.

Please report anything unexpected with the bug button in the app: what you were doing, which player was active, which Sigil was involved, what you expected and what happened.

# 19. The TurnHub Philosophy

TurnHub is not intended to play the game for you. It quietly handles the bookkeeping that gets in the way of playing.

TurnHub should answer the question: "Whose turn is it again?"

Then get out of the way.
