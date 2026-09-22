#include "web_pages.h"

namespace TurnHubWeb {

const char PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0d11">
<title>TurnHub</title>
<style>
:root{color-scheme:dark;--bg:#0b0d11;--panel:#141820;--panel2:#1b202a;--panel3:#202632;--line:#2b3240;--text:#f3f5f7;--muted:#9ca6b7;--good:#62d58a;--warn:#efc55a;--bad:#ef6a6a;--blue:#72a7ff}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}body{padding:max(12px,env(safe-area-inset-top)) max(12px,env(safe-area-inset-right)) max(18px,env(safe-area-inset-bottom)) max(12px,env(safe-area-inset-left))}.shell{width:min(2000px,100%);margin:auto}header{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:12px;flex-wrap:wrap}.brand{font-size:1.85rem;font-weight:950;letter-spacing:.02em}.sub,.small{color:var(--muted);font-size:.82rem;line-height:1.45}.right,.actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);box-shadow:0 0 0 4px rgba(239,106,106,.08)}.dot.online{background:var(--good);box-shadow:0 0 0 4px rgba(98,213,138,.08)}a.link,button,input,select{border:1px solid var(--line);background:var(--panel2);color:var(--text);border-radius:10px;padding:9px 12px;font:inherit}a.link{text-decoration:none;font-weight:800}button{font-weight:800;cursor:pointer}button.primary{background:#e7ebf2;color:#111318;border-color:#e7ebf2}button.good{border-color:rgba(98,213,138,.5);color:var(--good)}button.warn{border-color:rgba(239,197,90,.5);color:var(--warn)}button.bad{border-color:rgba(239,106,106,.5);color:var(--bad)}button.blue{border-color:rgba(114,167,255,.5);color:var(--blue)}button:disabled{opacity:.4;cursor:not-allowed}.tabs{display:flex;gap:7px;overflow:auto;padding:5px;margin:0 0 12px;background:var(--panel);border:1px solid var(--line);border-radius:14px;position:sticky;top:max(6px,env(safe-area-inset-top));z-index:20}.tab{flex:1;min-width:92px;border:0;background:transparent;color:var(--muted);padding:10px 13px}.tab.active{background:var(--panel2);color:var(--text);box-shadow:0 0 0 1px var(--line) inset}.view{display:none}.view.active{display:block}.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:12px}.card{grid-column:span 12;background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 12px 34px rgba(0,0,0,.12)}.hero{min-height:245px;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center}.eyebrow,.section{font-size:.78rem;color:var(--muted);font-weight:900;letter-spacing:.12em;text-transform:uppercase}.hero h1{font-size:clamp(2.8rem,10vw,6rem);margin:.2rem 0;line-height:.95;overflow-wrap:anywhere}.hero p{color:var(--muted);font-size:1.05rem;margin:.35rem 0}.badges{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-top:12px}.badge{border:1px solid var(--line);border-radius:999px;padding:6px 10px;color:var(--muted);font-size:.76rem;font-weight:800}.badge.good{color:var(--good);border-color:rgba(98,213,138,.35)}.badge.warn{color:var(--warn);border-color:rgba(239,197,90,.35)}.badge.bad{color:var(--bad);border-color:rgba(239,106,106,.35)}.badge.blue{color:var(--blue);border-color:rgba(114,167,255,.35)}.metrics{display:grid;grid-template-columns:repeat(2,1fr);gap:9px}.metric,.device,.session-box,.player,.seat,.setting-box{background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:13px}.metric label{display:block;color:var(--muted);font-size:.72rem;font-weight:800;text-transform:uppercase}.metric strong{display:block;margin-top:4px;font-size:1.08rem}.players,.devices{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}.player.active,.seat.active{border-color:var(--blue);box-shadow:0 0 0 1px rgba(114,167,255,.2) inset}.player.winner{border-color:var(--good)}.player.eliminated{opacity:.55}.player-name,.device-name,.session-title{font-size:1.08rem;font-weight:900}.player-head,.device-top,.session-head,.setting-head{display:flex;justify-content:space-between;gap:10px;align-items:flex-start}.player-state{font-size:.72rem;font-weight:900;text-transform:uppercase}.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.75rem;color:var(--muted);overflow-wrap:anywhere}.device-state{font-size:.72rem;font-weight:900}.online-text{color:var(--good)}.offline-text{color:var(--bad)}.seats{display:grid;gap:8px;margin-top:10px}.seat{padding:10px}.seat-top{display:flex;justify-content:space-between;gap:8px}.slot{display:inline-flex;width:25px;height:25px;align-items:center;justify-content:center;border-radius:8px;border:1px solid var(--line);font-weight:950;margin-right:6px}.notice{border-left:3px solid var(--warn);background:var(--panel2);padding:12px 13px;border-radius:10px;color:var(--muted);line-height:1.45}.notice.good{border-left-color:var(--good)}.notice.blue{border-left-color:var(--blue)}.profile{display:grid;grid-template-columns:1fr auto;gap:8px;margin-top:10px}.status-row{display:flex;justify-content:space-between;gap:12px;padding:9px 0;border-bottom:1px solid var(--line)}.status-row:last-child{border-bottom:0}.status-row strong{text-align:right}.control-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-top:12px}.control-grid button{min-height:48px}.control-grid .wide{grid-column:1/-1}.settings-grid{display:grid;grid-template-columns:1fr;gap:10px;margin-top:12px}.field{display:grid;gap:7px}.field label{color:var(--muted);font-size:.76rem;font-weight:800;text-transform:uppercase;letter-spacing:.05em}.toggle-row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid var(--line)}.toggle-row:last-child{border-bottom:0}.switch{width:46px;height:26px;position:relative;display:inline-block}.switch input{display:none}.slider{position:absolute;inset:0;background:#303744;border-radius:999px;cursor:pointer}.slider:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.15s}.switch input:checked+.slider{background:#4178d0}.switch input:checked+.slider:before{transform:translateX(20px)}.capabilities{display:flex;gap:6px;flex-wrap:wrap;margin-top:8px}.empty{color:var(--muted);padding:14px;border:1px dashed var(--line);border-radius:12px;text-align:center}.footer-note{color:var(--muted);font-size:.76rem;text-align:center;margin:14px 0 4px}.toast{position:fixed;z-index:80;left:50%;bottom:max(16px,env(safe-area-inset-bottom));transform:translateX(-50%) translateY(18px);width:min(540px,calc(100% - 24px));background:var(--panel3);border:1px solid var(--line);border-radius:14px;padding:12px 14px;box-shadow:0 18px 55px rgba(0,0,0,.45);opacity:0;pointer-events:none;transition:.18s}.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}.toast.bad{border-color:rgba(239,106,106,.55);color:var(--bad)}
@media(min-width:820px){.hero-card{grid-column:span 8}.table-card{grid-column:span 4}.session-card{grid-column:span 6}.game-info-card{grid-column:span 6}.half{grid-column:span 6}.third{grid-column:span 4}.two-third{grid-column:span 8}.settings-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:600px){body{padding-left:10px;padding-right:10px}.card{padding:14px}.hero{min-height:210px}.profile{grid-template-columns:1fr}.control-grid{grid-template-columns:1fr}.tabs{top:4px}.status-row{font-size:.92rem}}
:focus-visible{outline:3px solid #ffda78;outline-offset:3px}@media(prefers-reduced-motion:reduce){*{transition:none!important}}
[hidden]{display:none!important}fieldset{min-width:0}select,input{max-width:100%}
.request-alert{position:sticky;top:72px;z-index:19;margin-bottom:12px;color:var(--text);box-shadow:0 8px 20px #0008}

.hero.hero-card{grid-column:1/-1;min-height:0;flex-direction:row;justify-content:flex-start;gap:12px;padding:12px 18px;text-align:left;flex-wrap:wrap}.hero h1{font-size:clamp(1.5rem,4vw,2.2rem);margin:0;line-height:1.1}.hero p{font-size:.85rem;margin:0}.hero .badges{margin:0 0 0 auto}
.life-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}.life-player{background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:14px;min-width:0}.life-player.active{border-color:var(--blue)}.life-player h3{margin:0;font-size:1rem;overflow-wrap:anywhere}.life-total{font-size:2.3rem;font-weight:900;font-variant-numeric:tabular-nums}.life-buttons{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px}.life-buttons button{padding:10px 4px;min-height:44px}.life-player.eliminated{opacity:.55}summary{cursor:pointer;font-weight:700;padding:10px 0}.qr-grid{display:flex;gap:24px;flex-wrap:wrap}.qr-box svg{display:block;width:192px;height:192px;max-width:100%;background:white}.qr-box a{display:inline-block;color:var(--blue);margin-top:8px}
@media(min-width:820px){.session-card{grid-column:span 4;align-self:start}.life-card{grid-column:span 8}.game-info-card,.table-card{grid-column:span 6}}
</style>
</head>
<body>
<div class="shell">
<header>
  <div><div class="brand">TurnHub</div><div class="sub">Atlas table console</div></div>
  <div class="right"><span id="dot" class="dot"></span><span id="connection" class="sub">Connecting</span><a id="devLink" class="link" href="/dev" hidden>Developer</a></div>
</header>
<nav class="tabs" aria-label="Portal sections">
  <button class="tab active" data-tab="game" onclick="showTab('game')">Game</button>
  <button class="tab" data-tab="players" onclick="showTab('players')">Players</button>
  <button class="tab" data-tab="account" onclick="showTab('account')">My Account</button>
  <button id="deviceSettingsTab" class="tab" data-tab="settings" onclick="showTab('settings')" hidden>Device Settings</button>
</nav>
<section id="lifeRequestAlert" class="notice request-alert" hidden aria-label="Pending life change"><span id="lifeAlertText" role="status" aria-live="polite"></span> <button onclick="reviewLifeRequest()">Review life request</button></section>

<section id="adminSetup" class="notice" hidden><strong>Set up this Atlas</strong><p>Create or sign into your account in My Account. Then hold the physical Atlas master button and select the button below to establish the initial Admin.</p><button onclick="setupAdmin()">Make my account the initial Admin</button></section>
<main id="view-game" class="view active">
<div class="grid">
  <section class="card hero hero-card"><div id="eyebrow" class="eyebrow">TURNHUB</div><h1 id="heroTitle">READY</h1><p id="heroSub">Waiting for Atlas</p><div id="heroBadges" class="badges"></div></section>

  <section class="card session-card"><div class="section">My seat</div><div id="sessionBox" class="session-box" style="margin-top:12px"><div class="session-head"><div><div id="sessionTitle" class="session-title">Not authenticated</div><div id="sessionMeta" class="small">Sign into your profile to join or reconnect.</div></div><button id="logoutButton" class="bad" style="display:none" onclick="logoutSession()">Log out</button></div><div id="sessionState" class="badges" style="justify-content:flex-start"></div><div id="sessionControls" class="control-grid" style="display:none"></div>
<div id="claimHelp" class="notice blue" style="margin-top:12px"><a href="/login">Sign in or create a profile</a> to play from this phone. A physical Sigil is optional.</div></div></section>
<section class="card life-card"><div class="section">Life</div><p id="gameProfileSummary" class="small"></p><p class="small">Your own changes apply immediately. Other players have 15 seconds to respond before Atlas accepts a request.</p><div id="tableLifeTotals" class="life-grid"></div><p id="lifeMessage" role="status" aria-live="polite"></p><div id="lifePanel" style="display:none;margin-top:16px">
<h3>My life: <span id="myLifeTotal" aria-live="polite">—</span></h3>
<details><summary>Custom and preset life changes</summary><fieldset id="lifeFields"><legend>Adjust life</legend>
<label for="lifeTarget">Player whose life changes</label><select id="lifeTarget" onchange="renderCounterControls()"></select>
<p id="lifeTargetHelp" class="small">Your own changes apply immediately.</p>
<div class="control-grid" style="grid-template-columns:repeat(2,minmax(0,1fr))"><button type="button" id="lifeMinusBig" onclick="adjustMyLife(-lifeBigStep)">−5</button><button type="button" id="lifePlusBig" onclick="adjustMyLife(lifeBigStep)">+5</button><button type="button" id="lifeMinus" onclick="adjustMyLife(-lifeStep)">−1</button><button type="button" id="lifePlus" onclick="adjustMyLife(lifeStep)">+1</button></div>
<div id="lifeTens" class="control-grid" style="grid-template-columns:repeat(2,minmax(0,1fr))" hidden><button type="button" aria-label="Subtract 10 life" onclick="adjustMyLife(-10)">−10</button><button type="button" aria-label="Add 10 life" onclick="adjustMyLife(10)">+10</button></div>
<form onsubmit="event.preventDefault();adjustMyLife(Number(lifeDelta.value))" style="margin-top:10px">
<label for="lifeDelta">Custom life change (negative to subtract)</label><div class="profile"><input id="lifeDelta" type="number" step="1" min="-1000000" max="1000000" required><button id="lifeSubmit" type="submit">Apply life change</button></div></form>
</fieldset></details></div>
<section id="lifeRequestsPanel" hidden style="margin-top:16px"><h3>Life change requests</h3><p id="lifeRequestNotice" role="status" aria-live="polite"></p><div id="lifeRequestActions"></div><p id="lifeRequestCountdown" class="small"></p><div id="outgoingLifeRequests" class="small"></div></section></section>

<section id="commanderPanel" class="card" hidden><h2>My received Commander damage</h2>
<p class="small">Record damage from each commander separately. Adding damage also subtracts life. A negative correction restores life. Players decide when to concede.</p>
<div id="commanderTotals"></div>
<form onsubmit="saveCommanderDamage(event)"><fieldset id="commanderFields"><legend>Record or correct damage received</legend>
<div class="settings-grid"><div class="field"><label for="commanderSource">Commander owner</label><select id="commanderSource"></select></div>
<div class="field"><label for="commanderSlot">Commander</label><select id="commanderSlot"><option value="1">Commander 1</option><option value="2">Commander 2</option></select></div>
<div class="field"><label for="commanderDelta">Damage change (negative to correct)</label><input id="commanderDelta" type="number" step="1" min="-1000000" max="1000000" required></div></div>
<button type="submit" style="margin-top:12px">Update damage and life</button></fieldset></form><p id="commanderMessage" role="status" aria-live="polite"></p></section>
<section class="card game-info-card"><div class="section">Game status</div><div style="margin-top:10px"><div class="status-row"><span>Starting player</span><strong id="starterGame">None</strong></div><div class="status-row"><span>Win response</span><strong id="confirmGame">None</strong></div><div class="status-row"><span>Elimination target</span><strong id="eliminationGame">None</strong></div><div class="status-row"><span>Winner</span><strong id="winnerGame">None</strong></div></div></section>
  <section class="card table-card"><div class="section">Table</div><div class="metrics" style="margin-top:12px"><div class="metric"><label>Players</label><strong id="playersMetric">0</strong></div><div class="metric"><label>Sigils</label><strong id="sigilsMetric">0</strong></div><div class="metric"><label>Host</label><strong id="hostMetric">None</strong></div><div class="metric"><label>Starter</label><strong id="starterMetric">None</strong></div></div><div style="margin-top:10px"><div class="status-row"><span>State</span><strong id="stateMetric">—</strong></div><div class="status-row"><span>Active</span><strong id="activeMetricGame">None</strong></div></div></section>
<section class="card"><h2>Game profile and life</h2>
<form onsubmit="saveGameSettings(event)"><fieldset id="gameSettingsFields"><legend>Game setup</legend>
<label for="gameProfileSelect">Game profile</label>
<select id="gameProfileSelect" onchange="chooseGameProfile()"><option value="generic">Generic</option><option value="mtg">Magic: The Gathering</option><option value="mtg_commander">MTG Commander</option><option value="yugioh">Yu-Gi-Oh!</option></select>
<label for="startingLifeInput">Starting life</label><input id="startingLifeInput" type="number" min="0" max="1000000" step="1" required value="40" oninput="gameSettingsDirty=true">
<button type="submit">Save game settings</button></fieldset></form>
<p id="gameSettingsMessage" role="status" aria-live="polite">The table host can change settings in the lobby.</p>
<p class="small">Life and Commander damage do not automatically eliminate a player. Changes to another player's life need their approval; Atlas accepts unanswered requests after 15 seconds.</p></section>
</div>
</main>

<main id="view-players" class="view">
<div class="grid">
  <section class="card"><div class="player-head"><div><div class="section">Players</div><div class="small" style="margin-top:5px">Live table seats, browser links, turn state, and physical Sigil identity.</div></div><button onclick="refreshAll()">Refresh</button></div><div id="players" class="players" style="margin-top:14px"></div></section>
<section class="card"><div class="section">Invite players</div><p class="small">Connect to the table’s Wi-Fi, then scan a code to open TurnHub.</p><div class="qr-grid"><div class="qr-box"><h3>Table portal</h3><div id="portalQr"></div><a id="portalQrLink">Open portal</a></div><div class="qr-box"><h3>Sign in / join</h3><div id="joinQr"></div><a id="joinQrLink">Open sign in</a></div></div></section><section class="card"><h2>Connect a Sigil</h2><p class="small">Sign into your account, then attach a Sigil. Account login and table participation are separate.</p><div id="devices" class="devices"></div></section><section class="card" id="gmPanel" hidden><h2>Game Master</h2><div id="gmAccounts"></div></section></div>
</main>

<main id="view-account" class="view"><div class="grid">  <section class="card half"><div class="section">My account</div><div id="profileSignedOut" class="notice blue" style="margin-top:12px">Sign into your profile to save a name or PIN.</div><div id="profileBox" style="display:none;margin-top:12px"><div class="setting-box"><div class="setting-head"><div><div class="session-title" id="profileSeatTitle">My seat</div><div class="small" id="profileSeatMeta"></div></div></div><label for="profileName">Display name</label><div class="profile"><input id="profileName" maxlength="32" placeholder="Player name"><button onclick="saveName()">Save name</button></div><label for="profilePin">New PIN</label><div class="profile"><input id="profilePin" type="password" inputmode="numeric" maxlength="8" placeholder="New 4-8 digit PIN"><button onclick="savePin()">Set PIN</button></div><form id="profilePolicyForm" style="margin-top:16px" onsubmit="saveProfilePolicy(event)">
<fieldset id="profilePolicyFields"><legend>Physical access and privacy</legend>
<label style="display:block;margin:12px 0"><input id="allowPhysicalWithoutPin" type="checkbox" onchange="profilePolicyDirty=true"> Allow physical use without a PIN</label>
<p class="small">When disabled, sign into this profile in the portal before joining with a Sigil. An existing game continues.</p>
<label style="display:block;margin:12px 0"><input id="hideStatsWithoutAuthentication" type="checkbox" onchange="profilePolicyDirty=true"> Hide stats without authentication</label>
<p class="small">Statistics always accumulate. This choice also applies to future Sigil statistics screens. Signing into this profile enables its connected Sigil's full display.</p>
<button type="submit">Save profile choices</button></fieldset>
<p id="profilePolicyStatus" role="status" aria-live="polite"></p></form>
<button id="clearPinButton" class="bad" style="margin-top:8px;display:none" onclick="clearPin()">Remove PIN</button></div></div></section>
  <section class="card half"><div class="section">Browser feedback</div><div class="setting-box" style="margin-top:12px"><div class="toggle-row"><div><strong>Browser sound</strong><div class="small">Play lightweight feedback tones on this device.</div></div><label class="switch"><input id="soundToggle" type="checkbox" onchange="saveBrowserPrefs()"><span class="slider"></span></label></div><div class="toggle-row"><div><strong>Vibration</strong><div class="small">Use haptics when supported by the browser.</div></div><label class="switch"><input id="vibrationToggle" type="checkbox" onchange="saveBrowserPrefs()"><span class="slider"></span></label></div><div class="field" style="margin-top:12px"><label for="volumeSelect">Feedback volume</label><select id="volumeSelect" onchange="saveBrowserPrefs()"><option value="low">Low</option><option value="medium">Medium</option><option value="high">High</option></select></div><button class="blue" style="margin-top:12px" onclick="playFeedback('test',true)">Test feedback</button></div></section>
<section class="card"><a class="link" href="/login">Sign in / create account</a> <a class="link" href="/stats">My statistics</a><h3>Private moderation history</h3><div id="myModeration">Sign in to view your counts.</div></section></div></main>
<main id="view-settings" class="view">
<div class="grid">

  <section class="card half"><div class="section">Atlas Wi-Fi security</div><div class="setting-box" style="margin-top:12px"><div class="status-row"><span>Network</span><strong id="wifiSsidSetting">—</strong></div><div class="status-row"><span>Security</span><strong id="wifiSecuritySetting">—</strong></div><div class="status-row"><span>Connected clients</span><strong id="wifiClientsSetting">—</strong></div><div class="status-row"><span>Password</span><strong id="wifiPasswordState">—</strong></div><div class="field" style="margin-top:12px"><label for="networkPasswordInput">New Wi-Fi password</label><input id="networkPasswordInput" type="password" minlength="8" maxlength="63" autocomplete="new-password" placeholder="8-63 characters"></div><div class="actions" style="margin-top:10px"><button onclick="generateNetworkPassword()">Generate</button><button id="wifiRevealButton" onclick="toggleNetworkPassword()">Show</button><button class="warn" onclick="saveNetworkPassword()">Save & restart Atlas</button></div><div id="networkRestartNotice" class="notice" style="margin-top:12px">Hold the physical Atlas master button while saving. Changing the password restarts Atlas and disconnects every Wi-Fi client until it reconnects with the new password.</div></div></section>
<section class="card"><h2>Device names</h2><p class="small">Hold the Atlas master button while renaming a device.</p><div id="atlasDevice"></div><div id="deviceSettingsList" class="devices"></div></section><section class="card"><h2>Account permissions</h2><p class="small">Permissions can be combined. The initial Admin must remain an Admin. Privileged accounts require a PIN.</p><div id="adminAccounts"></div><button onclick="refreshAccountList()">Refresh accounts</button></section>


  <section class="card half"><div class="section">Atlas</div><div style="margin-top:10px"><div class="status-row"><span>State</span><strong id="stateMetricSystem">—</strong></div><div class="status-row"><span>Firmware</span><strong id="firmwareMetric">—</strong></div><div class="status-row"><span>Build</span><strong id="buildMetric">—</strong></div><div class="status-row"><span>ESP-NOW</span><strong id="espMetric">—</strong></div><div class="status-row"><span>Master button</span><strong id="masterMetric">Released</strong></div><div class="status-row"><span>Atlas OTA</span><strong id="otaMetric">Locked</strong></div><div class="status-row"><span>Active player</span><strong id="activeMetric">None</strong></div><div class="status-row"><span>Winner</span><strong id="winnerMetric">None</strong></div></div></section>
  <section class="card half"><div class="section">Portal & network</div><div class="setting-box" style="margin-top:12px"><div class="status-row"><span>Address</span><strong id="portalAddress" class="mono"></strong></div><div class="status-row"><span>Wi-Fi</span><strong id="wifiSsidSystem">—</strong></div><div class="status-row"><span>Security</span><strong id="wifiSecuritySystem">—</strong></div><div class="status-row"><span>Wi-Fi clients</span><strong id="wifiClientsSystem">—</strong></div><div class="status-row"><span>Browser seat</span><strong id="browserSeatMetric">Not signed in</strong></div><div class="status-row"><span>Connection</span><strong id="connectionMetric">Connecting</strong></div><div class="actions" style="margin-top:12px"><button onclick="refreshAll()">Refresh now</button><a class="link" href="/update">Atlas firmware</a></div></div></section>

</div>
</main>

<div id="toast" class="toast" role="status" aria-live="polite"></div>
<div class="footer-note">TurnHub runs locally on Atlas. No cloud connection is required for table control.</div>
</div>
<script src="/portal-qr.js"></script>
<script>
let refreshInFlight=null,gameSettingsData=null,gameSettingsDirty=false,gameSettingsSaving=false,lifeBusy=false,lifeStep=1,lifeBigStep=5;
let profilePolicyDirty=false,profilePolicyOwner=null;
let counterData=null,counterBusy=false,counterOwner=null;
let statusData=null,deviceData={devices:[]},seatData={seats:[]},networkData=null,sessionInfo=null,pendingClaim=null,pendingTimer=null;
let sessionToken=localStorage.getItem('turnhubSessionToken')||'';
let lastObservedState=null,lastObservedActive=0,lastObservedConfirm=0,toastTimer=null;
let browserPrefs={sound:true,vibration:true,volume:'medium'};
try{browserPrefs=Object.assign(browserPrefs,JSON.parse(localStorage.getItem('turnhubBrowserPrefs')||'{}'))}catch(_){}

function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function badge(t,c=''){return `<span class="badge ${c}">${esc(t)}</span>`}
function authHeaders(){return sessionToken?{'X-TurnHub-Token':sessionToken}:{}}
function seatByPlayer(n){return seatData.seats.find(s=>Number(s.player)===Number(n))}
function deviceById(id){return (deviceData&&deviceData.devices||[]).find(d=>Number(d.id)===Number(id))||null}
function sigilLabel(id){if(Number(id)>=8&&Number(id)<24)return 'Phone controller';const d=deviceById(id);return d&&d.label?d.label:`Sigil ${Number(id)+1}`}
function playerLabel(n){if(!Number(n))return 'None';const s=seatByPlayer(n);return s&&s.name?s.name:`Player ${n}`}
function sameSessionSeat(s){return !!(sessionInfo&&sessionInfo.authenticated&&Number(sessionInfo.module)===Number(s.module)&&Number(sessionInfo.slot)===Number(s.slot))}
function showToast(msg,bad=false){const e=document.getElementById('toast');e.textContent=msg;e.className='toast show'+(bad?' bad':'');if(toastTimer)clearTimeout(toastTimer);toastTimer=setTimeout(()=>e.className='toast',3500)}
function originalShowTab(name){document.querySelectorAll('.view').forEach(v=>v.classList.toggle('active',v.id==='view-'+name));document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('active',b.dataset.tab===name));localStorage.setItem('turnhubPortalTab',name)}

function loadBrowserPrefs(){soundToggle.checked=!!browserPrefs.sound;vibrationToggle.checked=!!browserPrefs.vibration;volumeSelect.value=browserPrefs.volume||'medium'}
function saveBrowserPrefs(){browserPrefs={sound:soundToggle.checked,vibration:vibrationToggle.checked,volume:volumeSelect.value};localStorage.setItem('turnhubBrowserPrefs',JSON.stringify(browserPrefs));showToast('Browser feedback settings saved.')}
function playFeedback(kind,force=false){if((browserPrefs.vibration||force)&&navigator.vibrate){const p=kind==='turn'?[70,35,70]:kind==='alert'?[100,50,100]:kind==='gameover'?[130,60,130]:[55];try{navigator.vibrate(p)}catch(_){}}
  if(!browserPrefs.sound&&!force)return;try{const Ctx=window.AudioContext||window.webkitAudioContext;if(!Ctx)return;const ctx=window.turnhubAudioContext||(window.turnhubAudioContext=new Ctx());if(ctx.state==='suspended')ctx.resume();const gain=ctx.createGain();gain.gain.value=({low:.025,medium:.055,high:.1})[browserPrefs.volume]||.055;gain.connect(ctx.destination);let tones=[620];if(kind==='turn')tones=[700,900];else if(kind==='pause')tones=[440,330];else if(kind==='resume')tones=[440,660];else if(kind==='alert')tones=[760,760];else if(kind==='gameover')tones=[620,780,980];else if(kind==='test')tones=[620,820];tones.forEach((hz,i)=>{const o=ctx.createOscillator();o.type='sine';o.frequency.value=hz;o.connect(gain);const t=ctx.currentTime+i*.105;o.start(t);o.stop(t+.075)})}catch(_){}
}
function processBrowserFeedback(s){const me=sessionInfo&&sessionInfo.authenticated?Number(sessionInfo.player):0;if(lastObservedState!==null&&me){if(Number(s.active)===me&&Number(lastObservedActive)!==me)playFeedback('turn');if(Number(s.winConfirm)===me&&Number(lastObservedConfirm)!==me)playFeedback('alert');if(s.state!==lastObservedState){if(s.state==='PAUSED')playFeedback('pause');else if(s.state==='RUNNING'&&lastObservedState==='PAUSED')playFeedback('resume');else if(s.state==='GAME_OVER')playFeedback('gameover')}}lastObservedState=s.state;lastObservedActive=Number(s.active)||0;lastObservedConfirm=Number(s.winConfirm)||0}

function renderStatus(s){statusData=s;dot.className='dot online';connection.textContent='Connected';connectionMetric.textContent='Connected';playersMetric.textContent=s.players;sigilsMetric.textContent=s.sigils;hostMetric.textContent=Number(s.host)>=0?sigilLabel(s.host):'None';starterMetric.textContent=playerLabel(s.starter);stateMetric.textContent=s.state;stateMetricSystem.textContent=s.state;firmwareMetric.textContent='v'+s.firmware;buildMetric.textContent=s.build||'—';espMetric.textContent=s.espNow?'Ready':'Error';activeMetric.textContent=playerLabel(s.active);activeMetricGame.textContent=playerLabel(s.active);winnerMetric.textContent=playerLabel(s.winner);masterMetric.textContent=s.masterButton?'Pressed':'Released';otaMetric.textContent=s.otaStateAllowed?'State ready':'Game locked';starterGame.textContent=playerLabel(s.starter);confirmGame.textContent=playerLabel(s.winConfirm);eliminationGame.textContent=playerLabel(s.eliminationTarget);winnerGame.textContent=playerLabel(s.winner);
  let title='READY',sub='Sign in and join from your phone, or press Action on a Sigil',eye=s.state,bs='';if(s.state==='LOBBY'){title=s.players?`${s.players} PLAYER${Number(s.players)===1?'':'S'}`:'READY';sub=s.starter?`${playerLabel(s.starter)} goes first`:'Sign in and join from your phone, or press Action on a Sigil';if(Number(s.host)>=0)bs+=badge(`${sigilLabel(s.host)} host`,'good');if(s.starter)bs+=badge(`${playerLabel(s.starter)} starter`,'blue')}else if(s.state==='STARTING'){title='3 · 2 · 1';sub=`${playerLabel(s.starter)} starts`;bs+=badge('Countdown','blue')}else if(s.state==='RUNNING'){eye='ACTIVE TURN';title=s.active?playerLabel(s.active).toUpperCase():'RUNNING';sub='Table is live';bs+=badge('Running','good')}else if(s.state==='PAUSED'){title=s.winConfirm?'RESPOND':'PAUSED';sub=s.winConfirm?`${playerLabel(s.winConfirm)} must confirm or deny`:s.eliminationTarget?`${playerLabel(s.eliminationTarget)} selected`:'Game paused';bs+=badge('Paused','warn')}else if(s.state==='GAME_OVER'){title=s.winner?playerLabel(s.winner).toUpperCase():'COMPLETE';sub=s.winner?'Winner confirmed':'Game complete';bs+=badge('Game over','good')}eyebrow.textContent=eye;heroTitle.textContent=title;heroSub.textContent=sub;heroBadges.innerHTML=bs;renderPlayers();renderSession();processBrowserFeedback(s)}

function playerStateText(s){if(s.eliminated)return 'Eliminated';if(statusData&&Number(statusData.winner)===Number(s.player))return 'Winner';if(s.active)return 'Active turn';if(statusData&&Number(statusData.winConfirm)===Number(s.player))return 'Response needed';return 'At table'}
function renderPlayers(){const cards=seatData.seats.map(s=>{let cls='player';if(s.active)cls+=' active';if(statusData&&Number(statusData.winner)===Number(s.player))cls+=' winner';if(s.eliminated)cls+=' eliminated';const current=sameSessionSeat(s);let controls='';if(s.virtual&&!current)controls=`<a class="link" href="/login?profile=${encodeURIComponent(s.profileId)}">Sign into this profile</a>`;else if(current)controls='<button disabled>Current browser seat</button>';else{controls+=`<button onclick="claimSeat(${s.module},${s.slot})">Use this seat</button>`;if(s.hasPin&&!s.virtual)controls+=`<button class="blue" onclick="pinLogin(${s.module},${s.slot})">PIN login</button>`}let flags='';if(s.active)flags+=badge('Active','blue');if(s.sessionClaimed)flags+=badge('Browser linked','good');if(s.hasPin)flags+=badge('PIN');if(s.eliminated)flags+=badge('Out','bad');return `<div class="${cls}"><div class="player-head"><div><div class="player-name">${esc(s.name||`Player ${s.player}`)}</div><div class="small">Player ${s.player} · ${esc(sigilLabel(s.module))} · Seat ${s.slotName}</div></div><span class="player-state ${s.active?'online-text':''}">${esc(playerStateText(s))}</span></div><div class="badges" style="justify-content:flex-start">${flags}</div><div class="actions" style="margin-top:12px">${controls}</div></div>`});players.innerHTML=cards.length?cards.join(''):'<div class="empty">No players have joined yet.</div>'}
function seatsForModule(id){return seatData.seats.filter(s=>Number(s.module)===Number(id))}
function capabilityHtml(x){const caps=[];if(Number(x.capabilities)&1)caps.push(badge('Display','good'));if(!x.metadata)caps.push(badge('Legacy metadata','warn'));if(x.customName)caps.push(badge('Custom name','blue'));return caps.join('')}
function renderDevices(d){
 deviceData=d;atlasDevice.className='notice good';
 atlasDevice.innerHTML=`<strong>Atlas</strong> · ${esc(d.atlas.hardwareId)} · firmware ${esc(d.atlas.firmware)}`;
 devices.innerHTML=d.devices.length?d.devices.map(x=>{
  const state=x.online?'<span class="device-state online-text">ONLINE</span>':'<span class="device-state offline-text">OFFLINE</span>';
  const fw=x.metadata?`Firmware ${esc(x.firmware)}`:'Firmware metadata unavailable';
  const seats=seatsForModule(x.id);
  let seatHtml=seats.length?seats.map(s=>{
   const current=sameSessionSeat(s);let controls=current?'<button disabled>Current seat</button>':`<button ${x.online?'':'disabled'} onclick="claimSeat(${s.module},${s.slot})">Use ${s.slotName}</button>`;
   if(s.hasPin&&!current)controls+=`<button class="blue" onclick="pinLogin(${s.module},${s.slot})">PIN login</button>`;
   return `<div class="seat ${s.active?'active':''}"><div class="seat-top"><div><span class="slot">${s.slotName}</span><strong>${esc(s.name||`Player ${s.player}`)}</strong></div><span class="small">P${s.player}</span></div><div class="actions" style="margin-top:8px">${controls}</div></div>`
  }).join(''):'<div class="small">No seats have joined yet.</div>';
  const attach=sessionInfo&&sessionInfo.authenticated&&statusData&&statusData.state==='LOBBY'?`<button ${x.online?'':'disabled'} onclick="claimSeat(${x.id},1)">Attach seat A to my profile</button>`:'';
  const signIn=!sessionInfo||!sessionInfo.authenticated?'<a class="link" href="/login">Sign in to attach a Sigil</a>':'';

  const defaultLine=x.customName?`<div class="small">${esc(x.defaultLabel)}</div>`:'';
  return `<div class="device"><div class="device-top"><div><div class="device-name">${esc(x.label)}</div>${defaultLine}<div class="mono">${esc(x.hardwareId)}</div></div>${state}</div><div class="small" style="margin-top:8px">${fw}<br>Last seen ${Math.round(Number(x.ageMs)/100)/10}s ago · ${Number(x.sessionCount)} browser session${Number(x.sessionCount)===1?'':'s'}</div><div class="capabilities">${capabilityHtml(x)}</div><div class="actions" style="margin-top:10px">${attach}${signIn}</div><div class="small" style="margin-top:8px">Both seats clear on restart and at game end. Rematch restores the same players.</div><div class="seats">${seatHtml}</div></div>`
 }).join(''):'<div class="empty">No Sigils discovered yet.</div>'
}

function renderNetwork(n){networkData=n;wifiSsidSetting.textContent=n.ssid||'—';wifiSecuritySetting.textContent=n.security||'—';wifiClientsSetting.textContent=String(n.stations??'—');wifiPasswordState.textContent=n.passwordConfigured?`${n.passwordLength} characters`:'Not configured';wifiSsidSystem.textContent=n.ssid||'—';wifiSecuritySystem.textContent=n.security||'—';wifiClientsSystem.textContent=String(n.stations??'—')}

function renderSession(){
 const authed=!!(sessionInfo&&sessionInfo.authenticated);
 if(!authed){gameSettingsDirty=false;lifePanel.style.display='none';profilePolicyOwner=null;profilePolicyDirty=false;sessionTitle.textContent='Not signed in';sessionMeta.textContent='Sign into your profile to join or reconnect.';sessionState.innerHTML='';sessionControls.style.display='none';logoutButton.style.display='none';claimHelp.style.display='block';profileBox.style.display='none';profileSignedOut.style.display='block';browserSeatMetric.textContent='Not signed in';return}
 const policyOwnerChanged=profilePolicyOwner!==sessionInfo.profileId;
 if(policyOwnerChanged){profilePolicyDirty=false;profilePolicyOwner=sessionInfo.profileId}
 profilePolicyFields.disabled=!sessionInfo.policyAvailable;
 if(!profilePolicyDirty&&sessionInfo.policyAvailable){
   allowPhysicalWithoutPin.checked=!!sessionInfo.allowPhysicalWithoutPin;
   hideStatsWithoutAuthentication.checked=!!sessionInfo.hideStatsWithoutAuthentication;
 }
 if(!sessionInfo.policyAvailable)profilePolicyStatus.textContent='Profile settings are unavailable. Reload before trying again.';
 else if(policyOwnerChanged)profilePolicyStatus.textContent='';
 const me=sessionInfo, joined=!!me.participating, name=me.name||'Unnamed profile', state=statusData||{};
 sessionTitle.textContent=name;sessionMeta.textContent=joined?`Player ${me.player} · ${me.virtual?'Phone play':sigilLabel(me.module)+' + phone'}`:'Signed in · not at the table';
 browserSeatMetric.textContent=joined?`${name} · Player ${me.player}`:name+' · signed in';logoutButton.style.display='inline-block';claimHelp.style.display='none';profileBox.style.display='block';profileSignedOut.style.display='none';
 profileSeatTitle.textContent=name;profileSeatMeta.textContent=`Profile ${me.profileId}`;if(document.activeElement!==profileName)profileName.value=me.name||'';
 clearPinButton.style.display=me.hasPin&&joined&&!me.virtual?'inline-block':'none';
 sessionState.innerHTML=(me.host?badge('Table host','good'):'')+(me.active?badge('Your turn','blue'):'')+(me.eliminated?badge('Eliminated','bad'):'');
 const buttons=[];
 if(!joined){buttons.push(`<button class="primary wide" ${state.state==='LOBBY'?'':'disabled'} onclick="participation('join')">Join table</button>`)}
 else if(state.state==='LOBBY'){
  buttons.push(`<button onclick="sendControl('starter')">I go first</button>`);
  if(me.host)buttons.push(`<button class="primary" ${Number(state.players)>=2?'':'disabled'} onclick="sendControl('start')">Start game</button>`);
  buttons.push(`<button onclick="participation('leave')">Leave table</button>`);
  if(me.host)buttons.push(`<button class="bad" onclick="resetTable()">Reset table</button>`);
 }else if(state.state==='STARTING'){buttons.push(`<button onclick="sendControl('cancel-start')">Cancel countdown</button>`)}
 else if(state.state==='RUNNING'&&!me.eliminated){
  buttons.push(`<button class="primary wide" ${me.active?'':'disabled'} onclick="sendControl('pass')">${Number(state.passPending)===Number(me.player)?'Cancel pending pass':'Pass turn'}</button>`);
  buttons.push(`<button onclick="sendControl('pause')">Pause</button>`);
  buttons.push(`<button ${me.active?'':'disabled'} onclick="sendControl('win')">Claim win</button>`);
  buttons.push(`<button class="bad" onclick="concede()">Concede</button>`);
 }else if(state.state==='PAUSED'&&!me.eliminated){
  if(Number(state.winConfirm)===Number(me.player)){buttons.push(`<button onclick="sendControl('confirm')">Confirm win</button><button onclick="sendControl('deny')">Deny claim</button>`)}
  else if(!state.winConfirm&&!state.eliminationTarget){buttons.push(`<button onclick="sendControl('pause')">Resume game</button>`);if(me.active)buttons.push(`<button onclick="sendControl('win')">Claim win</button>`);buttons.push(`<button class="bad" onclick="concede()">Concede</button>`)}
 }else if(state.state==='GAME_OVER'&&me.host){buttons.push(`<button class="primary" onclick="sendControl('rematch')">Rematch</button><button onclick="resetTable()">Reset table</button>`)}
 buttons.push(`<button class="blue wide" onclick="location.href='/stats'">My statistics</button>`);
 const html=buttons.join('');if(sessionControls.innerHTML!==html){const action=document.activeElement&&document.activeElement.getAttribute('onclick');sessionControls.innerHTML=html;if(action){const replacement=[...sessionControls.querySelectorAll('button')].find(b=>b.getAttribute('onclick')===action);if(replacement)replacement.focus()}}
 sessionControls.style.display='grid';
}
function resetTable(){if(confirm('Reset the table and return everyone to the lobby? Profiles and saved statistics are kept.'))sendControl('reset')}
async function participation(action){try{const r=await fetch('/api/session/'+action,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Request failed');showToast(d.message||'Updated');await refreshAll()}catch(e){showToast(e.message,true)}}

async function refreshSession(){if(!sessionToken){sessionInfo=null;renderSession();return}const r=await fetch('/api/session/me',{headers:authHeaders(),cache:'no-store'});if(r.status===401){sessionToken='';sessionInfo=null;localStorage.removeItem('turnhubSessionToken')}else if(!r.ok){throw new Error('Session unavailable')}else{sessionInfo=await r.json()}renderSession()}

async function claimSeat(module,slot){const label=sigilLabel(module);showToast(`Waiting for ${label} ${slot===1?'A':'B'} authorization...`);try{const r=await fetch(`/api/session/request?module=${module}&slot=${slot}`,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Claim failed');pendingClaim=d.requestId;if(pendingTimer)clearInterval(pendingTimer);pendingTimer=setInterval(pollClaim,450);showToast(`Press Action on ${label} within 30 seconds for seat ${slot===1?'A':'B'}.`)}catch(e){showToast(e.message,true)}}
async function pollClaim(){if(!pendingClaim)return;try{const r=await fetch(`/api/session/poll?id=${encodeURIComponent(pendingClaim)}`,{cache:'no-store'});const d=await r.json();if(!r.ok){clearInterval(pendingTimer);pendingClaim=null;showToast(d.error||'Authorization expired.',true);return}if(d.status==='approved'){clearInterval(pendingTimer);pendingClaim=null;sessionToken=d.token;localStorage.setItem('turnhubSessionToken',sessionToken);playFeedback('test');showToast('Seat authorized.');await refreshAll()}}catch(_){}}
async function pinLogin(module,slot){const pin=prompt(`PIN for ${sigilLabel(module)} seat ${slot===1?'A':'B'}:`);if(pin===null)return;try{const r=await fetch('/api/session/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({module,slot,pin})});const d=await r.json();if(!r.ok)throw new Error(d.error||'Login failed');sessionToken=d.token;localStorage.setItem('turnhubSessionToken',sessionToken);playFeedback('test');showToast('PIN accepted.');await refreshAll()}catch(e){showToast(e.message,true)}}
async function logoutSession(){try{await fetch('/api/session/logout',{method:'POST',headers:authHeaders()})}catch(_){}sessionToken='';sessionInfo=null;localStorage.removeItem('turnhubSessionToken');showToast('Logged out.');await refreshAll()}
async function sendControl(name){try{const r=await fetch('/api/control/'+name,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Control rejected');playFeedback('test');showToast(d.message||'Control accepted');setTimeout(refreshAll,100)}catch(e){showToast(e.message,true)}}
function concede(){if(confirm('Concede this player from the current game?'))sendControl('concede')}
async function saveProfilePolicy(event){
 event.preventDefault();
 const choices={allowPhysicalWithoutPin:allowPhysicalWithoutPin.checked?'1':'0',hideStatsWithoutAuthentication:hideStatsWithoutAuthentication.checked?'1':'0'};
 profilePolicyFields.disabled=true;
 try{
  const r=await fetch('/api/session/policy',{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(choices)});
  const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save profile choices');
  profilePolicyDirty=false;profilePolicyStatus.textContent='Profile choices saved on Atlas.';await refreshAll();
 }catch(e){profilePolicyStatus.textContent=e.message;showToast(e.message,true)}
 finally{profilePolicyFields.disabled=!(sessionInfo&&sessionInfo.policyAvailable)}
}
async function saveName(){const name=profileName.value.trim();try{const r=await fetch('/api/session/profile?name='+encodeURIComponent(name),{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save name');showToast('Player name saved.');await refreshAll()}catch(e){showToast(e.message,true)}}
async function savePin(){const pin=profilePin.value.trim();if(!/^\d{4,8}$/.test(pin)){showToast('PIN must be 4 to 8 digits.',true);return}try{const r=await fetch('/api/session/profile',{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({pin})});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not set PIN');profilePin.value='';showToast('PIN saved.');await refreshAll()}catch(e){showToast(e.message,true)}}
async function clearPin(){if(!confirm('Remove the saved PIN for this profile?'))return;try{const r=await fetch('/api/session/profile?clearPin=1',{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not remove PIN');showToast('PIN removed.');await refreshAll()}catch(e){showToast(e.message,true)}}

async function renameSigil(id){const d=deviceById(id);if(!d)return;const next=prompt(`Custom name for ${d.defaultLabel || ('Sigil '+(Number(id)+1))}:`,d.customName||'');if(next===null)return;await saveSigilName(id,next.trim())}
async function clearSigilName(id){const d=deviceById(id);if(!d)return;if(!confirm(`Clear the custom name “${d.label}”?`))return;await saveSigilName(id,'')}
async function saveSigilName(id,name){try{const r=await fetch(`/api/device/name?module=${Number(id)}&name=${encodeURIComponent(name)}`,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save Sigil name');showToast(name?`Sigil renamed to ${d.label}.`:'Custom Sigil name cleared.');await refreshAll()}catch(e){showToast(e.message,true)}}
async function setSeatPersistence(id,remember){try{const r=await fetch(`/api/device/persistence?module=${Number(id)}&slot=1&remember=${remember?'1':'0'}`,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save persistence choice');showToast(remember?'Seat A profile will persist across reconnects.':'Seat A profile will clear on reconnect.');await refreshAll()}catch(e){showToast(e.message,true);await refreshAll()}}

function generateNetworkPassword(){const chars='ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789';let out='';if(window.crypto&&crypto.getRandomValues){const a=new Uint32Array(16);crypto.getRandomValues(a);for(const n of a)out+=chars[n%chars.length]}else{for(let i=0;i<16;i++)out+=chars[Math.floor(Math.random()*chars.length)]}networkPasswordInput.value=out;networkPasswordInput.type='text';wifiRevealButton.textContent='Hide'}
function toggleNetworkPassword(){const show=networkPasswordInput.type==='password';networkPasswordInput.type=show?'text':'password';wifiRevealButton.textContent=show?'Hide':'Show'}
async function saveNetworkPassword(){const password=networkPasswordInput.value;if(password.length<8||password.length>63){showToast('Wi-Fi password must be 8 to 63 characters.',true);return}if(!confirm('Change the Atlas Wi-Fi password? Atlas will restart and every connected device will be disconnected.'))return;networkRestartNotice.className='notice';networkRestartNotice.textContent='Saving network password. Keep holding the Atlas master button...';try{const r=await fetch('/api/network/password?password='+encodeURIComponent(password),{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not update Wi-Fi password');if(!d.changed){networkRestartNotice.className='notice good';networkRestartNotice.textContent=d.message||'Password already matches.';showToast(d.message||'No network change needed.');return}networkRestartNotice.className='notice good';networkRestartNotice.textContent=`Password saved. Atlas is restarting. Reconnect to ${networkData&&networkData.ssid?networkData.ssid:'TurnHub-Atlas'} using the new password, then reopen this page.`;showToast('Wi-Fi password saved. Atlas is restarting.')}catch(e){networkRestartNotice.className='notice';networkRestartNotice.textContent=e.message||'Network password update failed.';showToast(e.message,true)}}

async function pollJson(path,headers={}){
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),5000);
 try{const r=await fetch(path,{cache:'no-store',headers,signal:controller.signal});if(!r.ok)throw new Error('Refresh failed');return await r.json()}finally{clearTimeout(timer)}
}
function refreshAll(){
 if(refreshInFlight)return refreshInFlight;
 refreshInFlight=(async()=>{
  try{
   const s=await pollJson('/api/status');
   const d=await pollJson('/api/devices');
   const seats=await pollJson('/api/seats');
   await refreshSession();
   const n=(sessionInfo&&sessionInfo.permissions&1)?await pollJson('/api/network',authHeaders()):null;
   gameSettingsData=await pollJson('/api/game/settings',authHeaders());
   counterData=sessionInfo&&sessionInfo.participating&&sessionInfo.lifeAvailable?await pollJson('/api/game/counters',authHeaders()):null;
   deviceData=d;seatData=seats;networkData=n;
   renderDevices(d);renderDeviceSettings(d);if(n)renderNetwork(n);renderStatus(s);renderGameLife();renderCounterControls();renderAccess();
  }catch(_){dot.className='dot';connection.textContent='Disconnected';connectionMetric.textContent='Disconnected';counterData=null;lifeFields.disabled=true;renderCounterControls()}
 })().finally(()=>{refreshInFlight=null});
 return refreshInFlight;
}
function renderGameLife(){
 const settings=gameSettingsData,me=sessionInfo||{},state=statusData||{};
 const labels={generic:'Generic',mtg:'Magic: The Gathering',mtg_commander:'MTG Commander',yugioh:'Yu-Gi-Oh!'};
 gameProfileSummary.textContent=settings?`${labels[settings.gameProfile]||settings.gameProfile} · Starting life ${settings.startingLife}`:'';
 renderLifeCards();
 if(settings&&!gameSettingsDirty){gameProfileSelect.value=settings.gameProfile;startingLifeInput.value=settings.startingLife}
 gameSettingsFields.disabled=gameSettingsSaving||!settings||!settings.canEdit;
 const show=!!(me.authenticated&&me.lifeAvailable);
 lifePanel.style.display=show?'block':'none';
 if(show&&myLifeTotal.textContent!==String(me.life))myLifeTotal.textContent=String(me.life);
 lifeStep=settings&&settings.gameProfile==='yugioh'?100:1;lifeBigStep=lifeStep===100?1000:5;
 lifeMinus.textContent='−'+lifeStep;lifePlus.textContent='+'+lifeStep;
 lifeMinusBig.textContent='−'+lifeBigStep;lifePlusBig.textContent='+'+lifeBigStep;
 lifeMinus.setAttribute('aria-label',`Subtract ${lifeStep} life`);lifePlus.setAttribute('aria-label',`Add ${lifeStep} life`);
 lifeMinusBig.setAttribute('aria-label',`Subtract ${lifeBigStep} life`);lifePlusBig.setAttribute('aria-label',`Add ${lifeBigStep} life`);
 lifeFields.disabled=lifeBusy||!show||me.eliminated||!['RUNNING','PAUSED'].includes(state.state)||!!state.winConfirm||!!state.eliminationTarget;
 lifeTens.hidden=!settings||!['mtg','mtg_commander'].includes(settings.gameProfile);
 if(settings&&!settings.available)gameSettingsMessage.textContent='Game settings storage is unavailable.';
}
function chooseGameProfile(){gameSettingsDirty=true;startingLifeInput.value=({generic:40,mtg:20,mtg_commander:40,yugioh:8000})[gameProfileSelect.value]}
async function saveGameSettings(event){
 event.preventDefault();if(gameSettingsSaving)return;
 const settings={gameProfile:gameProfileSelect.value,startingLife:startingLifeInput.value};
 gameSettingsSaving=true;renderGameLife();
 try{
  const r=await fetch('/api/game/settings',{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(settings)});
  const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save game settings');
  gameSettingsDirty=false;gameSettingsMessage.textContent='Game settings saved on Atlas.';
  await refreshAll();await refreshAll();
 }catch(e){gameSettingsMessage.textContent=e.message}
 finally{gameSettingsSaving=false;renderGameLife()}
}
async function adjustMyLife(delta,player=Number(lifeTarget.value)){
 if(lifeBusy)return;
 if(!Number.isInteger(delta)||!delta||Math.abs(delta)>1000000){lifeMessage.textContent='Enter a nonzero whole-number change up to 1000000.';return}
 const target=Number(player),own=target===Number(sessionInfo&&sessionInfo.player);
 if(!target||!counterData||!counterData.editable){lifeMessage.textContent='Refresh the current game before changing life.';return}
 lifeBusy=true;renderGameLife();
 try{
  const r=await fetch(own?'/api/control/life':'/api/control/life/request',{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(own?{delta}:{delta,target})});
  const d=await r.json();if(!r.ok)throw new Error(d.error||'Life change rejected');
  lifeMessage.textContent=own?'Life updated.':d.message||'Life change requested.';lifeDelta.value='';
  await refreshAll();await refreshAll();
 }catch(e){lifeMessage.textContent=e.message+' Check the current total before retrying.'}
 finally{lifeBusy=false;renderGameLife();renderCounterControls()}
}

function updatePlayerOptions(select,seats,preferred){
 const options=seats.map(s=>`<option value="${Number(s.player)}">${esc(s.name||'Player '+s.player)}${Number(s.player)===Number(sessionInfo&&sessionInfo.player)?' (me)':''}</option>`).join('');
 if(select.innerHTML!==options){const previous=select.value;select.innerHTML=options;select.value=seats.some(s=>String(s.player)===previous)?previous:String(preferred);if(!select.value&&seats.length)select.value=String(seats[0].player)}
}
function renderLifeCards(){
 const me=sessionInfo||{},data=counterData;
 const editable=!!(me.authenticated&&!me.eliminated&&data&&data.available&&data.editable&&Number(data.player)===Number(me.player)&&['RUNNING','PAUSED'].includes(statusData?.state)&&!statusData.winConfirm&&!statusData.eliminationTarget);
 const totals=(seatData.seats||[]).filter(s=>s.lifeAvailable).map(s=>{
  const name=s.name||'Player '+s.player,id=Number(s.player),own=id===Number(me.player);
  return `<article class="life-player${s.active?' active':''}${s.eliminated?' eliminated':''}" data-player="${id}" aria-label="${esc(name)} life"><div class="player-head"><h3>${esc(name)}${own?' (me)':''}</h3><span class="small">${s.eliminated?'Eliminated':s.active?'Active turn':''}</span></div><div class="life-total">${Number(s.life)}</div><div class="life-buttons">${[1,-1,10,-10].map(delta=>`<button data-delta="${delta}" aria-label="${delta>0?'Add':'Subtract'} ${Math.abs(delta)} life ${delta>0?'to':'from'} ${esc(name)}" onclick="adjustMyLife(${delta},${id})">${delta>0?'+':'−'}${Math.abs(delta)}</button>`).join('')}</div><p class="small">${own?'Applies immediately':'Requires approval · 15 seconds'}</p></article>`;
 }).join('')||'Life totals appear when the game starts.';
 if(tableLifeTotals.renderedCards!==totals){tableLifeTotals.renderedCards=totals;const focused=document.activeElement,player=focused?.closest('[data-player]')?.dataset.player,delta=focused?.dataset.delta;tableLifeTotals.innerHTML=totals;if(player&&delta)tableLifeTotals.querySelector(`[data-player="${player}"] [data-delta="${delta}"]`)?.focus({preventScroll:true})}
 tableLifeTotals.querySelectorAll('button').forEach(b=>b.disabled=lifeBusy||!editable||b.closest('.life-player').classList.contains('eliminated'));
}
function renderCounterControls(){
 renderLifeCards();
 const me=sessionInfo||{},data=counterData,available=!!(me.authenticated&&data&&data.available&&Number(data.player)===Number(me.player));
 const owner=me.profileId||'';
 if(counterOwner!==owner){counterOwner=owner;lifeTarget.innerHTML='';commanderSource.innerHTML='';commanderDelta.value='';lifeDelta.value='';lifeMessage.textContent='';commanderMessage.textContent=''}
 updatePlayerOptions(lifeTarget,(seatData.seats||[]).filter(s=>s.lifeAvailable&&!s.eliminated),me.player);
 const own=Number(lifeTarget.value)===Number(me.player);
 lifeTargetHelp.textContent=own?'Your own changes apply immediately.':'This sends a request. The recipient can accept or reject it; Atlas accepts it after 15 seconds without a response.';
 lifeSubmit.textContent=own?'Apply life change':'Request life change';
 if(!available||!data.editable)lifeFields.disabled=true;
 const requests=available?data.requests||[]:[],incoming=requests.find(r=>Number(r.target)===Number(me.player));
 lifeRequestsPanel.hidden=!me.authenticated||(!me.lifeAvailable&&!requests.length);
 const outcomes={accepted:'Accepted',rejected:'Rejected',automatic:'Automatically accepted',cancelled:'Cancelled by a table decision or player departure',failed:'Not applied: life limits or player state changed'};
 const describe=r=>`${playerLabel(r.actor)} requests ${Number(r.delta)>0?'+':''}${r.delta} life for ${playerLabel(r.target)}.`;
 const notice=!available?'Request status unavailable. Reconnect to Atlas; its 15-second timer continues.':incoming?describe(incoming)+(incoming.state==='pending'?' Accept or reject this change.':' '+(outcomes[incoming.state]||incoming.state)+'.'):'No request awaiting your response.';
 if(lifeRequestNotice.textContent!==notice)lifeRequestNotice.textContent=notice;
 const pending=incoming&&incoming.state==='pending';
 lifeRequestAlert.hidden=!pending;
 const alertText=pending?`${playerLabel(incoming.actor)} requests ${Number(incoming.delta)>0?'+':''}${incoming.delta} life for you. Respond within 15 seconds or Atlas accepts it.`:'';
 if(lifeAlertText.textContent!==alertText)lifeAlertText.textContent=alertText;
 const controls=pending?`<div class="actions"><button class="good" onclick="respondLifeChange(${Number(incoming.id)},true)">Accept life change</button><button class="bad" onclick="respondLifeChange(${Number(incoming.id)},false)">Reject life change</button></div>`:'';
 if(lifeRequestActions.innerHTML!==controls)lifeRequestActions.innerHTML=controls;
 lifeRequestActions.querySelectorAll('button').forEach(button=>button.disabled=counterBusy||!available||!data.editable);
 lifeRequestCountdown.textContent=pending?`Atlas automatically accepts in ${Math.ceil(Number(incoming.remainingMs)/1000)} seconds.`:'';
 const outgoing=requests.filter(r=>Number(r.actor)===Number(me.player)).map(r=>`<p>${esc(describe(r))} ${esc(r.state==='pending'?'Awaiting response · '+Math.ceil(Number(r.remainingMs)/1000)+' seconds':outcomes[r.state]||r.state)}</p>`).join('');
 if(outgoingLifeRequests.innerHTML!==outgoing)outgoingLifeRequests.innerHTML=outgoing;
 commanderPanel.hidden=!available||!data.commanderEnabled;
 commanderFields.disabled=counterBusy||!available||!data.editable;
 if(available&&data.commanderEnabled){
  updatePlayerOptions(commanderSource,seatData.seats||[],(seatData.seats||[]).find(s=>Number(s.player)!==Number(me.player))?.player||me.player);
  const totals=(data.damage||[]).map(d=>`<div class="status-row"><span>${esc(playerLabel(d.source))}</span><strong>Commander 1: ${Number(d.commanders[0])}<br>Commander 2: ${Number(d.commanders[1])}</strong></div>`).join('');
  if(commanderTotals.innerHTML!==totals)commanderTotals.innerHTML=totals;
 }
}
async function sendCounterControl(path,values,messageElement){
 if(counterBusy||!counterData||!counterData.editable)return;
 counterBusy=true;renderCounterControls();
 try{
  const r=await fetch('/api/control/'+path,{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(values)});
  const result=await r.json();if(!r.ok)throw Error(result.error||'Change rejected');
  messageElement.textContent=result.message||'Updated';
  if(path==='commander')commanderDelta.value='';
  await refreshAll();await refreshAll();
 }catch(e){messageElement.textContent=e.message+' Check the current totals before retrying.';showToast(e.message,true);await refreshAll()}
 finally{counterBusy=false;renderCounterControls()}
}
function respondLifeChange(requestId,accept){return sendCounterControl('life/respond',{requestId,accept:accept?'1':'0'},lifeMessage)}
function reviewLifeRequest(){showTab('game');lifeRequestsPanel.scrollIntoView({block:'center'});const button=lifeRequestActions.querySelector('button');if(button)button.focus({preventScroll:true})}
function saveCommanderDamage(event){event.preventDefault();return sendCounterControl('commander',{source:commanderSource.value,commander:commanderSlot.value,delta:commanderDelta.value},commanderMessage)}
function renderInviteCodes(){
 for(const [id,path] of [['portal','/portal'],['join','/login']]){
  const url=location.origin+path,box=document.getElementById(id+'Qr'),link=document.getElementById(id+'QrLink');link.href=url;
  try{const code=qrcode(0,'M');code.addData(url);code.make();box.innerHTML=code.createSvgTag({cellSize:4,margin:16,scalable:true});box.querySelector('svg').setAttribute('aria-label',id==='portal'?'Table portal QR code':'Sign in QR code')}
  catch(_){box.textContent='QR code unavailable. Use the link below.'}
 }
}
renderInviteCodes();portalAddress.textContent=location.host;loadBrowserPrefs();showTab(localStorage.getItem('turnhubPortalTab')||'game');refreshAll();setInterval(refreshAll,800);

let lastAccessKey='';
function showTab(name){if(!['game','players','account','settings'].includes(name))name='game';if(name==='settings'&&!(sessionInfo&&sessionInfo.permissions&1))name='game';originalShowTab(name);if(['account','players','settings'].includes(name))refreshAccountList()}
function renderAccess(){
 const flags=sessionInfo&&sessionInfo.permissions||0;
 deviceSettingsTab.hidden=!(flags&1);devLink.hidden=!(flags&4);gmPanel.hidden=!(flags&2);
 if(!(flags&1)&&document.getElementById('view-settings').classList.contains('active'))originalShowTab('game');
 if(!(flags&1))adminAccounts.innerHTML='';if(!(flags&2))gmAccounts.innerHTML='';
 if(!sessionInfo||!sessionInfo.authenticated)myModeration.textContent='Sign in to view your counts.';
 const key=(sessionInfo&&sessionInfo.profileId||'')+':'+flags;
 if(key!==lastAccessKey){lastAccessKey=key;if(sessionInfo&&sessionInfo.authenticated)refreshAccountList()}
}
function renderDeviceSettings(d){deviceSettingsList.innerHTML=(d.devices||[]).map(x=>`<div class="device"><strong>${esc(x.label)}</strong><div class="actions"><button onclick="renameSigil(${x.id})">Rename</button>${x.customName?`<button onclick="clearSigilName(${x.id})">Clear name</button>`:''}</div></div>`).join('')}
async function accountPost(path,data){const r=await fetch('/api/accounts/'+path,{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const d=await r.json();if(!r.ok)throw Error(d.error||'Request failed');return d}
async function setupAdmin(){try{await accountPost('setup',{});adminSetup.hidden=true;await refreshAll();showTab('settings');showToast('Admin account established.')}catch(e){showToast(e.message,true)}}
async function loadSetup(){try{const d=await pollJson('/api/accounts/setup');adminSetup.hidden=!d.setupRequired}catch(e){showToast('Account setup unavailable',true)}}
async function refreshAccountList(){
 if(!sessionToken||!sessionInfo||!sessionInfo.authenticated)return;
 const requestedToken=sessionToken;
 try{const d=await pollJson('/api/accounts',authHeaders()),flags=sessionInfo&&sessionInfo.permissions||0;
 if(requestedToken!==sessionToken||!sessionInfo)return;
 d.accounts.sort((a,b)=>Number(!!a.archived)-Number(!!b.archived));
 const mine=d.accounts.find(a=>a.profileId===sessionInfo.profileId);
 if(mine)myModeration.textContent=`Connection resets: ${mine.connectionResets||0} · Game removals: ${mine.gameRemovals||0}`;
 adminAccounts.innerHTML=flags&1?d.accounts.map(a=>`<form class="setting-box" onsubmit="savePermissions(event,'${a.profileId}')"><strong>${esc(a.name||a.profileId)}${a.archived?' · Archived':''}</strong><div class="small">${a.profileId}</div>${[[1,'Admin'],[2,'Game Master'],[4,'Developer'],[8,'GM: reset connections'],[16,'GM: remove from game']].map(([bit,label])=>`<label style="display:block;margin:8px"><input type="checkbox" value="${bit}" ${a.permissions&bit?'checked':''}> ${label}</label>`).join('')}<button>Save permissions</button> <button type="button" class="${a.archived?'':'bad'}" onclick="archiveAccount('${a.profileId}',${!a.archived})">${a.archived?'Restore account':'Archive account'}</button></form>`).join(''):'';
 gmAccounts.innerHTML=flags&2?d.accounts.filter(a=>!a.archived).map(a=>`<div class="setting-box"><strong>${esc(a.name||a.profileId)}</strong><p class="small">Private counts · resets: ${a.connectionResets} · removals: ${a.gameRemovals}</p><div class="actions"><button onclick="moderate('${a.profileId}','pass')">Force pass</button><button onclick="moderate('${a.profileId}','${a.nudgeMuted?'unmute':'mute'}')">${a.nudgeMuted?'Unmute':'Mute'} nudges</button>${flags&8?`<button onclick="moderate('${a.profileId}','reset')">Reset connections</button>`:''}${flags&16?`<button class="bad" onclick="moderate('${a.profileId}','remove')">Remove from game</button>`:''}</div></div>`).join('')+'<p class="small">Archived accounts are hidden from Game Master actions. Nudge mute is saved for the future nudge feature. Connection resets keep the seat and life totals. Removal ends active participation.</p>':'';
 }catch(e){showToast(e.message,true)}
}
async function savePermissions(event,id){event.preventDefault();const form=event.currentTarget;const permissions=[...form.querySelectorAll('input:checked')].reduce((n,x)=>n|Number(x.value),0);try{await accountPost('permissions',{profileId:id,permissions});await refreshAll();await refreshAccountList();showToast('Permissions saved')}catch(e){showToast(e.message,true)}}
async function archiveAccount(id,archived){if(!confirm(id+': '+(archived?'Archive this account? Sign-in and Sigil use will be blocked; statistics stay saved. The account must first leave the table.':'Restore this account and its existing permissions?')))return;try{await accountPost('archive',{profileId:id,archived:archived?'1':'0'});await refreshAll();await refreshAccountList();showToast(archived?'Account archived':'Account restored')}catch(e){showToast(e.message,true)}}
async function moderate(id,action){const meaning={reset:'Invalidate all browser sessions and suspend Sigil controls until this account signs in again? The seat and life totals stay.',remove:'Remove this player from the game and invalidate their connections?',pass:'Immediately pass this player’s turn?',mute:'Block this account from sending future nudges?',unmute:'Allow this account to send future nudges?'};if(!confirm(id+': '+meaning[action]))return;try{await accountPost('moderate',{profileId:id,action});await refreshAll();await refreshAccountList();showToast('Moderation applied')}catch(e){showToast(e.message,true)}}
loadSetup();
</script>
</body>
</html>
)HTML";

const char DEV_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#0b0d11"><title>TurnHub Dev</title><style>:root{color-scheme:dark}body{margin:0;padding:18px;background:#0b0d11;color:#f3f5f7;font-family:ui-monospace,Consolas,monospace}main{max-width:1000px;margin:auto}a{color:#9fc0ff}.toolbar{display:flex;justify-content:space-between;gap:12px;align-items:center;flex-wrap:wrap}button{background:#202632;color:#f3f5f7;border:1px solid #2b3240;border-radius:8px;padding:8px 12px;font:inherit;cursor:pointer}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#141820;border:1px solid #2b3240;border-radius:14px;padding:14px}.activity{max-height:430px;overflow:auto}.event{border-bottom:1px solid #2b3240;padding:8px 0}.event:last-child{border-bottom:0}.time{color:#9ca6b7;margin-right:8px}.kind{color:#9fc0ff;font-weight:700;margin-right:8px}.message{overflow-wrap:anywhere}</style></head><body><main><div class="toolbar"><h1>TurnHub Dev</h1><button onclick="refresh()">Refresh now</button></div><p><a href="/portal">Back to portal</a> · <a href="/stats">Player stats</a></p><h2>Activity monitor</h2><p>Recent in-memory Atlas events. The feed clears when Atlas restarts and is never written to flash.</p><div id="activity" class="activity">Loading...</div><h2>Status</h2><pre id="status">Loading...</pre><h2>Devices</h2><pre id="devices">Loading...</pre><h2>Seats</h2><pre id="seats">Loading...</pre><h2>Runtime diagnostics</h2><pre id="diagnostics">Loading...</pre><script>const token=()=>localStorage.getItem('turnhubSessionToken')||'';function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}async function fetchJson(id){const x=await fetch('/api/'+id,{cache:'no-store',headers:{'X-TurnHub-Token':token()}});return await x.json()}async function refresh(){try{const a=await fetchJson('diagnostics/activity');document.getElementById('activity').innerHTML=(a.events||[]).map(e=>`<div class="event"><span class="time">${Math.round(Number(e.ageMs||0)/100)/10}s ago</span><span class="kind">${esc(e.kind)}</span><span class="message">${esc(e.message)}</span></div>`).join('')||'<div>Waiting for activity…</div>';for(const id of ['status','devices','seats','diagnostics']){try{document.getElementById(id).textContent=JSON.stringify(await fetchJson(id),null,2)}catch(_){document.getElementById(id).textContent='Disconnected'}}}catch(_){document.getElementById('activity').textContent='Disconnected'}}refresh();setInterval(refresh,1000)</script></main></body></html>
)HTML";

}  // namespace TurnHubWeb
