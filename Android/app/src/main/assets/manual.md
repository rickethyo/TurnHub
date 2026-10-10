<!-- Generated from TurnHub Manual V0.14.docx by Android/tools/export_manual.py. Do not edit by hand. -->

# TurnHub User Manual

## Prototype Edition v0.13

**TurnHub** is a tabletop turn management system designed to keep the game moving without putting a phone in the middle of the table.

The system consists of a central Atlas and one or more player Sigils. Atlas maintains the authoritative game state while Sigils give each player a simple physical interface for turns, life totals, actions, and other game events.

**Prototype Notice**
This manual describes the current TurnHub prototype. Controls, menus, terminology, and hardware may change before the final production version.

# 1. Meet TurnHub

## Atlas

Atlas is the central controller for the table. It has a 2.8-inch touchscreen and no other buttons.

It is responsible for:

- Maintaining the current game state

- Tracking whose turn it is

- Managing connected Sigils

- Handling game-wide actions

- Hosting the TurnHub web interface and its Wi-Fi network

- Coordinating turn, life, pause, win, and elimination events

- Showing the table on its touchscreen

Atlas is the authority for the game. Sigils, browsers, the Android app and the Atlas touchscreen send requests to Atlas, which decides the result and shares the new game state.

## The Atlas Screen

The top line shows what the table is doing (Lobby, Starting, Playing, Paused or Game Over) and how many Sigils are online; during and after a game it also shows the round and how long the match has run. Below it, Atlas shows whose turn it is by name, a turn clock, and a card for each player with their name, life total, their total time on their own turns and a label: TURN, OUT, WINNER, STARTS, or CONFIRM for the player a win claim is waiting on.

The buttons along the bottom change with the game: Start (once two players have joined), Clear (hold, to empty the lobby) and Menu in the lobby; Cancel start during the countdown; Pause (or Resume) and Table during a game; Rematch, Reset and Menu after it. Menu holds Pair a Sigil (lobby only), QR codes, Tests (section 27) and Info; Back steps out one screen at a time. **Table** opens the less-used game controls, **Master pass** and **End match**, so they stay out of the way during play. A pressed button changes color and gains a heavier border, and buttons that must be held say so and count down while you hold them.

A red **NO SD CARD** label appears when no microSD card is in use. The game works without one; see section 22.

## Sigils

Sigils are the player-facing devices placed around the table. The prototype has two models:

- **E-ink Sigil:** a paper-like screen, a thumbstick that also clicks when pressed in, and a ring of lights. One E-ink Sigil can be shared by two players.

- **OLED Sigil:** a small bright screen, the same clicking thumbstick and the same ring of lights. An OLED Sigil can also be shared by two players.

A Sigil shows who is playing on it, whether it is their turn, their life total, the game status, pairing status, prompts, and which of its controls does what right now.

## Virtual Sigils

An Android phone or tablet can also participate through the TurnHub app. The browser portal is for Atlas administration.

Virtual Sigils provide many of the same player functions without requiring an additional physical module.

An Android tablet or phone can lie in the middle of the table as one shared screen with a panel facing each player: tablet mode (section 17). With no Atlas, the app can keep a simple game by itself (section 18).

# 2. Before You Begin

For a typical game you will need:

- One TurnHub Atlas

- One or more Sigils, Virtual Sigils (Android phones or tablets), or one shared Android tablet in tablet mode

- Power for Atlas

- Power or sufficient battery charge for each physical Sigil

- Optionally, a microSD card in Atlas for detailed statistics (section 22)

- A phone, tablet, or computer for initial setup when desired. Play can begin without adding profiles, setting up an administrator, or ever opening the web portal. Without profiles, players are shown as “Player 1”, “Player 2” and so on, and their statistics are not saved.

TurnHub is designed to operate locally and does not require an Internet connection for normal game operation.

No Atlas with you? The Android app can run a game on its own tablet or phone and hand the results to Atlas later (section 18).

# 3. Starting TurnHub

## Power On Atlas

Connect power to Atlas. It shows the TurnHub logo for a moment, then the lobby.

The first time Atlas starts, and after a factory reset, it asks you to calibrate the touchscreen: press and release the center of each cross as it appears, four in all. To calibrate again later, for example if touches land in the wrong place, press and hold anywhere on the lobby screen for 10 seconds.

Do not begin pairing devices until Atlas shows the lobby.

## Setting Up a New Table

A new Atlas, or one that was factory reset, shows **Welcome to TurnHub** instead of the lobby. Setup takes a few minutes and is easiest with the Android app: open it and tap **Set up a new table**; it joins Atlas’s Wi-Fi by itself. Without the app, join the Wi-Fi network TurnHub-Atlas (password TurnHub-Setup) and open 192.168.4.1 in the browser.

- **Your account:** create one or sign in.

- **At the table:** type the code the Atlas screen shows. This account becomes the table’s Admin.

- **Sigils:** pair each one (section 5), or skip.

- **Updates:** install new firmware on Atlas and every Sigil, or choose Later. This needs internet on the phone; without it, Continue.

- **Secure Wi-Fi:** choose the table’s own Wi-Fi password, 8 to 63 characters. Finish saves it and restarts Atlas, and the app rejoins with the new password.

The browser covers the account, the code and the Wi-Fi password; pairing and updates are on the Atlas screen and in the app. To play first, tap **Skip for now** on the Atlas screen: setup comes back at the next start-up and under Menu. When setup is done, Atlas says You’re all set; tap **Pair a Sigil** or **Done**.

## Power On Your Sigils

Turn on each Sigil you plan to use. It shows the TurnHub logo, then either its Sigil number (it is paired and ready) or “Unpaired”.

# 4. Connecting to Atlas

Atlas creates its own Wi-Fi network called **TurnHub-Atlas**. Its password is **TurnHub-Setup** until an administrator changes it.

Players can connect using:

- The QR codes on the Atlas screen

- The local TurnHub address, http://192.168.4.1/portal

- A previously configured connection method, such as the Android app

## QR Codes on the Atlas Screen

Tap **Menu**, then QR codes, on the Atlas screen (in the lobby or after a game) and choose a code:

- **Wi-Fi:** joins the TurnHub-Atlas network.

- **Portal:** opens the TurnHub page.

- **Sign in:** opens the profile sign-in page.

The chosen code is framed and marked “shown”. If an administrator has set their own Wi-Fi password, the Wi-Fi code only appears while an administrator is verified at the table (section 20), so the password is not shown to everyone. An empty lobby explains how to join; its codes are under Menu.

**Info** (under Menu on the Atlas screen) shows the Wi-Fi name, the portal address, the firmware version, how many Sigils are online, the microSD card status and how long Atlas has been running. When newer firmware is available, Atlas’s small light blinks blue (red means a pairing window is open), and every Atlas screen shows an Update available tag (Update available for Atlas, if only Atlas is behind). Sigils say Update available on their screens too, and the app shows an Update available card with an Update now button for an Admin. The TurnHub app checks for new firmware and tells Atlas, since Atlas itself has no internet; install it with the app or the portal’s update pages.

## Using the Android App

The TurnHub Android app is on Google Play. While TurnHub is a prototype it is offered through Google Play’s testing track, so the owner adds you as a tester first. Choose Play on this device for a local game (section 18), or Connect to Atlas to join a table. The app joins the TurnHub-Atlas Wi-Fi by itself. It tries the saved password, then TurnHub-Setup, and asks only if neither works. On newer Android versions, allow Nearby devices when you choose an Atlas action; local play needs no nearby-device permission.

Sign in with your PIN or password, or choose **New here? Create an account** on the sign-in sheet to make one: a name, and either a PIN (quick to enter on a Sigil) or a password. An account made without a PIN, for example by a tablet (section 17), asks you to choose one the first time you sign in from a phone; enter it twice. From then on it is checked as usual.

Signed in, you can join the table in the lobby, pass, pause or resume, change life and Commander damage, claim or answer a win, concede and set Sigil accessibility. Atlas keeps the turn clock.

If Atlas stops answering, the app stays where it is and shows the last known table under an **Offline: reconnecting to Atlas** banner that says how old it is. It rejoins Atlas’s Wi-Fi by itself and carries on as soon as Atlas answers. Changes wait until then, except life and Commander damage in tablet mode (section 17).

The app carries this manual (the ? in its top bar), so you can read it before connecting.

# 5. Pairing a Physical Sigil

Pairing normally starts from Atlas’s touchscreen (Menu, then Pair a Sigil). The small button marked **BOOT** on a board does three jobs: a quick press pairs, holding it for 3 seconds unpairs, and holding it for 10 seconds factory resets that board. A case can hide a Sigil's BOOT button, so an unpaired Sigil also goes into pairing mode when you hold its joystick in for 3 seconds; its screen says so. On Atlas, BOOT is a backup for the touchscreen: a quick press opens pairing, 3 seconds forgets every Sigil, and 10 seconds erases Atlas, the same as Menu, then Device, on the touchscreen. A tone sounds at 3 and at 10 seconds, so release at the tone to stop there. Each Sigil's device menu also has Unpair and Factory reset (section 9).

## To Pair a Sigil

1. In the lobby, tap **Menu**, then Pair a Sigil, on the Atlas screen. Atlas returns to the lobby screen, opens a pairing window of 60 seconds and counts it down. An administrator can lengthen it to 90 or 120 seconds under Device Settings. Atlas’s small light blinks red while the window is open.

2. Within that time, put the Sigil into pairing mode: hold its joystick in for 3 seconds (the light ring fills as you hold), or press its Pair (BOOT) button. Its light ring blinks while it looks for Atlas. The Sigil’s own window is always 60 seconds, so with a longer Atlas window tap Pair a Sigil on Atlas first, then start the Sigil.

3. The Sigil and the Atlas screen each show the same 4-digit code. If they match, tap Codes match on Atlas (or, as an administrator verified at the table, in Device Settings in the browser). If they differ, tap Reject: something else answered the Sigil. After Codes match the Sigil shows its number.

4. If nobody answers within a minute, nothing is saved: pair again. After updating to firmware with the secure link, a Sigil paired with older firmware shows Unpaired: pair it again once the same way. From then on everything Atlas and the Sigil say to each other is encrypted.

5. Repeat for additional Sigils as needed.

Do not hold the BOOT button while plugging a Sigil in or restarting it: that puts the board into its programming mode instead of starting TurnHub.

## Forgetting a Sigil

A paired Sigil stays paired through restarts. To remove a pairing:

- **On the Sigil:** open its device menu (section 9) and hold **Unpair** for 3 seconds, or hold its Pair button for 3 seconds. A tone sounds, the Sigil forgets Atlas, its lights go off and its screen shows “Unpaired”. Keep holding the Pair button to 10 seconds to factory reset the Sigil instead.

- **On Atlas:** an administrator opens Device Settings in the browser and chooses Forget next to a Sigil, or Forget all Sigils. This works in the lobby, for Sigils nobody is seated on. A Sigil that is switched on and in range also forgets Atlas.

A forgotten Sigil can be paired again at any time.

## Factory Reset

Device Settings has **Factory reset** next to each Sigil and a **Factory reset Atlas** button. Both need an administrator who is verified at the table (section 20), and neither works during a game.

At Atlas itself, tap **Menu**, then **Device**, between games. Hold **Unpair Sigils** for 3 seconds (in the lobby, with nobody seated on a Sigil) to make Atlas forget every Sigil, or hold **Factory reset** for 10 seconds to erase Atlas. Like Atlas’s BOOT button, these need no administrator: anyone at the table can use them.

The same screen has **Sleep**: tap it between games and Atlas says “Going to sleep. Touch the screen to wake”, then its screen goes dark. Touch the screen (or press BOOT) to wake it. Atlas restarts: profiles, pairings and settings are kept, but the lobby empties and phones sign in again. Sigils show that Atlas is lost until it wakes.

- **A Sigil** erases everything it has saved, including its pairing, and restarts as new. Atlas forgets it. Nobody may be seated on it. If the Sigil is out of range, Atlas only forgets it; hold the Sigil’s Pair button for 3 seconds to clear it too.

- **Atlas** asks you to type RESET to confirm. It then erases every profile, PIN, statistic it holds, Sigil pairing, the Wi-Fi password (back to TurnHub-Setup) and all settings, empties the microSD card apart from the web portal kept on it, and restarts as new, asking for touchscreen calibration and then setup (section 3). This cannot be undone.

# 6. Player Setup

Players can create or select a profile (an account) before beginning a game. A profile has a name, usually a PIN or password, and the player’s Sigil accessibility choices.

Profiles are separate from physical Sigils. A player is not permanently assigned to one device, so the same Sigil can be used by different players in different games.

## Moving onto a Sigil During a Game

Nobody new joins a game once it has started, but a player who is playing from a phone or the tablet can move onto a Sigil. On a Sigil that is not in the game, choose **Join**: it lists only the players in the game who have no Sigil. Choose one, and that player carries on from the Sigil with their place, life and turn clock.

## Two Players on One Sigil

Every Sigil can carry two players. After joining, push Left in the lobby (**Add seat B**) for the second player; Left again drops seat B. The screen shows both names, prompts say which seat (A or B) they are for, and Down switches between the seats. In the lobby, tap either player on the Atlas screen to set turn order. Earlier and Later can swap A and B; moving past their pair moves the whole Sigil. Choose B left for B to take its turn before A, or B right for B to follow A. Both seats stay next to each other in turn order. Set this before starting the game. The order is kept for a rematch.

The same Player screen on Atlas has **Remove**: hold it for 2 seconds to take that player out of the lobby. Removing seat A of a shared Sigil removes seat B too.

# 7. Starting a Game

Any player seated at the table can set up and start the game. There is no table host.

1. Each player joins: choose Join game on a Sigil, or Join table in the Android app.

2. Optionally choose who goes first: Next starter or Random start on a Sigil, or I go first in the Android app.

3. Choose the game profile, starting life and turn timer on the Game tab in the Android app. Custom turn lengths accept 15 to 3600 seconds; choose Save game settings to apply.

4. Choose **Start game**, or tap **Start** on the Atlas screen. A 3-second countdown begins, which any seated player, or **Cancel start** on the Atlas screen, can cancel.

Game profiles and their starting life:

- Generic: 40

- Magic: 20

- Commander: 40

- Yu-Gi-Oh!: 8000

- Custom starting life: any whole number from 0 to 1,000,000

Changes to the game settings apply from the next game that starts.

Two-Headed Giant (Magic and Commander): turn it on in Game setup for teams of two. Players 1 and 2 are a team, then 3 and 4, and so on; change the turn order in the lobby to change teams. It needs an even number of players, at least 4.

- Teammates share one life total: 30 for Magic, 60 for Commander (you can change it). Either teammate can change it.

- Teammates take their turn together. Both see “Your turn”, and either can pass, cancel the pass or claim the win.

- Commander damage still counts per player, and each hit comes off the team’s life.

- When one teammate is eliminated or concedes, the whole team is out. The last team left wins, and both teammates get the win.

- The starting team skips its first draw; the Atlas screen reminds you during that first turn.

# 8. Taking Turns

TurnHub always maintains a current active player. The active player’s Sigil shows YOUR TURN, and the Atlas screen shows their name and turn clock.

When finished, pass play to the next player: choose Pass turn on the Sigil (it is the likely choice on your turn), or in the Android app. For 3 seconds after passing you can change your mind with Undo pass on the Sigil; after that the turn moves on. Meanwhile your Sigil shows PASSING and its light ring counts the 3 seconds down in green; every other Sigil shows which player is passing with an amber countdown, and the Atlas screen shows “Passing in 3s”. The table hears two falling ticks when a pass starts and two rising ticks if it is undone. On an OLED Sigil, clicking the stick again undoes the pass.

## A Stuck Turn: Master Pass

If the active player has stepped away or their Sigil is not responding, anyone at the table can move the game on. On the Atlas screen, tap **Table**, then hold **Master pass** for 2 seconds. The turn passes to the next player at once, with no undo window.

A master pass is recorded as a master pass, not as that player’s own pass. It is not available while the game is paused or while a win claim or elimination is waiting. A Game Master’s Pass turn in the app is recorded the same way.

## Passing to Yourself

TurnHub supports passing a turn back to yourself when required by the game or table rules.

A distinct alert is used for a self-pass so it is not easily confused with normal turn progression.

## Turn Timer

Any seated player can choose the turn timer in the lobby in the Android app. It applies from the next game that starts.

- Off: there is no countdown. After a turn has lasted five minutes, the active player’s Sigil shows a gentle long-turn reminder.

- Presets: 1, 2, 3 or 5 minutes.

- Custom: any whole number of seconds from 15 seconds to 60 minutes.

With a timer set, each turn counts down from the full time, shown as a clock and a bar on the Atlas screen. Pausing the game stops the countdown, and passing starts a fresh countdown for the next player.

When 10 seconds are left, the active player’s Sigil plays a short chirp and its light shows a slow warning pulse. The Atlas screen turns the clock red and still shows the time in numbers; the app show the time left and a warning.

When time runs out, the Sigil plays two low notes and its light stays steady. The turn does not end automatically: the player passes as usual.

# 9. Sigil Controls

A Sigil offers a short menu of what you can do right now. Atlas decides the choices, so the menu changes with the game: Join game in the lobby, Pass turn on your turn, Confirm win when a win claim is waiting on you, and so on.

## Choosing an Action

Every Sigil uses the same keys. Click (press the stick straight in) is the obvious next step: Join, Start game, **Pass turn** on your turn (Undo pass while it is passing), Resume, Rematch, Confirm win or Confirm out. In a game, Left and Right change your life (section 10). In the lobby, Left adds or drops seat B and Right picks the next starter. Down switches seat on a shared Sigil. Up always opens **Menu**, with every other action Atlas offers right now (such as Cmd damage, Pause, Claim win, I’m out, Partner, Random start, Leave lobby, Switch game or Link phone), then **Device**. The two Sigils show Menu differently:

- **E-ink Sigil:** Menu comes in pages of up to four choices, one each on the click, Up, Right and Down. The bottom of the screen lists them; each line starts with an arrow for a direction or a filled circle for the click. While more choices follow, Down is **More**. Left goes back a page, then out. The header names the page, such as Page 1 of 2.

- **OLED Sigil:** Menu is a scrolling list of everything you can do right now, ending with Device and Back. Push Up or Down to move through it, click (or push Right) to choose the highlighted line, and push Left to go back. The top line counts where you are in the list, and lines you must hold say (hold). Choosing an action closes the list. The bottom line of the game screen names one key at a time, starting with the click, and moves on every few seconds.

## Screen Themes

Each Atlas and Sigil remembers its own screen theme: Graphite, Daylight, Brass or High contrast. Changing one device does not change another device, the app, or the browser. Unpairing keeps the choice; factory reset clears it. Each device also remembers a separate Invert setting. It flips black and white on Sigils and reverses screen colors on Atlas. Changing theme keeps this setting; unpairing keeps it and factory reset clears it. The setting leaves player LED preferences unchanged. Atlas QR codes keep their usual black-on-white format.

On Atlas, between games, tap Menu, then Device, then Theme. Tap a named theme to preview and save it immediately. The framed choice is selected; Back returns to Device. Graphite is the default. Tap Invert: off/on on the same page to toggle inversion.

On a Sigil, push Up for Menu, choose Device when it is listed, then choose Theme. Each tap cycles to the next theme while the menu stays open. OLED shows the current name beside Theme; e-paper shows it under Device. The local menu works while Atlas is unavailable or the Sigil is unpaired. Close the menu before holding the joystick to pair. Choose Invert to toggle the screen polarity. On e-paper, choose More on the first Device page to reach Theme and Invert; INV beside the theme name means inversion is on. OLED shows Invert: on/off in the Device list.

Daylight uses dark text on a light OLED background; other OLED themes use light text on dark. E-paper keeps black text on white: Graphite uses chevrons and a life shield, Daylight uses outline icons and a heart, Brass uses engraved icons and a gauge with serif lettering, and High contrast uses boxed icons and heavier borders. Modern themes use clear sans-serif type. The screens always show turn and warning states in words. Calm ring lights default to teal for Graphite, blue for Daylight, amber for Brass and soft white for High contrast. A player’s saved ring color overrides the theme default. Action and warning colors and patterns keep their usual meaning. Each theme has a header emblem on all three displays: Graphite diamond, Daylight sun, Brass gear and High contrast boxed hourglass. Inverted Daylight OLED uses light text on a dark background.

## Device Menu

Each Sigil has its own **Device** menu that Atlas is not asked about. It is the last entry in Menu on both Sigils, at any time, even during a game. When Atlas cannot be reached, Menu opens straight on it.

- **Unpair:** hold for 3 seconds. The Sigil forgets Atlas and shows Unpaired; hold the joystick in to pair it again.

- **Sleep:** choose it. The Sigil shows how to wake it and sleeps; its lights go off. Click the joystick (or press its Pair button) to wake it; it restarts and reconnects to Atlas by itself.

- **Factory reset:** hold for 5 seconds. The Sigil erases everything it has saved, including its pairing, and restarts as new.

- **Back:** push Left to step back out, or choose Back at the end of the OLED list. The menu also closes by itself after 10 seconds.

## Actions You Hold

Actions that are hard to undo must be held: **Claim win**, **Confirm out**, **Reset table**, and the device menu's **Unpair** and **Factory reset**. While you hold, the light ring fills up. Let go early to cancel. Each player can change the hold times (except the device menu's 3 and 5 seconds); see Accessibility Settings in section 13.

## Pair Control

The Pair (BOOT) button is reserved for pairing: press it to pair, hold it for 3 seconds to make the Sigil forget Atlas, or hold it for 10 seconds to factory reset it. The hold works at any time, even during a game. If the case hides the button, an unpaired Sigil also starts pairing when you hold its joystick in for 3 seconds.

## Light Ring

The ring of lights shows the same thing as the screen: in the lobby it shows your player number as lit lights; during the game it shows your turn, waiting, paused, warnings and prompts. The words on the screen always carry the meaning, so nothing depends on color alone.

If a Sigil cannot hear Atlas for about 7 seconds (Atlas is switched off, restarting or out of range), its screen says Atlas lost, Searching… (NO ATLAS on the OLED Sigil) and one orange light sweeps back and forth around the ring. With Reduced motion, two opposite lights stay on instead. Game and lobby actions are hidden and their presses ignored, because nothing can reach Atlas. Menu stays on Up, so you can still put the Sigil to sleep, unpair it or factory reset it from its device menu; the Pair button still works too. When Atlas answers again, the Sigil returns to its normal screen on its own.

# 10. Life Tracking

When life tracking is enabled, players can adjust life totals through TurnHub. The Atlas screen shows every player’s life total.

On a Sigil, Left and Right change your own life during a game; hold to repeat. The light ring shows the change you are making, and the Sigil sends it 2 seconds after your last press. In games that start at 1000 or more, a Sigil steps by 100.

On the Atlas screen, tap a player’s card for -5, -1, +1 and +5, or Concede.

In the app and tablet mode the life steps are 1 and 5 (100 and 1000 for Yu-Gi-Oh!). Custom signed changes are also available.

Other players may also request changes to another player’s life total.

The player asked must approve it: Right approves and Left denies on a Sigil, or use the buttons in the Android app.

If they do not answer, the change is accepted after 15 seconds, or after 30 or 60 seconds if that player chose a longer time in Sigil accessibility (section 13). Pausing the game does not stop that time.

# 11. Commander Damage

TurnHub can track Commander damage separately from the player’s main life total.

Damage is recorded by the player who received it, from each opponent’s commander (and their partner, if they have one). Each hit also comes off that player’s life, or the team’s life in Two-Headed Giant, and a correction gives it back. Record it on your Sigil (below), on the Game screen of the Android app, or on your panel in tablet mode under More.

Partner commanders are off until a player turns them on: choose Partner from the Sigil’s Menu, or enter damage from a second commander in the Android app. Once a partner has dealt damage it stays on until a rematch or reset.

Because game formats vary, players remain responsible for determining when a game rule requires elimination. Recording 21 Commander damage does not eliminate a player by itself.

## Commander Damage from a Sigil

Record damage on the Sigil of the player who received it. On a shared Sigil, first show the correct seat with Down (Switch seat). Push Up for Menu and choose **Cmd damage**. The receiving name and A/B seat stay fixed while you enter the hit.

Use Left/Right to select who hit you and click; with partners on, choose Commander 1 or 2 and click; then enter the amount. Hold Left/Right to repeat. Click to review the life and Commander damage totals; click again to apply both together. Up goes back; Down cancels. This works during anyone’s turn without pausing the game.

Undo hit previews and reverses the last hit recorded this way for the shown seat. It restores the life lost and removes that Commander damage together. A correction to the same damage entry removes that undo option. Entry pages and Undo clear on a restart or new match; recovered life and damage totals remain.

# 12. Pausing the Game

TurnHub includes a global pause function. Any player in the game can pause from their Sigil menu, the Android app, or tap Pause on the Atlas screen. Resume works the same way.

When paused:

- Normal turn progression is suspended

- Players can resolve questions or interruptions without losing the current game state

- The game can resume from the same position afterward

This can be useful for rules discussions, food breaks, disconnected hardware, or other interruptions.

# 13. Nudges and Alerts

While a game runs, any player whose turn it is not can nudge the active player with Nudge in the Android app. The active player’s Sigil plays the nudge sound (if its sound is on), and their phone shows "<name> nudged you" and vibrates. Each player can nudge once every 30 seconds, and a Game Master can mute an account’s nudges, which stops that account sending them.

TurnHub can provide alerts through:

- Sigil lights and sounds

- The Atlas speaker

- App and Sigil alerts

The app shows turn and decision status in words. Native vibration and Android turn notifications supplement those cues; foreground event-sound and vibration preferences remain planned.

Check the player panel for Confirm or Deny when a win claim needs your answer. A tablet pause overlay does not cover these controls.

Turn-timer alerts (the 10-second warning and time running out) always appear as text in the app, as well as through the Sigil’s light and sound.

## Accessibility Settings

Each player can choose how their Sigil behaves. Sign in, then open Sigil accessibility in the Android app. Your choices are saved with your profile on Atlas and follow you to whichever Sigil you use.

- Sigil sound: turn your Sigil’s tones off. Everything a tone means still appears as text in the app.

- Sigil lights: Standard; Reduced motion (steady lights and slow blinks only, with your turn bright and waiting dim); or Monochrome-safe (no two signals differ by color alone).

- Hold times: how long to hold for deliberate actions (1 to 4 seconds, normally 2) and to claim a win (3 to 10 seconds, normally 5). The win hold is always at least one second longer.

- Time to answer life changes other players ask for: 15, 30 or 60 seconds (normally 15).

If two players share a Sigil, it uses the more accommodating choice: it stays quiet if either player turned sound off, uses reduced motion if either chose it, and uses the longer hold times.

When a decision is waiting on you, such as confirming a win or approving a life change, your Sigil plays two short tones (unless its sound is off) and the request appears on screen.

The administration portal has its own theme selector. The Android app has themes and Reduce motion, and follows the phone’s contrast, text size and screen-reader settings.

# 14. Claiming a Win

A player can claim a win by holding Claim win on their Sigil, or from the Android app.

A win claim does not immediately end the game. Instead, TurnHub asks the remaining living players, one at a time, to approve the result with **Confirm win** or **Deny win**. The Atlas screen marks the player it is waiting on with CONFIRM.

All required players must approve the claim. If a player denies the request, the win claim is canceled and normal play continues.

This helps prevent an accidental button press from ending the game.

## After the Game

When a game ends, choose Rematch to play again with the same players, or Reset to empty the table. Both are on the Atlas screen as well as on the Sigils and in the app.

# 15. Conceding

A player who is leaving the game can concede. On a Sigil, choose I’m out from Menu, then hold Confirm out. On a shared Sigil, Other seat switches to the other player first. In the app or tablet mode, choose Concede and confirm.

Conceding removes that player from normal turn progression while preserving the rest of the game.

Earlier prototype versions may refer to this function as Eliminate. Current terminology uses Concede for a player intentionally leaving the game.

## Ending a Match as a Draw

If a match cannot or should not be finished, for example after a power cut, tap **Table** on the Atlas screen, then hold **End match** for 5 seconds while the game is running or paused. The button counts down while you hold it; a shorter press only shows a reminder.

The match ends with no winner. Every player’s statistics count one game played, with the result shown as a draw (players who had already conceded keep their result). The browser and the app show “Draw”. Any win claim, pending pass or elimination choice is canceled.

# 16. Atlas Administration Portal

The local browser portal is for Admin setup, maintenance and recovery. Personal and shared-tablet play, player account preferences, statistics and Game Master controls are in the Android app.

Open http://192.168.4.1/portal on Atlas Wi-Fi and sign in as Admin. Initial setup can create the first Admin account, verify the Atlas code and secure Wi-Fi.

Admin controls cover Wi-Fi, the table-code setting, pairing decisions, speaker/pairing window, device names/forget/reset and account roles/archive/restore. Choose Save for staged settings.

Signed local Atlas, Sigil and portal package installation remains available. Developer diagnostics and hardware tests require Developer permission, including for an Admin.

Atlas 0.7.6 and portal 2.0.0 form the cutover. A missing card or installed v1 pack uses flash administration recovery; install a signed v2 pack and reload old browser tabs. Browser /tablet and /stats open recovery.

Play, shared tablet mode, personal preferences/statistics and Game Master controls are in Android. The app’s game selector follows Game 1 or Game 2; Atlas validates every action.

The app’s Players screen displays an installation QR. After installing, join Atlas Wi-Fi and choose Connect. A signed-in player can refresh Paired Sigils, request attachment of empty seat A in the lobby and confirm Link phone on that Sigil.

# 17. Tablet Mode

In tablet mode one Android tablet or phone lies in the middle of the table and acts for everyone. Panels face their seats and show name, life, Commander damage and turn clock. Atlas remains authoritative.

## Turning It On

- In the Android app, sign in, open My account and tap Open tablet mode. Browser tablet mode is retired.

- Tap **Show a code on Atlas** and enter the six digits the Atlas screen shows. This proves the tablet is at the table. An account with the **Tablet access** role skips the code (section 20).

- The tablet stays in tablet mode until you choose **Leave tablet mode** or sign out. Seated players stay seated when it leaves. The app keeps the screen on during play.

## Seating Players

In the lobby, add everyone who is playing with **Seat player**. Type a new name to create an account for that player with no PIN, or pick a saved player. A saved player who keeps a PIN types it on the tablet, unless they turned on **Allow Sigil and tablet use without a PIN** in My account. Use the arrows to set turn order; panels sit clockwise in that order. Then choose the game, starting life and Two-Headed Giant, and tap **Start the game**.

Players seated by the tablet can still use their own Sigil or phone as well: a Sigil’s Join picker lists them as at the table, and a phone can sign in to the same player.

## Playing on the Panels

- Tap the top half of a panel to add life and the bottom half to subtract it; hold to repeat the bigger step.

- The player whose turn it is holds **Hold to pass**. While it says **Passing… tap to keep**, a tap undoes the pass.

- **More** on a panel holds **Claim the win** and **Concede** (tap again to confirm) and Commander damage received from each opponent, with partner commanders when needed.

- **Menu** in the middle of the table holds **Pause**, **Turn panels around**, **End without a winner** (a draw) and **Leave tablet mode**. After the game choose **Rematch** or **New players**.

If Atlas stops answering during a game, the panels keep taking life and Commander damage. The changes are saved with the time they were tapped and sent to Atlas as soon as it answers, and Atlas applies them by its usual rules. A change Atlas can no longer apply, for example after Atlas restarted or the player left the game, is dropped with a note. Passing, pausing, conceding, win claims and seating wait for Atlas.

# 18. Playing Without Atlas

The Android app can keep a game on this phone or tablet with no Atlas, account or network. Tap **Play on this device** on the first screen, or **Resume game** if a local match is running or paused. Local play is available while the app is looking for Atlas; choosing it stops that connection attempt. The app remembers device play for its next launch. Back returns to the connection screen without ending the local game; an explicit Atlas connection, search or setup chooses Atlas for future launches.

- Add everyone who is playing. Type a name to create a local player, or pick a saved player from this device. Short IDs distinguish people with equal names; names never select Atlas accounts automatically.

- Use the arrows to set turn order, choose the game and starting life, and tap **Start the game**.

- The table looks and works like tablet mode (section 17): life, Commander damage, passing, pause, concede and the winner. There is no turn timer, no Two-Headed Giant, and no phones or Sigils in this mode.

Atlas import is optional (firmware 0.7.2 or later). Connect and sign in, open Device players and history, then Load Atlas players for import. For a match, choose Link this match for Atlas import and explicitly pick a different Atlas profile for every player. Nothing is selected by name. Check the destination Atlas ID and all player choices, then Import mapped match. Local players and match results stay on this device. History shows pending, imported or needs-attention status and the response reason. Retry a pending import with the same mapping after reconnecting; review a rejected match before explicitly linking it again. The app never automatically resends an imported match. Atlas remembers only its last 64 import IDs, and storage failures or power loss can leave partial credit; acknowledged partial credit is shown and must be reviewed rather than blindly replayed.

This device saves the current game and reusable local players when you close the app. Device players and history on Home, or Players and history in the local lobby, shows finished matches and each local player’s played, won and draw totals. History has no automatic pruning and stays after import or rejection. Clearing app data or uninstalling removes it; export/backup is not available yet. Older queued records are kept without guessing their local identities; earlier discarded records cannot be recovered.

A game without Atlas is separate from any game Atlas is running: nothing in it changes a game on Atlas.

# 19. Two Games on One Atlas

One Atlas can run two games at once, **Game 1** and **Game 2**, each with its own lobby, players, settings and turn order. Everyone starts in Game 1. This is new in the prototype and still being tested at the table.

- **The Android app’s Game on this Atlas selector chooses Game 1 or Game 2 in personal and shared-tablet mode. Switching leaves a personal lobby seat; Atlas rejects switching during an active personal game. A shared-tablet switch keeps its players at their original game.**

- **On a Sigil,** choose **Switch game** from its menu while it is not in a game. It leaves its lobby and goes to the other game; a shared Sigil takes both seats with it.

- A player, a Sigil or a phone is in one game at a time.

- The Atlas screen, pairing and the tablet show Game 1 for now. The app shows the game your account is in, otherwise Game 1.

While both games are in progress the Atlas speaker stays quiet, so neither table hears the other’s sounds; screens, Sigil lights and Sigil sounds work as usual. Firmware updates, Atlas factory reset and Sleep wait until both games are between games.

# 20. Administrators and Game Masters

## Setting Up the First Administrator

On a new Atlas, or after a factory reset, the setup in section 3 makes your account the Admin. In the browser, the same step appears as a Set up this Atlas banner:

1. Create or sign into your account, with a PIN or password.

2. Select **Make my account the initial Admin**.

3. The Atlas screen shows a six-digit code and a QR code. Enter the code on your phone, or scan the QR code with the same phone.

Your profile is now the administrator. Other accounts can be given roles later under Device Settings.

## Verify at the Table

Some administrator actions need proof that you are at the table: changing the Wi-Fi password, naming devices, updating firmware, Return table to lobby, and factory reset. When you try one, the browser asks for a code automatically; you can also choose **Verify at the table** in Device Settings.

- The Atlas screen shows a six-digit code and a QR code for 90 seconds. They only work for the account that asked.

- Enter the code on your phone, or scan the QR code with the same phone.

- You stay verified for 10 minutes. **Stop** in Device Settings ends it early.

- Five wrong codes cancel the code; ask for a new one.

- If a code appears that nobody at the table asked for, tap **Cancel** on the Atlas screen.

Anyone at the table can read the code aloud to the person entering it.

## Game Master

TurnHub supports a privileged Game Master role for table setup, game administration, correcting unusual game states, troubleshooting and organized play. Exact permissions are still being refined for the prototype.

When a Game Master resets a player’s connections or removes them from a game, TurnHub records it privately with that player’s statistics. Only the player can see these counts, on their statistics page after signing in with their PIN. Game Masters, other players and statistics downloads never show them.

## Roles and Tablet Access

An administrator gives roles under Manage people in the administration portal, or Device Settings in the app: Admin, Game Master, Developer and Tablet access. Roles require a PIN or password. Tablet access permits tablet mode without a table code and grants no other administration permissions.

# 21. Reconnecting

If a Sigil or browser client temporarily disconnects, TurnHub attempts to restore it without requiring the game to restart.

Because Atlas owns the authoritative game state, a reconnecting player receives the current state from Atlas rather than relying on potentially outdated information stored on the client.

If a device does not reconnect automatically:

- Confirm Atlas is still powered on.

- Confirm the device is within range.

- Reopen the Android app or restart the Sigil. For maintenance, reload the administration portal.

- Pair the Sigil again if required.

Do not restart Atlas unless normal reconnection attempts fail.

The Android app does not give up when Atlas goes quiet: it shows the last known table, marked offline, and reconnects by itself (section 4).

# 22. Statistics and the microSD Card

Atlas keeps statistics for each profile. Without a microSD card it keeps the basics: games played, games won, the last result and the type of the last game.

With a microSD card, Atlas also keeps detailed turn statistics and last-game details. View them under statistics in the Android app; it explains when the card is needed.

- You can insert or remove the card while Atlas is running. Atlas notices within a few seconds and starts or stops using it without a restart. To be safe, avoid removing it just as a game ends, while Atlas saves statistics.

- Without a card, the Atlas screen shows a red NO SD CARD label, and Info shows “SD card: NOT INSERTED”.

- Playing never depends on the card.

- Games played on this device have their own local history and totals (section 18), separate from Atlas statistics.

# 23. Updating Firmware

Atlas, the Sigils and the web portal are updated over the air with signed update packages. Each device checks the signature itself and refuses a package that is unsigned, altered, made for another device or older than what it runs.

- **With the Android app:** while the phone has internet, the app checks for new releases and tells Atlas. Atlas, the Sigils and the app then show **Update available**. At the table, an administrator taps **Update now**; the app installs the update on Atlas and then on each Sigil that needs it. Atlas itself never needs the internet.

- **In the browser:** an administrator can install a package file on the Update page (Atlas and the portal) or the Sigil firmware page.

- Updates need an administrator verified at the table (section 20), and the table must be in the lobby or after a game. One update runs at a time.

Atlas restarts after its own update, and Sigils restart and reconnect by themselves. Keep Atlas and the Sigils powered until the update finishes.

# 24. Restarting Atlas

To restart Atlas, disconnect its power and connect it again. Atlas also restarts on its own after a Wi-Fi password change, a firmware update or a factory reset.

If a game was in progress, it comes back paused. Resume it to continue, or tap Table on the Atlas screen and hold End match for 5 seconds to end it as a draw.

Avoid disconnecting power while Atlas is saving settings.

# 25. Shutting Down

There is no shut-down command. For a normal shutdown:

1. End the current game, or pause it.

2. Disconnect power from Atlas.

3. Switch off the Sigils.

A game that was still in progress is restored, paused, the next time Atlas starts.

# 26. Privacy and Local Operation

TurnHub is designed around local-first operation.

Normal tabletop gameplay does not require game data to be sent to an external service.

Features involving statistics or potentially sensitive player information are intended to be opt-in.

TurnHub’s goal is to help manage the table without requiring players to hand over unnecessary personal data.

# 27. Troubleshooting

## My Sigil Will Not Pair

- Confirm Atlas is powered on and showing the lobby.

- Tap Pair on the Atlas screen again.

- Hold the Sigil’s joystick in for 3 seconds (or press its Pair (BOOT) button) within the pairing window. If you need more time, an administrator can lengthen Atlas’s window under Device Settings.

- Move the Sigil closer to Atlas.

- Restart the Sigil if necessary.

- If the Sigil was paired with a different Atlas, make it forget (Unpair in its device menu, or hold its Pair button for 3 seconds), then pair it again.

## My Sigil Disconnected

A Sigil that cannot reach Atlas shows Atlas lost and an orange light sweeping around its ring (section 9). Allow it a moment to reconnect; it returns to its normal screen on its own.

If it does not:

- Confirm Atlas is still available.

- Restart the Sigil.

- Pair it again if requested.

The game state should remain controlled by Atlas.

## Touches on the Atlas Screen Land in the Wrong Place

In the lobby, press and hold anywhere on the screen for 10 seconds, then press and release each cross as it appears.

## The Atlas Screen Shows a Code Nobody Asked For

Tap Cancel. A code only works for the account that asked for it, and it expires after 90 seconds.

## The Atlas Screen Says NO SD CARD

The game works normally. To keep detailed statistics, insert a microSD card; Atlas starts using it within a few seconds, without a restart. If the label stays, the card may be unreadable: try another card. Atlas never formats a card.

## The Wrong Player Has the Turn

Use the available game controls to correct the active player.

If the active player cannot pass, use **Master pass** on the Atlas screen’s Table controls (section 8). If that does not resolve the problem, use Game Master access.

## A Player Accidentally Claimed a Win

Other players can deny the win request.

A denied request cancels the claim and returns the game to normal play.

## Someone Changed My Life Total

Life changes requested by another player display a confirmation prompt.

Review the requested change before accepting it.

## The Turn Timer Ran Out but the Turn Did Not Change

This is expected. Running out of time is a reminder only; TurnHub never passes a turn on its own. The active player passes when ready.

## I Cannot Change the Turn Timer

Join the table first: any seated player can change the turn timer, and only in the lobby before the game starts.

## The Browser No Longer Shows Play Controls

Use the TurnHub Android app or a Sigil for play. The portal handles administration. Install Atlas firmware 0.7.6 and portal pack 2.0.0 together; earlier installed portal packs use the cardless administration fallback on this firmware.

Tap or click the TurnHub interface once, then try again.

## My Sigil Needs a Longer Press Than Expected

Hold times can be lengthened in Sigil accessibility. If two players share the Sigil, the longer of their hold times applies. You can also pause or claim a win from the Android app.

## A Tests Button Appeared in the Atlas Menu

It appears only while a TurnHub test harness, a developer tool that plays as extra Sigils, is connected. Players can ignore it.

## The App Says Offline: Reconnecting to Atlas

Atlas has stopped answering. Check that Atlas is on and the phone is in range; the app rejoins its Wi-Fi and carries on by itself. In tablet mode, life and Commander damage tapped meanwhile are sent when Atlas answers.

## A Game Played Without Atlas Is Missing From Statistics

Connect the app that kept the game to Atlas (firmware 0.7.2 or later), sign in and explicitly map each player through Device players and history (section 18). Names never select an Atlas account. Pending work can be retried with the same mapping; rejected records stay available with their reason. Import never deletes the local result.

## Tablet Mode Asks for a Code Every Time

If the table code setting is on, prove the tablet is at the table when enabling tablet mode. With table code off, or the account’s Tablet access role, no code is needed.

## The Atlas Speaker Went Quiet

Atlas silences its speaker while two games are in progress at once (section 19). Its volume is set in Device Settings.

# 28. Prototype Limitations

The current TurnHub prototype is still under active development.

You may encounter:

- Temporary interface elements

- Changing terminology

- Unfinished enclosure designs

- Features that move between software versions

- Different behavior between physical and Virtual Sigils

- Diagnostic controls not intended for the final product

Prototype users should report unexpected behavior with as much detail as possible.

Helpful information includes:

- What you were doing

- Which player was active

- Which Sigil was involved

- What you expected to happen

- What actually happened

- Whether the issue could be reproduced

# 29. The TurnHub Philosophy

TurnHub is not intended to play the game for you.

It is intended to quietly handle the bookkeeping that gets in the way of playing.

TurnHub should answer the question:

“Whose turn is it again?”

Then get out of the way.

## TurnHub Prototype Documentation

**Manual Version:** 0.13
**Hardware:** Prototype (Atlas E32R28T touchscreen board; E-ink and OLED Sigils)
**Software:** Atlas 0.7.4, Sigil 0.9.15, TurnHub Android app (Google Play)
**Product names and specifications subject to change.**
