<!-- Generated from TurnHub Manual V0.11.docx by Android/tools/export_manual.py. Do not edit by hand. -->

# TurnHub User Manual

## Prototype Edition v0.11

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

The buttons along the bottom change with the game: Start (once two players have joined), Clear (hold, to empty the lobby) and Menu in the lobby; Cancel start during the countdown; Pause (or Resume) and Table during a game; Rematch, Reset and Menu after it. Menu holds Pair a Sigil (lobby only), QR codes, Tests (section 23) and Info; Back steps out one screen at a time. **Table** opens the less-used game controls, **Master pass** and **End match**, so they stay out of the way during play. A pressed button changes color and gains a heavier border, and buttons that must be held say so and count down while you hold them.

A red **NO SD CARD** label appears when no microSD card is in use. The game works without one; see section 19.

## Sigils

Sigils are the player-facing devices placed around the table. The prototype has two models:

- **E-ink Sigil:** a paper-like screen, a thumbstick that also clicks when pressed in, and a ring of lights. One E-ink Sigil can be shared by two players.

- **OLED Sigil:** a small bright screen, the same clicking thumbstick and the same ring of lights. An OLED Sigil can also be shared by two players.

A Sigil shows who is playing on it, whether it is their turn, their life total, the game status, pairing status, prompts, and which of its controls does what right now.

## Virtual Sigils

A phone, tablet, or computer can also participate through the TurnHub browser interface or the Android app.

Virtual Sigils provide many of the same player functions without requiring an additional physical module.

# 2. Before You Begin

For a typical game you will need:

- One TurnHub Atlas

- One or more Sigils, or browser-based Virtual Sigils

- Power for Atlas

- Power or sufficient battery charge for each physical Sigil

- Optionally, a microSD card in Atlas for detailed statistics (section 19)

- A phone, tablet, or computer for initial setup when desired. Play can begin without adding profiles, setting up an administrator, or ever opening the web portal. Without profiles, players are shown as “Player 1”, “Player 2” and so on, and their statistics are not saved.

TurnHub is designed to operate locally and does not require an Internet connection for normal game operation.

# 3. Starting TurnHub

## Power On Atlas

Connect power to Atlas. It shows the TurnHub logo for a moment, then the lobby.

The first time Atlas starts, and after a factory reset, it asks you to calibrate the touchscreen: press and release the center of each cross as it appears, four in all. To calibrate again later, for example if touches land in the wrong place, press and hold anywhere on the lobby screen for 10 seconds.

Do not begin pairing devices until Atlas shows the lobby.

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

The chosen code is framed and marked “shown”. If an administrator has set their own Wi-Fi password, the Wi-Fi code only appears while an administrator is verified at the table (section 17), so the password is not shown to everyone. An empty lobby explains how to join; its codes are under Menu.

**Info** (under Menu on the Atlas screen) shows the Wi-Fi name, the portal address, the firmware version, how many Sigils are online, the microSD card status and how long Atlas has been running. When newer firmware is available, Atlas’s small light blinks blue (red means a pairing window is open), and every Atlas screen shows an Update available tag (Update available for Atlas, if only Atlas is behind). Sigils say Update available on their screens too, and the app shows an Update available card with an Update now button for an Admin. The TurnHub app checks for new firmware and tells Atlas, since Atlas itself has no internet; install it with the app or the portal’s update pages.

## Using the Android App

The TurnHub Android app joins the Atlas Wi-Fi and shows the table. A player can sign in with their profile PIN, join the table in the lobby, pass, and pause or resume the game.

The app shows the same turn clock as the Sigils and the browser, because Atlas keeps the time.

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

Device Settings has **Factory reset** next to each Sigil and a **Factory reset Atlas** button. Both need an administrator who is verified at the table (section 17), and neither works during a game.

At Atlas itself, tap **Menu**, then **Device**, between games. Hold **Unpair Sigils** for 3 seconds (in the lobby, with nobody seated on a Sigil) to make Atlas forget every Sigil, or hold **Factory reset** for 10 seconds to erase Atlas. Like Atlas’s BOOT button, these need no administrator: anyone at the table can use them.

The same screen has **Sleep**: tap it between games and Atlas says “Going to sleep. Touch the screen to wake”, then its screen goes dark. Touch the screen (or press BOOT) to wake it. Atlas restarts: profiles, pairings and settings are kept, but the lobby empties and phones sign in again. Sigils show that Atlas is lost until it wakes.

- **A Sigil** erases everything it has saved, including its pairing, and restarts as new. Atlas forgets it. Nobody may be seated on it. If the Sigil is out of range, Atlas only forgets it; hold the Sigil’s Pair button for 3 seconds to clear it too.

- **Atlas** asks you to type RESET to confirm. It then erases every profile, PIN, statistic it holds, Sigil pairing, the Wi-Fi password (back to TurnHub-Setup) and all settings, and restarts as new, asking for touchscreen calibration. The microSD card is not erased. This cannot be undone.

# 6. Player Setup

Players can create or select a profile before beginning a game. A profile has a name, a PIN, and the player’s Sigil accessibility choices.

Profiles are separate from physical Sigils. A player is not permanently assigned to one device, so the same Sigil can be used by different players in different games.

## Two Players on One Sigil

Every Sigil can carry two players. After joining, choose **Add seat B** from its menu for the second player. The screen shows both names, and prompts say which seat (A or B) they are for.

# 7. Starting a Game

Any player seated at the table can set up and start the game. There is no table host.

1. Each player joins: choose **Join game** on a Sigil, or Join table in the browser or app.

2. Optionally choose who goes first: **Next starter** or **Random start** on a Sigil, or the starter choice in the browser.

3. Choose the game profile, starting life and turn timer on the Game tab in the browser.

4. Choose **Start game**, or tap **Start** on the Atlas screen. A 3-second countdown begins, which any seated player, or **Cancel start** on the Atlas screen, can cancel.

Game profiles and their starting life:

- Generic: 40

- Magic: 20

- Commander: 40

- Yu-Gi-Oh!: 8000

- Custom starting life: any whole number from 0 to 1,000,000

Changes to the game settings apply from the next game that starts.

# 8. Taking Turns

TurnHub always maintains a current active player. The active player’s Sigil shows YOUR TURN, and the Atlas screen shows their name and turn clock.

When finished, pass play to the next player: choose **Pass turn** on the Sigil (it is the likely choice on your turn), or in the browser or app. For 3 seconds after passing you can change your mind with **Undo pass** on the Sigil; after that the turn moves on. Meanwhile your Sigil shows PASSING and its light ring counts the 3 seconds down in green; every other Sigil shows which player is passing with an amber countdown, and the Atlas screen shows “Passing in 3s”. The table hears two falling ticks when a pass starts and two rising ticks if it is undone. On an OLED Sigil, clicking the stick again undoes the pass.

## A Stuck Turn: Master Pass

If the active player has stepped away or their Sigil is not responding, anyone at the table can move the game on. On the Atlas screen, tap **Table**, then hold **Master pass** for 2 seconds. The turn passes to the next player at once, with no undo window.

A master pass is recorded as a master pass, not as that player’s own pass. It is not available while the game is paused or while a win claim or elimination is waiting. A Game Master’s **Pass turn** in the browser is recorded the same way.

## Passing to Yourself

TurnHub supports passing a turn back to yourself when required by the game or table rules.

A distinct alert is used for a self-pass so it is not easily confused with normal turn progression.

## Turn Timer

Any seated player can choose the turn timer in the lobby, from the browser interface or the Android app. The setting applies from the next game that starts.

- Off: there is no countdown. After a turn has lasted five minutes, the active player’s Sigil shows a gentle long-turn reminder.

- Presets: 1, 2, 3 or 5 minutes.

- Custom: any whole number of seconds from 15 seconds to 60 minutes.

With a timer set, each turn counts down from the full time, shown as a clock and a bar on the Atlas screen. Pausing the game stops the countdown, and passing starts a fresh countdown for the next player.

When 10 seconds are left, the active player’s Sigil plays a short chirp and its light shows a slow warning pulse. The Atlas screen turns the clock red and still shows the time in numbers; the browser and the app show the time left and a warning.

When time runs out, the Sigil plays two low notes and its light stays steady. The turn does not end automatically: the player passes as usual.

# 9. Sigil Controls

A Sigil offers a short menu of what you can do right now. Atlas decides the choices, so the menu changes with the game: Join game in the lobby, Pass turn on your turn, Confirm win when a win claim is waiting on you, and so on.

## Choosing an Action

Click (press the stick straight in) is always the most likely action: **Pass turn** on your turn, Join, Start game, Confirm win or Rematch. To undo a pass, click again while the Sigil says it is passing. In a game, Left and Right change your life (section 10). The two Sigils show their other actions differently:

- **E-ink Sigil:** each action has its own direction. Push the stick the way shown, or click, to choose it. The bottom of the screen lists every choice; each line starts with a key: an arrow for a direction, or a filled circle for the click.

- **OLED Sigil:** push Up for **Menu**, a scrolling list of everything you can do right now (such as Pause, Claim win, Link phone or Leave lobby), followed by the Sigil's own Sleep and Device recovery. Push Up or Down to move through it, click (or push Right) to choose the highlighted line, and push Left to go back. The top line counts where you are in the list, and lines you must hold say (hold). Choosing an action closes the list. The bottom line of the screen names one key at a time, starting with the click, and moves on every few seconds.

## Device Menu

Each Sigil has its own menu that Atlas is not asked about. On the E-ink Sigil, outside a game, Up (or Down, if Up is in use) shows **Menu** and opens it. On the OLED Sigil, Sleep ends its Menu list, which opens with Up at any time, even during a game, and Unpair and Factory reset sit one step further in, under Device recovery, so they stay out of the way of play.

- **Unpair:** hold for 3 seconds (the click, on the E-ink Sigil). The Sigil forgets Atlas and shows Unpaired; hold the joystick in to pair it again.

- **Sleep:** choose it (push Up, on the E-ink Sigil). The Sigil shows how to wake it and sleeps; its lights go off. Click the joystick (or press its Pair button) to wake it; it restarts and reconnects to Atlas by itself.

- **Factory reset:** hold for 5 seconds (Down, on the E-ink Sigil). The Sigil erases everything it has saved, including its pairing, and restarts as new.

- **Back:** push Left to close the menu (or choose Back at the end of the OLED list; in Device recovery, Back and Left return to Menu). It also closes by itself after 10 seconds.

## Actions You Hold

Actions that are hard to undo must be held: **Claim win**, **Confirm out**, **Reset table**, and the device menu's **Unpair** and **Factory reset**. While you hold, the light ring fills up. Let go early to cancel. Each player can change the hold times (except the device menu's 3 and 5 seconds); see Accessibility Settings in section 13.

## Pair Control

The Pair (BOOT) button is reserved for pairing: press it to pair, hold it for 3 seconds to make the Sigil forget Atlas, or hold it for 10 seconds to factory reset it. The hold works at any time, even during a game, when the E-ink Sigil's device menu is not offered. If the case hides the button, an unpaired Sigil also starts pairing when you hold its joystick in for 3 seconds.

## Light Ring

The ring of lights shows the same thing as the screen: in the lobby it shows your player number as lit lights; during the game it shows your turn, waiting, paused, warnings and prompts. The words on the screen always carry the meaning, so nothing depends on color alone.

If a Sigil cannot hear Atlas for about 7 seconds (Atlas is switched off, restarting or out of range), its screen says Atlas lost, Searching… (NO ATLAS on the OLED Sigil) and one orange light sweeps back and forth around the ring. With Reduced motion, two opposite lights stay on instead. Game and lobby actions are hidden and their presses ignored, because nothing can reach Atlas. Menu stays on Up, so you can still put the Sigil to sleep, unpair it or factory reset it from its device menu; the Pair button still works too. When Atlas answers again, the Sigil returns to its normal screen on its own.

# 10. Life Tracking

When life tracking is enabled, players can adjust life totals through TurnHub. The Atlas screen shows every player’s life total.

For supported Magic-style configurations, TurnHub includes larger adjustment options such as:

+10 and -10

Other players may also request changes to another player’s life total.

When this occurs, TurnHub displays a confirmation prompt.

If no response is made, the current prototype can automatically confirm the change after approximately 15 seconds.

# 11. Commander Damage

TurnHub can track Commander damage separately from the player’s main life total.

This allows players to monitor Commander-specific damage without replacing normal life tracking.

Because game formats vary, players remain responsible for determining when a game rule requires elimination.

# 12. Pausing the Game

TurnHub includes a global pause function. Any player in the game can pause from their Sigil menu, the browser or the app, or tap **Pause** on the Atlas screen. Resume works the same way.

When paused:

- Normal turn progression is suspended

- Players can resolve questions or interruptions without losing the current game state

- The game can resume from the same position afterward

This can be useful for rules discussions, food breaks, disconnected hardware, or other interruptions.

# 13. Nudges and Alerts

Players can send a nudge when another player needs attention.

TurnHub can provide alerts through:

- Sigil lights and sounds

- The Atlas speaker

- Browser-based buzzer notifications

Alerts are designed to communicate useful events without turning the table into a constant stream of sounds and lights.

The Atlas speaker plays table-wide sounds. An administrator sets its volume (Off, Low, Medium or High) in Device Settings. Every sound also has a visible counterpart on a screen.

Turn-timer alerts (the 10-second warning and time running out) always appear as text in the browser and the app, as well as through the Sigil’s light and sound.

## Accessibility Settings

Each player can choose how their Sigil behaves. Sign in, then open My Account in the browser and find Sigil accessibility, or tap Sigil accessibility in the Android app. Your choices are saved with your profile on Atlas and follow you to whichever Sigil you use.

- Sigil sound: turn your Sigil’s tones off. Everything a tone means still appears as text in the browser and the app.

- Sigil lights: Standard; Reduced motion (steady lights and slow blinks only, with your turn bright and waiting dim); or Monochrome-safe (no two signals differ by color alone).

- Hold times: how long to hold for deliberate actions (1 to 4 seconds, normally 2) and to claim a win (3 to 10 seconds, normally 5). The win hold is always at least one second longer.

If two players share a Sigil, it uses the more accommodating choice: it stays quiet if either player turned sound off, uses reduced motion if either chose it, and uses the longer hold times.

When a decision is waiting on you, such as confirming a win or approving a life change, your Sigil plays two short tones (unless its sound is off) and the request appears on screen.

The browser’s own appearance is set separately on each phone or computer, under Appearance in My Account: themes including High contrast (chosen automatically when your device asks for more contrast), Reduce motion, and browser sound and vibration. The Android app follows the phone’s contrast, text size and screen-reader settings.

# 14. Claiming a Win

A player can claim a win by holding **Claim win** on their Sigil, or from the browser or app.

A win claim does not immediately end the game. Instead, TurnHub asks the remaining living players, one at a time, to approve the result with **Confirm win** or **Deny win**. The Atlas screen marks the player it is waiting on with CONFIRM.

All required players must approve the claim. If a player denies the request, the win claim is canceled and normal play continues.

This helps prevent an accidental button press from ending the game.

## After the Game

When a game ends, choose **Rematch** to play again with the same players, or **Reset** to empty the table. Both are on the Atlas screen as well as on the Sigils and in the browser.

# 15. Conceding

A player who is leaving the game can concede. On a Sigil, pause the game, choose **I’m out**, then hold **Confirm out**. On a shared Sigil, **Other seat** switches to the other player first.

Conceding removes that player from normal turn progression while preserving the rest of the game.

Earlier prototype versions may refer to this function as Eliminate. Current terminology uses Concede for a player intentionally leaving the game.

## Ending a Match as a Draw

If a match cannot or should not be finished, for example after a power cut, tap **Table** on the Atlas screen, then hold **End match** for 5 seconds while the game is running or paused. The button counts down while you hold it; a shorter press only shows a reminder.

The match ends with no winner. Every player’s statistics count one game played, with the result shown as a draw (players who had already conceded keep their result). The browser and the app show “Draw”. Any win claim, pending pass or elimination choice is canceled.

# 16. Browser Interface

Atlas provides a local browser interface for setup and game management.

Available functions include:

### Game

- Join or leave the table

- Start game and choose who goes first

- Choose the game profile, starting life and turn timer

- View current players

- Pass

- Adjust life and Commander damage

- Pause and resume

- Claim, confirm or deny a win

- Concede

- Send nudges

### Player

- Create or sign into a profile with a PIN

- Edit your name and PIN

- Sigil accessibility and browser appearance

- View and download your statistics

### System

- View Atlas status

- Device Settings (administrators): name, forget or factory reset Sigils, pairing window, speaker volume

- Wi-Fi password (administrators)

- Return table to lobby (administrators)

- Firmware update (administrators)

Some administrative functions require an administrator account, and some also require being verified at the table (section 17).

# 17. Administrators and Game Masters

## Setting Up the First Administrator

On a new Atlas, or after a factory reset, the browser shows a Set up this Atlas banner.

1. Create or sign into your profile, with a PIN.

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

# 18. Reconnecting

If a Sigil or browser client temporarily disconnects, TurnHub attempts to restore it without requiring the game to restart.

Because Atlas owns the authoritative game state, a reconnecting player receives the current state from Atlas rather than relying on potentially outdated information stored on the client.

If a device does not reconnect automatically:

- Confirm Atlas is still powered on.

- Confirm the device is within range.

- Reopen the TurnHub browser page or restart the Sigil.

- Pair the Sigil again if required.

Do not restart Atlas unless normal reconnection attempts fail.

# 19. Statistics and the microSD Card

Atlas keeps statistics for each profile. Without a microSD card it keeps the basics: games played, games won, the last result and the type of the last game.

With a microSD card in Atlas, it also keeps detailed statistics: turn times, fastest and longest turns, and details of the last game. The statistics page in the browser says when detailed statistics need the card.

- You can insert or remove the card while Atlas is running. Atlas notices within a few seconds and starts or stops using it without a restart. To be safe, avoid removing it just as a game ends, while Atlas saves statistics.

- Without a card, the Atlas screen shows a red NO SD CARD label, and Info shows “SD card: NOT INSERTED”.

- Playing never depends on the card.

# 20. Restarting Atlas

To restart Atlas, disconnect its power and connect it again. Atlas also restarts on its own after a Wi-Fi password change, a firmware update or a factory reset.

If a game was in progress, it comes back paused. Resume it to continue, or tap Table on the Atlas screen and hold End match for 5 seconds to end it as a draw.

Avoid disconnecting power while Atlas is saving settings.

# 21. Shutting Down

There is no shut-down command. For a normal shutdown:

1. End the current game, or pause it.

2. Disconnect power from Atlas.

3. Switch off the Sigils.

A game that was still in progress is restored, paused, the next time Atlas starts.

# 22. Privacy and Local Operation

TurnHub is designed around local-first operation.

Normal tabletop gameplay does not require game data to be sent to an external service.

Features involving statistics or potentially sensitive player information are intended to be opt-in.

TurnHub’s goal is to help manage the table without requiring players to hand over unnecessary personal data.

# 23. Troubleshooting

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

## The Browser Buzzer Does Not Make Sound

Some browsers prevent websites from playing audio before the user has interacted with the page.

Tap or click the TurnHub interface once, then try again.

## My Sigil Needs a Longer Press Than Expected

Hold times can be lengthened in Sigil accessibility. If two players share the Sigil, the longer of their hold times applies. You can also pause or claim a win from the browser or the app.

## A Tests Button Appeared in the Atlas Menu

It appears only while a TurnHub test harness, a developer tool that plays as extra Sigils, is connected. Players can ignore it.

# 24. Prototype Limitations

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

# 25. The TurnHub Philosophy

TurnHub is not intended to play the game for you.

It is intended to quietly handle the bookkeeping that gets in the way of playing.

TurnHub should answer the question:

“Whose turn is it again?”

Then get out of the way.

## TurnHub Prototype Documentation

**Manual Version:** 0.9
**Hardware:** Prototype (Atlas E32R28T touchscreen board; E-ink and OLED Sigils)
**Software:** Development build (Atlas 0.6.4-dev, Sigil 0.9.6-dev)
**Product names and specifications subject to change.**
