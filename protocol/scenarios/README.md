# Shared rule scenarios

Each JSON file sets up a game, applies actions one by one and states what the
game must look like after each one. Every engine that runs these rules runs the
same files. Today that is the Android standalone game
(`PlanechaseScenarioTest`); Atlas's host suite runs them when Atlas gains the
feature. A rule change is made here first, then in each engine.

Randomness is always scripted. The deck is listed in order, top card first,
and every planar die roll names its face.

## Format

```json
{
  "description": "What the scenario proves",
  "rules": ["901.5"],
  "setup": {
    "profile": "mtg",
    "players": 3,
    "twoHeadedGiant": false,
    "starter": 1,
    "planechase": { "source": "digital", "deck": ["plane-a", "phen-x"], "phenomena": ["phen-x"] }
  },
  "steps": [
    { "action": "roll", "player": 1, "face": "blank", "expect": { "nextCost": { "1": 1 } } }
  ]
}
```

- **`setup`:**
  - Players are numbered from 1.
  - `profile` is a `settings.profile` wire value.
  - `starter` is the first player.
  - `planechase.deck` lists the shuffled deck, top card first. The card ids
    named in `phenomena` are phenomena.
  - `source` is `digital` or `physical`.
- **Actions:**
  - `roll` takes `player`, `face` (`blank`, `chaos` or `planeswalk`) and an
    optional `fromEffect`.
  - `pass`, `planeswalk`, `resolvePhenomenon`, `chaosEnsues` and
    `topToBottom` each take `player`.
  - `counter` takes `player`, `card` and `delta`.
  - `eliminate` takes `player`.
  - `pause` takes nothing; it pauses or resumes the game.
- **`expect` (each key optional):**
  - `changed`: default `true`. Use `false` when the engine must refuse the
    action and leave the game as it was.
  - `active`: the active player.
  - `faceUp` and `deck`: card ids, top card first.
  - `nextCost`: the next roll's cost by player.
  - `lastRoll`: `player`, `face` and `cost`.
  - `phenomenonPending`
  - `planeswalks` and `chaos`: running counts.
  - `counters`: by card id.
