# Web Portal Design System

How the Atlas web pages look and how their presentation is organized. This is
presentation only: Atlas remains the sole owner of game state, and nothing here
adds an Intent, a validator or persisted state.

Status: **Implemented** in source (2026-09-24). Host browser smoke tests pass.
Appearance on real phones and on Atlas hardware **needs verification**.

## Pages and shared stylesheet

| Route | Source | Notes |
|---|---|---|
| `/portal` | `web_pages.cpp` `PORTAL_HTML` | Game, Players, My Account, Device Settings |
| `/login` | `profile_login_page.cpp` | Sign in / create account |
| `/stats` | `stats_page.cpp` | Personal statistics with a win-rate dial |
| `/dev` | `web_pages.cpp` `DEV_HTML` | Developer diagnostics and serial log download |
| `/update` | `ota_manager.cpp` `UPDATE_HTML` | Admin firmware upload |
| `/theme.css` | `web_pages.cpp` `THEME_CSS` | Shared stylesheet, `Cache-Control: no-cache` |

Every page links `/theme.css` and runs a one-line head script that applies the
saved theme before first paint. Page-specific layout stays inline in its page.

## Themes

A theme is only a set of CSS custom properties (tokens) on `html[data-theme]`.
Components never hard-code colours, so adding a theme means adding one token block.

| Key | Name | Character |
|---|---|---|
| `brass` (default) | Brass | Steampunk: gold on dark walnut, riveted panels, serif display type, brass gauge |
| `midnight` | Midnight | Clean modern dark with gold accents, no ornament |
| `parchment` | Parchment | Light sepia and dark gold for bright rooms |
| `contrast` | High contrast | Black, white and yellow for maximum legibility |

The choice is a per-browser presentation preference stored in `localStorage`
(`turnhubTheme`) and chosen in My Account → Appearance. It is never sent to Atlas
and never changes gameplay. Missing or unknown values fall back to Brass.

## Visual language

- **Brand mark:** an original gear drawn from plain geometry (twelve trapezoid teeth
  and an axle hole), embedded in `theme.css` as a CSS mask so it takes the theme's
  gold. It turns slowly while a game is running.
- **Turn gauge:** the Game stage draws the turn as a 270° pressure gauge. With a turn
  timer, the arc and needle show the remaining fraction. With the timer off, the
  needle sweeps once per elapsed minute. The gauge is decorative (`aria-hidden`):
  the same information is in the heading, the sub-line text and the badges. It
  redraws what `/api/status` reports and never runs its own game clock.
- **Life tiles:** one tile per player, with large numerals and −10/−1/+1/+10 buttons
  (±100/±1000 for Yu-Gi-Oh!). A short +/− marker echoes a change that just happened.
  The total itself is the record.
- **Icons:** a handful of simple stroked glyphs hand-authored in the portal's inline
  SVG sprite.
- **Type:** system font stacks only (sans for body text, an installed serif for
  display on Brass/Parchment). No web fonts are downloaded.
- **Dialogs:** confirmations and PIN/rename prompts use a native `<dialog>` styled to
  the theme, falling back to the browser's own `confirm`/`prompt` where `<dialog>`
  is unsupported.

## Layout

- Phones: single column, with a bottom tab bar in thumb reach and the primary action
  (Join / Pass turn / Resume) as a large button in My seat.
- Wide screens (≥1000 px): the Game view is a main column (stage, life, Commander
  damage) plus a side column (My seat, Table, Game setup). The shell stretches to
  2000 px so table-side displays use their width.
- The sticky header must not use `backdrop-filter` on phones: that would make the
  header the containing block of the fixed bottom tab bar.

## Accessibility

- Every state shown by colour also carries text (badges, player-state labels,
  "Active turn", "Time's up"). Switches show state by knob position as well as colour.
- High contrast theme and `forced-colors` support. `prefers-reduced-motion` disables
  gear rotation, needle easing and other transitions.
- Touch targets are at least 44 px. Focus rings use the theme's focus token. The
  page has a skip link. The active tab carries `aria-current="page"`.
- All checkboxes remain real `<input>` elements with their own `<label>`. Help text
  is linked with `aria-describedby`, so the accessible name stays short.

This is a design target toward WCAG 2.2 AA, not a conformance claim.

## IP and dependencies

No third-party fonts, icon sets, images, CSS frameworks or scripts were added. The
gear, gauge, icons and themes are original to TurnHub, built from CSS/SVG
primitives. The only external code on these pages is still the existing
`qrcode-generator` 1.4.4 (MIT) script, vendored in `Atlas/third_party/qrcode-generator/`
with its license. It predates this redesign and does not yet have a row in
`../legal/DEPENDENCY_TRACKER.md`.

## Test contract

`Atlas/tests/host/portal_smoke.cjs` and `counter_smoke.cjs` drive the portal by
element IDs, accessible names and a few page globals (`refreshAll`, `sessionInfo`,
`gameSettingsData`, `counterData`). Keep those IDs, labels and globals stable when
restyling. Both fixtures serve `/theme.css` from `THEME_CSS`.

## Future: SD-card theme packs (Planned)

Once Atlas gains SD storage, additional theme packs could be served from the card
as extra token blocks, and optionally original fonts or artwork with recorded
provenance. The token contract above is the extension point. The built-in themes
stay in flash so the portal never depends on the card being present.
