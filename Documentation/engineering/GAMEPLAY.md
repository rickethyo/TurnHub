# Gameplay: Turns, Life, Commander and Cues

The rules Atlas runs during a match, and how they reach players as light,
sound and text. TurnHub tracks life and outcomes. Automatic elimination at zero
or negative life is on by default and may be disabled when cards change the
loss condition; Commander damage totals alone do not trigger elimination.

## Game profiles and settings

| Profile | Starting life |
|---|---|
| Generic (default) | 40 |
| Magic | 20 |
| Commander | 40, plus Commander damage |
| Star Commander | 40, Commander rules for exactly five players (below) |
| Yu-Gi-Oh! | 8000, counted in hundreds on every client |
| Pokémon (phone only) | 6 Prize cards left; the app game only (below) |
| Lorcana (phone only) | 0 lore, counting up to 20 to win; the app game only (below) |
| Death May Die (phone only) | 0 wounds, counting up; cooperative, the app game only (below) |

- **Official rules** (2026-10-14, owner request; host tests only, *needs a
  playtest*): Magic (20 life, 10 poison), Commander (40, 10), Two-Headed Giant
  Magic (30, 15 shared, exactly two teams of two) and Yu-Gi-Oh! (8000, exactly
  two players) keep those values unless the table turns on **House rules**
  (`GameSettings::houseRules`, `houseRules` on `/api/game/settings`, bit 4 of
  the `gamecfg` and recovery flags byte). Atlas saves the official values
  whatever a request carries (`applyOfficialRules`), refuses a live poison-limit
  change, and refuses Start on the wrong table size (`validOfficialTable`).
  Custom modes (Generic, Star Commander, Two-Headed Giant Commander) stay
  editable. The standalone app follows the same table (`OfficialRules.kt`).
- **Pokémon** (2026-10-16, owner request; JVM tests only, *needs a
  playtest*): phone-hosted games only. Atlas doesn't run or import the format
  (`GameProfile.atlasSupported`), so its games stay on the device. The panel
  number is the Prize cards a player has left (6, exactly two players unless
  house rules are on). Taking the last one wins at once. No one is eliminated
  at zero, and the other two ways to win are buttons in the player's menu.
  The active panel ticks off the once-per-turn actions (Energy, Supporter,
  Stadium, Retreat), which reset on every pass. The starter's first turn says
  no attack and no Supporter. Special Conditions are set in the menu (Asleep,
  Confused and Paralyzed replace each other). On pass a Pokémon Checkup card
  lists Poisoned and Burned damage for the players to place, flips the Burned
  and Asleep coins (heads removes them) and ends Paralyzed for the player whose
  turn ended (`StandaloneGame.pokemonCheckup`). Rules text in `OfficialRules.kt`.
- **Lorcana** (2026-10-16, owner request; JVM tests only, *needs a
  playtest*): phone-hosted like Pokémon. Lore starts at 0 and counts up; the
  setup's number is the lore to win (official 20, editable under house rules),
  and reaching it wins at once. Two or more players: a player who must draw
  from an empty deck taps that in their menu and leaves, and the last player
  left wins. The active panel has a "Card inked" reminder that resets on pass,
  and the starter's first turn says to skip the draw.
- **Cthulhu: Death May Die** (2026-10-17, owner request; JVM tests only,
  *needs a playtest*): phone-hosted, cooperative (`GameProfile.cooperative`).
  There is no fixed turn order (owner): each panel says whether that player
  has taken their turn this round. Tapping **Pass** opens **Pass to?**, listing
  investigators still to go. Selecting one finishes the current turn and gives
  them the next; Cancel or Back leaves the current turn in place. After the last
  turn, every living investigator is offered as the next round's starter,
  including the current player. **Take the next turn** stages one investigator
  still to go as **Up next** without changing the active turn, clock or statistics.
  The current player's Pass then counts their completed turn and hands off
  directly, skipping the chooser. **Cancel next turn** clears the selection;
  selecting another investigator replaces it. The selection survives pause and
  recovery, and clears when consumed, when its investigator leaves or when the
  game ends or returns to setup. Automatic passes also use the staged recipient.
  With nobody staged, Pass opens the chooser. Once every living player has gone, the next round starts (the
  round shows in the menu bar). The table is one upright list of player
  panels under a slim menu bar (owner). The active investigator gets 70% of
  the play area, with the others in a scrollable list below. Their menus retain
  all counters and skill controls. The active card scrolls independently, grows
  with wrapped controls, and puts abilities beside the counters on wide screens.
  Each player can pick their
  investigator in the lobby from every season and box (`domain/DeathMayDie.kt`,
  from the community wiki; any name can be typed) and no two players share one.
  Search matches investigator or known skill names. **Ability** filters six common
  skills; the community index covers 94 roster entries (including the ten full
  core summaries). Unindexed investigators are excluded by a skill filter, with
  coverage stated in the picker. **Random investigator** samples available figures
  within the search/filter, excludes other players' picks and opens a preview;
  **Use investigator** commits it. Every row also opens this preview. Missing
  signature effects remain explicit; a community-guide link supplies references.
  Matching Wounds and Stress counters are reminders that never knock anyone
  out. A tile shows the investigator's
  picture from the community wiki where it has one (Seasons 1 to 4 core
  investigators; a dashboard's portrait is cropped out), and the Game tab
  lists the players the same way.
  Each dashboard saves its sanity loss (0–20) and three base skill levels (1–4), with up to five added skills.
  Sanity loss stops at thresholds 4, 8, 12, 15, 18 and 19; bonus dice appear at
  8, 15, 18 and 19. Thresholds remind the player to resolve their insanity card
  and upgrade a skill; level choices remain manual for companions and corrections.
  Reaching 20 does not automatically eliminate anyone: resolve the printed
  dashboard and game conditions through the player's menu. The ten core-box
  investigators have offline skill-effect summaries checked against their
  dashboards. Indexed investigators also get the known common-skill summaries.
  **Edit abilities** supplies or corrects names and four level effects. It can
  add common presets (including Swiftness) or custom abilities, up to eight total,
  and remove added abilities. New markers start at 1; move them manually for the
  granting effect. Removing an extra preserves the other skills' levels. **Preview upgrades** shows
  future effects without changing a marker. Higher-level effects add to
  earlier effects unless they say instead. Edited ability text survives
  rematches for the original three slots; acquired extras are cleared and markers
  reset on returning to setup or starting a fresh match. **Phases of your turn**
  opens from setup, the active investigator and Game menu without changing state.
  It covers actions, Mythos, Discovery if safe or enemy attacks, and end checks in
  printed order; physical-game exceptions apply. Start stays off (owner,
  2026-10-19) until every player has chosen an **investigator** and has a **Sanity
  condition** assigned; the setup hint names who still needs what
  (`StandaloneGame.setupMissing`). **Use relics** is off by default; when
  enabled in setup, every investigator also needs a **Relic**. Disabled relic
  slots are hidden, and turning the option off clears relic assignments and offers.
  The **Elder One** tab (owner, 2026-10-19; shown only while Death May Die is the
  device game and the Game tab isn't showing Atlas, like Planes for Magic) records the Elder One and episode, chosen
  from `DeathMayDieStory` (community wiki; any name can be typed) in setup or
  during play, and follows the Story board (`domain/ElderOne.kt`, CMON rulebook):
  a **Summoning symbols** count where the third advances the Elder One and empties
  the count; the 8-space **Summoning track** (spaces 1 to 4 green, 5 to 7 red, 8
  last) where entering space 5 summons it; **We disrupted the ritual**, which
  summons it and makes it mortal; then Stage 2, 3 and Final, each with typed
  printed health and a wound count (excess wounds don't carry over). The
  Progression token reaching space 8 ends the game as a loss, and so does an
  investigator lost while a chosen Elder One is still on the track; the
  armed **We killed the Elder One** on the Final stage is a shared win. Moves
  that end the game are armed (tap twice); corrections (back a space, undo
  summon or disruption before wounds) are available. Card effects stay on the
  physical cards. The tab opens on a header naming the Elder One and episode over a
  summoning circle (green on the track, red once summoned, gold when banished),
  then the track as a path with a sliding marker, three Summoning-symbol sigils, a
  stage ladder and a wound ring; text carries every state, and Reduce motion
  stills the animation. The menu bar and Game tab show the track space or stage.
  Choices survive back-to-setup and rematch; progress resets.
  **Draw two, pick one** is a separate setup-only house-rule switch, off by default,
  applying to conditions and enabled relics. Each editor offers **Deal two in app**
  (random without replacement from the current catalogue) or **Enter two physical
  draws** (type both names, choose one and Save). App offers reserve both cards,
  survive cancel/reopen and recovery, and cannot reroll by reopening. Manual and
  catalogue assignment remain available for physical draws/corrections; no card
  text or art coverage is implied by a random deal. Back-to-setup preserves offers;
  rematch or changing investigator clears them. The house-rule switch persists.
  The condition/relic editor offers a searchable community catalogue (35 condition
  names, 20 relics), including known transformed relic names in search. All known
  entries are available without collection filtering; coverage is incomplete and
  **Use custom entry** supports other cards. Choosing an entry edits a draft;
  **Save** assigns it, while Cancel/Back leaves the existing assignment unchanged.
  Names and rules/notes remain editable on the active
  card or in the player menu for corrections. A named condition appears in the insanity
  threshold reminder. Card effects are resolved on the physical game. Both
  slots persist in game recovery and stay with the investigator when seats swap.
  Back-to-setup retains assignments and the relic option. **Set up rematch**
  returns to the lobby with both slots empty and the relic option retained;
  clocks start only after required cards are reassigned and Start is confirmed.
  Names are bounded to 80 characters and rules/notes to 2,000. Choosing a
  different investigator clears their dashboard, including both card slots.
  **View card reference** opens verified community photographs for 18 catalogue
  entries. Images may show paired cards or both relic faces; pinch/drag and
  labeled Zoom in/Zoom out/Fit controls support reading them. References are
  downloaded only when opened, cached on the device and credited to CMON and
  BoardGameGeek. Failed downloads can be retried; missing images never block
  assignment. Other entries link their community source and accept typed notes.
  The catalogue includes original, unverified mechanical reminders for 20 of 35
  conditions and both faces of 10 of 20 relics. Exact card prose and artwork are
  not bundled. Missing/uncertain effects stay blank, labeled for card/scan review.
  **Review condition & relic rules** in setup browses the full catalogue without
  assigning a player. Correct the draft from a physical card (including relic flip
  requirements), check each face independently, then **Save reference**. LocalLibrary
  owns these device-wide references; they survive recovery/rematch and prefill
  future choices. Existing match notes are independent and remain unchanged.
  Match editors also support physical checks; **Fill missing rules from reference**
  fills only blank fields. A content fingerprint binds each check to that card's
  name and face text; edits invalidate the matching check, and blank text cannot
  be checked. Source review never marks a physical check. Checking has no effect
  on start readiness, markers, flips or printed effects. See the complete
  [physical-card review checklist](DEATH_MAY_DIE_CARD_REVIEW.md). Recovery saves the selected catalogue
  ID with each card; a renamed card becomes a custom entry, and unknown or
  wrong-slot references are discarded without losing entered names/notes.
  Everyone wins together ("We killed the Elder One") or loses together ("We
  lost"); an investigator who falls with the Elder One on the board leaves and
  the rest play on, and the game is lost when nobody is left.
- **Game picker** (both lobbies): games are grouped as Magic: The Gathering,
  More card games, Board games and Your own game, each a tile saying what the table counts
  (`GamePicker`, `FORMATS` in `TableSetup.kt`). Atlas's lobby lists only the
  formats Atlas runs. The Magic format's tile is "Standard"; its game modes are
  Free-for-all and Two-Headed Giant, and Star Commander shows only under
  Commander (`GameMode.availableFor`).
- **Guided setup** (owner, 2026-10-19, idea from a playtester; device game only,
  optional, *needs a playtest*): under the game choice, **Guided setup: pass the
  device** goes round the seats. Each player gets a "Pass the device to" screen
  (or Skip), then their own choices: Death May Die investigator, sanity
  condition and enabled relic; other games tile card or commander. Saved
  players also get tile color and photo. A confirm screen lists everyone's choices
  and what is still missing, with Edit per player, **Start the game** (the
  normal Start path, so first-player choice and the setup gate still apply)
  and **Back to the setup menu**, which keeps every choice
  (`ui/tablet/GuidedSetup.kt`, using the lobby's `TableLobbyActions`). It fills
  the screen edge to edge; portraits are cropped toward the top so faces show.
- Custom starting life 0 to 1,000,000 (custom modes or house rules). Any
  seated player may change the next game's settings in the lobby
  (`ConfigureGame`); a running match keeps what it captured at start, and a
  rematch starts fresh.
- **Back to setup and short games** (2026-10-14; host tests only, *needs a
  playtest*): within the first three minutes of game time (`UNDO_START_MS`)
  any seated player can undo the start (`BackToSetup`): the lobby comes back
  with the same players and settings and nothing is recorded. A match under a
  minute (`SHORT_GAME_MS`) commits its finished checkpoint but its statistics
  wait for `CountShortGame` (state `shortGameUncounted`); left uncounted, it
  never reaches the statistics. The standalone app does the same. Android returns
  to the Game tab when this action succeeds, including from full screen.
- Local Android starts and rematches show a cancellable three-second countdown
  before committing the engine action. Back, leaving the screen or backgrounding
  cancels it; clocks start only after completion. Atlas retains its own countdown.
  During local Planechase play, Planar die beside Pass animates one validated
  engine roll. Blank stays on Table; chaos and planeswalk open Planes with the
  corresponding effect highlighted. Cancellation before the roll changes nothing.
- **Who goes first:** any player can choose (`SelectStarter`); choosing the
  chosen player again clears it. With nobody chosen, Atlas picks a random
  player when the countdown ends, as the standalone app does (rematches too).
- Saved in `gamecfg` (format, starting life, turn timer). A corrupt record
  blocks setup rather than being overwritten.
- HTTP: `GET/POST /api/game/settings` (omitted fields keep their value);
  `/api/session/me` and `/api/seats` report `lifeAvailable` and `life`.
- Lifetime statistics are not yet split by game (staged), apart from the
  Star Commander counters below.

## Elimination credit

Standalone app tables support up to ten players. Atlas's standalone importer
accepts ten-player records and a 16-bit winner set.

The app's manual Eliminate action opens a full-screen **Eliminated by?**
question before removing the player. No answer is preselected: choose any
opponent, **Self** (own team in Two-Headed Giant), or **Unknown**. Off-turn and
already-eliminated opponents can receive credit. Self is recorded separately,
awards no opponent credit and never refills the bag. Teammate/self opponent
credit is rejected. A team removal credits two opponents to the chosen player.

App-started games pause automatic removals for the same question; a pending
answer is authoritative match state, saved for recovery. All-out batches and
final eliminations defer victory/completion statistics until every source is
answered. Answering preserves the paused state when the match continues; resume
when ready. Review game closes the question without answering; the Game card
or table menu reopens it and completion still waits. Concessions and bag draws already have known causes and skip the
question. TurnHub never infers a source from the active turn or life-change
sender. An explicit Unknown is a completed answer. Boot/revision guards and a
pending-victim check reject stale and duplicate answers.

Native starts opt into this interactive policy (`askEliminationSource`); Bag of
Doom always enables it. Starts without that policy retain immediate, uncredited
automatic removal. A Sigil's manual removal in an interactive match queues the
same app question when no explicit source was supplied. Manual app choices are
captured before removal and therefore before any completion callback.

Credit is scoped to immutable match player order, independent of controllers or
names. It survives interrupted-match recovery and travels in finished history,
shares and imports. Lifetime and last-game **Opponents eliminated** totals use the
same completion/import path as other statistics, including short-game gating and
import duplicate handling. Atlas retains these totals in core NVS with no SD
card. A win claim does not fabricate eliminations of the remaining players.

## Zero-life elimination and exit reasons

In the Android full-screen table, life taps and holds are confined to the area
above the bottom action strip. A 12dp neutral gap separates the two; missing the
enlarged Pass target within the strip or gap cannot change life. Leaving the life
area cancels its gesture. Pass keeps its 600ms hold and semantic accessibility
action. These input protections do not alter automatic-elimination rules.

`autoEliminateAtZeroLife` is on by default. Any seated player can change it in
Android Game, the connected tablet lobby/table menu, or standalone play. During
a connected match ConfigureGame permits only this field to change: profile,
starting life, teams and turn timer stay fixed. The live change belongs to the
current match; lobby changes choose the next-match default. Changes wait while a
win claim or manual elimination decision is open.

Enabling it immediately checks all living players at zero or below, including
paused games. The engine resolves the full set before choosing a winner; if
no side survives the match is a draw. Life reductions from Commander hits and
accepted or expired life requests use the same check. Active-player elimination
advances the turn and cancels a queued pass; eliminated players cannot be healed
back into the match. Disable the setting before applying a card's alternate
loss condition. Shared team life eliminates both teammates.

Concede records a voluntary exit (`conceded=true`, `eliminated=true`); Eliminate
and automatic zero-life removal record elimination (`conceded=false`). Both
remove the player from turn order and can leave the last side as winner. The
Android controls confirm both actions; table panels and standalone history show
the recorded reason. Concessions increment `gamesConceded` and use the Conceded
last result; eliminations increment `gamesEliminated`. Team exits share a reason.
Saved match recovery preserves the policy and reason. Standalone results carry
the reason through optional Atlas import (`concededN`, 0 or 1).

The settings profile byte uses bit 0x40 for manual-only elimination (clear means
automatic). Checkpoint schema 2 uses the existing eliminated byte as flags:
bit 0 out, bit 1 conceded. Statistics schema 2 uses the former four trailing
padding bytes for `gamesConceded`, preserving the record size. Historical
concessions cannot be reconstructed from older eliminated results.

## Two-Headed Giant

*Planned → implemented 2026-10-06 (owner request); needs a playtest.* A team
option for the Magic and Commander profiles (`GameSettings::twoHeadedGiant`,
`twoHeadedGiant` on `/api/game/settings`, `settings.twoHeadedGiant` and
`players[].team` in the state).

- **Teams** are neighbours in turn order: players 1+2 are team 1, 3+4 team 2,
  and so on. Change teams by changing the lobby's turn order. Start needs an
  even table of at least 4 (`validTeamTable`). Teammates may share one Sigil
  (seats A and B) or use separate Sigils or phones.
- **One turn per team.** `activePlayer` is one teammate; both have the turn
  (`GameEngine::hasTurn`), either may pass, claim the win or cancel the
  team's pending pass, and the pass goes to the next team. A team turn counts
  for both teammates' turn statistics. Both teammates' Sigils show the turn
  and hear its cues.
- **One life total.** Starting life is the team's total: 30 for Magic (fixed,
  with exactly four players, unless house rules are on) and 60 for Commander
  (editable). Either
  teammate changes it directly; a change to the other team's life is a
  request either opponent can answer, one pending per team.
- **Commander damage** stays per player (21 from one commander is the usual
  loss), and every hit comes off the team's life. A teammate is never a source
  (2026-10-14). Two-Headed Giant Commander is a custom mode: its life stays
  editable.
- **Leaving together.** Eliminating or conceding one teammate eliminates the
  team. A win claim waits only on the other teams; the last team standing
  wins, and both teammates get the win in their statistics.
- **First draw.** The starting team skips its first draw; the Atlas screen
  reminds the table during that first turn. TurnHub does not track draws.
- **On a phone without Atlas** (2026-10-13, owner request) the full-screen
  table offers the same mode under **Game mode** in its setup, with the same
  rules: team life 30/60, shared poison (15), one team turn credited to both,
  Commander damage per player off the team's life, and teams leaving together.
  The local history credits both winners (`GameRecord.won`); importing such a
  record into Atlas still credits only the recorded winner. *Needs a playtest.*
- Storage keeps the flag in the profile byte's top bit (`PROFILE_BYTE_TEAMS`)
  of `gamecfg` and the recovery record, so neither layout changed. Sigils need
  no new firmware: Atlas sends the team's life and turn in the existing
  display packets.

## Commander · Bag of Doom

*Implemented; automated regression coverage, physical playtest pending.*
`mtg_bag_of_doom` (`GameProfile::DoomCommander`, Android `MTG_BAG_OF_DOOM`)
uses normal Commander life, damage, counters and last-player-standing victory.
Start with at least five players and no teams; the standalone app supports ten.
The Game mode selector offers it under Commander. Its bag state belongs to
GameEngine on Atlas and StandaloneGame on a phone-hosted table.

- Every living player begins in the bag. An actual negative life change to
  another living player makes the current turn's owner safe, including life
  payments and Commander damage. Own life loss does not. Healing or undoing a
  life entry does not retract immunity: use the explicit bag correction.
- The first check follows four complete turn cycles from the chosen starter.
  Afterward check after each cycle. The living predecessor of the starter is
  initially the boundary; removing that boundary moves it backward to the next
  living predecessor. Passing the boundary pauses play before the next turn.
  Pending life approvals retain the original turn owner and bag cycle; they
  must settle before a draw. A late approval cannot grant immunity in a newer
  cycle. While the check is pending, life edits apply to the just-finished turn.
- Each check uniformly chooses one living name still in the bag, or nobody if
  all are safe. The bag then refills for all survivors, resetting immunity.
  A drawn player is eliminated through the usual removal path, gives no player
  credit, and is marked in local history/sharing/imports. Their name cannot be
  credited later. Each check increments the bag cycle, even an empty check.
- A credited elimination refills the bag except for the credited player and
  moves its boundary to their living predecessor. It starts a one-cycle check,
  including a kill during the initial grace period. Concession, Self and Unknown
  removal just remove the name. The bag panel can credit an earlier automatic
  removal once, before the game ends, using the same statistics attribution.
- With four or fewer survivors, bag checks stop and Commander continues.
  Rematches reset the bag. Alternate win claims wait until the pending check is
  resolved; the table can still explicitly end without a winner.
- Full-screen modal windows on the shared tablet and personal-phone Game view
  open automatically for a due check and its result. They show named In bag /
  Safe entries, corrections, the boundary, the last result and a Draw control.
  They send semantic requests; only the authority selects. Continue resumes
  the paused game. Review life totals closes the window without drawing.
  Closing a preview or result never dismisses the next due check. The tablet
  center badge reopens a pending check directly. Pending source questions take
  priority over the bag check.
  Boot, revision and cycle guards reject stale or duplicate requests.
- Atlas durably checkpoints a private selection before revealing it. Failed or
  uncertain saves keep that selection sealed; retries and reboot recover the
  same name. Life/counter/removal/eligibility edits freeze once sealed. The
  projection exposes only `sealed`, never the unrevealed name. A phone-hosted
  draw commits the entire result to preferences before publishing it; a failed
  write blocks local play without revealing the result.

Rules reference: [Game Knights' ten-player episode](https://www.youtube.com/watch?v=CvyRDmwOeCs)
and [community summary](https://www.reddit.com/r/EDH/comments/1sng1au/commandzones_10_player_game/).
Written summaries differ; the owner explicitly selected **reset immunity after
every check**. This is TurnHub's custom interpretation, not an official Magic
format. No episode imagery, audio, logos or transcript is bundled.

## Star Commander

*Implemented 2026-10-13 (owner request); host and JVM tests only, needs a
playtest.* A five-player Commander variant with its own profile,
`mtg_star_commander` (`GameProfile::StarCommander`, Android
`MTG_STAR_COMMANDER`). Every Commander rule, counter and automatic-elimination
switch applies (`commanderRules()` / `magicRules()` in `game_profile.h`).

- **Exactly five players** (`STAR_PLAYERS`, `validStarTable`); start is refused
  with "Star Commander needs exactly five players." Two-Headed Giant is not
  allowed with it. Life (40), turns, counters and statistics stay individual.
- **Relationships by seat.** For player number p, i = p − 1: allies are
  (i+1)%5 and (i+4)%5, enemies (i+2)%5 and (i+3)%5 (`starAllies`,
  `starEnemies`; Android `domain/StarCommander.kt`). They follow the lobby turn
  order, are fixed once the match starts, and eliminated players keep their
  seat. Names, profiles and Sigils never decide them. Allies are not a team:
  no shared life, turns or automatic win.
- **Ally protection.** Not attacking or targeting allies is a table rule that
  TurnHub only explains. Commander damage between allies is refused by the
  engine (`changeCommanderDamage`, HTTP 409 "Allies can't deal Commander damage
  to each other in Star Commander."), by the standalone engine, and allies are
  left out of every source list (app, tablet, Sigil picker). Life changes are
  never restricted, so blanket effects still hit allies.
- **Victory** (`evaluateStarVictory`, standalone `StandaloneGame`): after every
  elimination batch on any path (manual, concession, zero life, poison,
  Commander damage, a rule switch), each living player whose two enemies are
  out wins; all who qualify after the same batch share the win (at most two,
  always neighbours). An eliminated player never wins, even if their enemies
  are later out (owner review). Last-player-standing does not apply; a batch
  that leaves nobody alive is a draw. Win claims for alternate win conditions
  are unchanged and name one winner.
- **Shared winners** are a winner set (`winnerMask_`), sent as `winnerPlayers`
  next to `winnerPlayer` (the lowest winner) in the state, kept in recovery
  schema 8, and credited to each winner's statistics. A shared win is never a
  draw. Standalone records carry `winners`, and the Atlas import accepts the
  `winners` field. In Two-Headed Giant `winnerPlayers` lists both teammates.
- **Statistics** count Star games separately (`starGamesPlayed`,
  `starGamesWon`, also counted in the totals), in the SD record (schema 4) and
  the NVS core record (schema 2). Older stats records read as corrupt and need
  a factory reset (no migrations before release). Local app statistics show the
  same pair.
- **Clients.** Android setup offers Star Commander under Game mode with a star
  map (pentagon of allies, dashed pentagram of enemies, tap a player to
  highlight their relationships, tap two to swap seats) and a text list of
  allies and enemies. Panels show "Enemies out n/2", the table menu shows the
  live star, and game over names every winner as a Star Commander victory.
  Sigils get no new display text: the display packet has no free field, and
  master's Sigil is headless until LVGL.

## Planechase

*Implemented 2026-10-15 in the Android standalone game (owner request);
JVM tests only, needs a playtest. Atlas, automatic plane effects and the plane
screen are planned ([Planned Designs](PLANNED_DESIGNS.md#planechase-on-atlas-and-the-plane-screen-planned-owner-2026-10-15)).*
The rules are the Comprehensive Rules' Planechase sections (901, 311, 312,
701.31, 704.6f), and the shared scenarios in
[`protocol/scenarios/planechase/`](../../protocol/scenarios/planechase) are
the spec every engine runs.

- **Setup.** Magic formats only (`Planechase` in `StandaloneGame`, the on/off
  switch sits with the other game options on the app's Game tab, which links
  to the deck options in the Planes tab). Planechase can use **Digital deck**
  or **Physical cards**.
  - The digital deck is the single shared planar deck option (901.15). Choose
    All planes, a release collection (including Planechase Anthology), or a
    named custom deck. The chosen pool contributes every selected plane and
    up to two selected phenomena per player when phenomena are on.
  - Browse & build searches names, types and rules text; collection and
    selected-only filters narrow the list. Checkboxes and Add/Remove shown
    build a pool; Preview shows art, rules and Previous/Next card controls.
    Save & use deck keeps up to 20 named presets locally; saving the same name
    replaces that preset. Delete removes only the preset, preserving the
    current copied choice. At least one known plane is required; small decks
    are supported for casual play, without certifying tournament legality.
    Choices are lobby-only. Missing selected cards block start/rematch until
    the library is refreshed or the pool changed; an All planes game can
    still start without downloads using physical cards.
  - With physical cards, the players turn over their own planes and TurnHub
    keeps no deck. A digital game started before any planes were downloaded
    plays the same way until the planes arrive; then the Planes tab offers
    to switch to the digital deck mid-game (`dealtLate`: dealt as at the start,
    keeping this turn's roll costs).
- **Start (901.5).** The deck is shuffled at start (and for a rematch). Cards
  are turned over until a plane comes up, and phenomena go to the bottom.
- **The die (901.3a, 901.9).** Only the active player rolls, or either player
  of the active Two-Headed Giant team (901.12d). The die has one planeswalk
  face, one chaos face and four blank faces.
  - The cost is {0}, then {1} more per roll that player paid for this turn.
  - A roll for a card's effect is free and doesn't count toward the cost.
  - Costs reset whenever the turn changes, including when the active player
    leaves.
  - Main phase and empty stack are the table's to keep.
- **Planeswalk (701.31b).** Every face-up card goes to the bottom, and the
  top card turns face up. Counters on the old plane are cleared.
- **Phenomena (704.6f).** A face-up phenomenon blocks rolls until the table
  taps Done, which planeswalks away.
- **Chaos.** A chaos roll, or Chaos ensues, highlights the plane's chaos
  ability. TurnHub doesn't apply card effects.
- **Card tools.** The planar controller (the active player) can planeswalk,
  make chaos ensue and put the top card on the bottom for cards that say so.
  Any living player can correct the counters on a face-up plane.
- **Pause.** Rolls and deck moves wait while paused; counter corrections
  don't.
- **Persistence.** The whole planar state is saved with the local game (the
  `planechase` object), including the selection snapshot. Saved presets live
  in LocalLibrary (`planarDecks`); deleting or replacing a preset cannot
  change a running deck. Start, rematch and late dealing use the copied pool.
- **Card data.** Names, rules text and `art_crop` art come from Scryfall,
  downloaded on the phone the first time Planechase is on with the digital
  deck, from Game setup or the Planes tab (`ScryfallPlanes`). All paper printings
  are downloaded to merge release membership into one entry per card name;
  refresh preserves cached representative IDs and art so saved choices remain
  valid. Older catalogues remain playable; Refresh library loads collections.
  Failed/incomplete downloads preserve the catalogue. The cards are
  saved in app files, the art in the app cache, and nothing is bundled. Each
  plane credits its artist, and the tab carries the Fan Content Policy
  notice.

The standalone domain owns choices and validates `setPlanarSelection` and
save/delete preset requests; PreferencesStandaloneStore saves game and library
together. Compose renders labeled selectors, checkboxes and text/art previews.
Atlas state and contracts are unchanged. This uses the existing Scryfall client
and caching, with no new dependency; browsing can download uncached preview art.

## Turns

- **Pass** waits `PASS_GRACE_MS` (3 s) so it can be undone: the passer's
  Sigil shows PASSING with a green ring countdown and the click undoes it;
  every other Sigil shows "P<n> PASSING" with an amber countdown
  (`PassPending`); the Atlas screen says "Passing in Ns". Two falling ticks
  play when a pass is queued and two rising ticks when it is undone.
- **Master pass** (Atlas Table screen, hold 2 s) and a Game Master's pass
  skip the grace and are logged as master passes.
- **Rounds:** the Atlas header and the Sigils show the table round, counted
  from each living player's completed turns. The Atlas screen also shows
  each player's total time on their own turns and the match time.
- **Shared Sigils** carry two seats (A and B). Seat B's turn uses an azure
  double pulse on B's half of the ring. Down on the Sigil switches which
  seat it shows (and so whose life Left/Right change) until the turn comes
  round; an eliminated seat never leads over a living one.
- **Pause** stops the turn clock. **End match** (Atlas Table screen, hold 5 s)
  ends a running or paused match as a draw.
- **Win claims** need the other living players to confirm, in Atlas's player
  order; a denial resumes play. **Concede** and Game Master removal eliminate
  through the same flow.

## Turn timer

- One value, `turnTimerMs`: 0 = off, else 15 s to 60 min in whole seconds.
  Presets (Off, 1, 2, 3, 5 min, `TURN_TIMER_PRESETS_MS` in `game_profile.h`)
  are UI shortcuts; a custom value is just as valid.
- The countdown is derived from the existing turn anchor: pause freezes it,
  a new turn restarts it. Phases, derived per call:
  - `NORMAL`; `WARNING` at 10 s or less;
  - `EXPIRED`: **the turn continues**. Atlas never passes, pauses or
    penalizes on expiry;
  - `LONG_TURN`: timer off and the turn reached 5 minutes (a gentle green
    cue, no sound).
- `/api/v1/state` reports `settings.turnTimerMs` and
  `turnTimer {phase, remainingMs}` (clock samples: they change without a
  revision bump). The recovery checkpoint carries the timer.
- Shown as text in the app and as a countdown on the Atlas screen.
  Not on Sigil screens: e-ink refresh doesn't suit a live clock (owner
  decision); try it on a future LCD Sigil, which needs a radio field.
- The Android standalone game (no Atlas) applies the same rules locally
  (`StandaloneGame.turnTimer`, lobby presets only). *Implemented* 2026-10-13;
  device acceptance pending.

## Life

- Own life changes apply at once (`ChangeLife`). A Sigil batches presses and
  sends one total 2 s after the last; holding repeats, and the ring shows the
  pending amount (green clockwise for a gain, red counter-clockwise for a
  loss). A Sigil steps by 100 in games starting at 1000 or more.
- **Another player's life** goes through an approval (`RequestLifeChange`):
  one pending request per recipient; only the recipient answers (Right
  approves, Left denies on a Sigil; buttons on a phone); an unanswered request
  is **accepted after the recipient's approval window**, 15 s by default
  (`LIFE_APPROVAL_MS`) or 30 or 60 s by the recipient's accessibility choice
  (the window is fixed when the request is made), and pause does not stop
  that timer. The delta applies to the current total, rechecked at
  acceptance. Request IDs stop a stale answer approving a newer request. Win
  claims, eliminations, game end, reset and rematch cancel pending requests.
- **Atlas touchscreen Player screen:** tap a chip in a match for -5/-1/+1/+5
  and Concede (asks again). In the lobby the Player screen has Earlier, Later
  and Remove (hold 2 s): Remove takes that seat out (seat A also takes its
  Sigil's seat B).
- **Nudge** (app): while a game runs, a living player who is not
  up can nudge the active player. Their Sigil plays the Nudge cue (if its
  sound is on) and their phone shows "<name> nudged you" and vibrates. One
  nudge per player every 30 s; a Game Master can mute an account's nudges.
- Life is bounded to ±1,000,000 and changes are accepted while running or
  paused, except during a table decision or after elimination. Zero or less
  eliminates when `autoEliminateAtZeroLife` is enabled (default true). Life changes never write flash (only the checkpoint).
- HTTP: `POST /api/control/life` (own life), `/life/request`,
  `/life/respond`, `GET /api/game/counters`.

## Commander damage

- The **receiver** records damage taken from a source player's commander.
  Each source has commander 1 and, with partners, commander 2. Positive
  damage lowers life and negative corrects it; both totals change together.
  Counters are 0 to 1,000,000 and reset each match. 21 damage does not
  eliminate automatically.
- **Partner commanders** (2026-10-06) are off for every player by default.
  A player turns them on for their own shown seat from the Sigil Menu
  (`SetPartner`); damage entered for commander 2 from a phone turns them on
  too, and once commander 2 has dealt damage they stay on. Rematch and reset
  clear them.
- **From a phone:** `POST /api/control/commander` (`ChangeCounter`).
- **From a Sigil** (Menu > Cmd damage, both displays): Atlas runs the entry
  pages (`commander_picker.cpp`, `CommanderFlow`/`CommanderKey`). Choose the
  attacker with Left/Right (the list skips the receiver and eliminated
  players), the commander (only with partners), the amount (hold to repeat),
  then a preview; the click applies. Up goes back; Down cancels. The
  receiver's name and A/B seat stay on every page even if turns move on.
  **Undo hit** reverses the last hit recorded this way for that receiver,
  once; a correction to the same cell invalidates it, and it is gone after a
  reboot. Entry closes after 60 s idle, on disconnect, elimination, a table
  decision or match end. Stale or duplicate keys never reapply a hit.

## Cues: light, sound and text

Every cue is presentation: it never changes game state, and nothing relies on
light or sound alone (see [Accessibility](ACCESSIBILITY.md)).

**Lights.** Atlas picks a semantic state per Sigil (`selectSigilLedState`):
a primary `LedCue` (Unassigned, Joined, Starting, TurnStarted, YourTurn,
Waiting, Paused, ConfirmationNeeded, EliminationSelect, GameOver) plus
`LedOverlay` facets (Starter, Winner, TurnWarning, TimerExpired, LongTurn) and
the seated players' light style. The Sigil draws it on the Jewel ring
(`Sigil/src/sigil_led.cpp`), in step with Atlas's table clock. Timer cues:
warning is a slow red pulse, expired steady red, long turn steady green, over
the active player's blue breathe. Every cadence is at most 2.5 Hz. Reduced
motion and Monochrome-safe styles are in [Accessibility](ACCESSIBILITY.md).

**Sound.** `AudioCue` names events; `AudioCueProfile` maps each to notes.
Sigil buzzers get per-player cues (turn start, joining, decisions waiting on
that Sigil: `ActionRequired`, two short notes, for a pending win confirmation
or life request), muted per player by their accessibility setting. The
**Atlas speaker** plays table-wide cues (countdown, start, turn change, timer
warning and expiry, pause, eliminations, win claim results, game over) so
phone players hear them, through `ATLAS_SPEAKER_MASK` to the board's speaker
(none on master's board-less build; the Gen 1 chime synthesizer is on
`legacy/gen1`), volume Off/Low/Medium/High (Admin, `ConfigureSpeaker`). Timer cues play once per turn.

**Android game sounds.** Local and connected play use the same observer for
confirmed start, pass/turn change, pause, resume, victory and draw, with one
short cue per observed transition. Sound is presentation only: button presses,
pending/cancelled/refused passes, repeated polls, first load, foreground return,
rotation and reconnect catch-up do not produce game-action sounds. Native
SoundPool plays original generated WAVs. Game sounds defaults on at 50%; its
phone-only toggle and volume are available in table setup, the table menu and
My account. Silent/vibrate mode and media volume still apply. Leaving the app
stops a cue; the background turn notification remains a separate OS channel.

**Text.** The app and Atlas screen show every state in words (time
left, warning, time over, long turn, PASSING, pending requests).

## Verification

Star Commander: `starCommander*` and `starStandaloneImport` host scenarios
(relationships, five-player start, ally Commander damage, victory paths, shared
winners, claims, recovery, rematch, two tables, statistics, import) and the
Star cases in Android `StandaloneGameTest`. Not yet played at the table.


Two-Headed Giant: host scenarios cover team turns, shared life, requests,
win claims, elimination, Commander, recovery, the settings API, the state JSON
and both teammates' Sigils (`twoHeadedGiant*` in `scenarios.cpp`). Not yet
played at the table.

Host scenarios cover settings and storage, life bounds and approvals
(authorization, deadline races, rollover, cancellation), Commander entry from
both Sigils (shared seats, partners, preview, undo, stale keys, closure), the
timer phases and cue selection, turn order with shared seats and recovery.
Browser approval smoke was retired with the administration-only portal; native
HTTP/controller tests and host scenarios retain approval coverage. Life, approvals,
the timer and Commander from phones are in use at the table (owner,
2026-10-02). Sigil Commander entry and partners (Sigil 0.9.10, Atlas 0.6.8)
and the shared-seat turn order (2026-10-05) still need a playtest.

## Magic monarch

Magic and Commander games have one optional monarch, identified by individual
player number even in Two-Headed Giant. A living player can choose **I’m the
monarch** from their player controls or table tile drawer, including while
paused. Claiming immediately transfers the crown from the previous holder;
there is no turn restriction. Table tiles show the crown and a Monarch label.
If the holder concedes or is eliminated (including through zero-life resolution
or team removal), the crown passes to the active player, or to the next living
player in turn order when the active player is leaving (Magic CR 724.4; owner
decision 2026-10-12, replacing the earlier clear-on-exit behavior). It clears
only if nobody is left. A pending win claim blocks new claims. Pause and recovery preserve it; a new
match or return to lobby clears it. TurnHub tracks the declaration; players
resolve the card’s combat and draw rules themselves.

Atlas owns this singleton in GameEngine. Authenticated seat controls converge
on ClaimMonarch; the domain rejects other formats, players out of the game,
and inactive matches. The state projection exposes nullable `monarchPlayer`,
and checkpoints preserve it. Android standalone maintains and recovers
the equivalent singleton for its separate local game. No radio change.

## Magic counters, commander tax and statuses

Magic/Commander native player controls and table drawers now expose poison,
energy and experience, initiative, city’s blessing, and (Commander) command-zone
casts. Counters range from 0 to 1,000,000; negative corrections cannot cross zero.
Counters and city’s blessing remain associated with their individual owner.
Initiative has one living holder per game, independent of monarch; claiming it
transfers it, and the holder may release it to correct an entry. A leaving
holder's initiative passes like the monarch (CR 725.4). City’s blessing
is independently declared by each player; clearing it is an explicit correction.
TurnHub does not count permanents, resolve the Undercity or enforce card effects.

Each commander’s casts from the command zone are tracked separately. Its next
cast tax is derived as twice the count; first cast has no tax. Counting a second
commander’s casts enables partners in Atlas. A minus corrects an accidental cast.

Automatic elimination has independent live toggles for zero life, poison and
21 damage received from one commander (not pooled across sources or partners).
New-game defaults are on. Poison defaults to 10, or 15 shared for a Two-Headed
Giant team; its limit is editable. Switching team mode in the lobby selects the
standard poison limit unless the request supplies a custom one. Changing a limit
or enabling a rule immediately checks all living players, as one batch; this
avoids a false winner if multiple players meet loss conditions simultaneously.
Poison/Commander losses use the elimination reason, not concession. The relevant
rule should be disabled before a card changes that loss condition. All these
settings remain reachable during pause.

Connected counters use the existing ChangeCounter intent with CounterKind,
validated by GameEngine and the living-seat/table-decision handler. Settings use
ConfigureGame; the common post-intent rule finalizer resolves automatic losses
before projecting/checkpointing. Settings schema 3 and checkpoint schema 8
persist rules, counters and initiative. The HTTP state is a read model; no
radio change. Native connected clients require Atlas for the new actions while
disconnected (they do not replay status/counter changes). Standalone games own
and persist their equivalent local state. New matches reset counters/status;
match settings carry forward. Elimination clears initiative; city’s blessing
and numerical counters remain as the last values on an eliminated player.

### Commander counter presentation

In the Android player menu, Partner commanders sits beside command-zone casts.
Commander 2 casts is hidden until that switch is enabled; hiding it preserves
its recorded total. This disclosure also exposes partner damage rows and is
retained across closing/reopening the menu. Existing nonzero received partner
damage remains visible for corrections. Cast rows and tax chips use an original
commander-card icon; set/rarity symbols are not extracted for UI artwork.
The named commander's selected printing supplies its rarity colour (common grey,
uncommon silver, rare gold, mythic orange), with theme-specific contrasting
colours. Known rarity is also spoken by accessibility labels. Unknown rarity and
unnamed partner icons use neutral ink. The existing Commander card art preference
controls metadata lookup; old cached art refreshes metadata once and retains its
image, with offline fallback to neutral until rarity is available.
