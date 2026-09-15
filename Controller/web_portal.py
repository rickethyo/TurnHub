"""Local TurnHub portal with status, settings, and secure seat-paired Pass control."""

from __future__ import annotations

import json
import socket
import threading
import time

from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from config import (
    MODULE_IDS,
    START_COUNTDOWN_SECONDS,
    STATE_GAME_OVER,
    STATE_LOBBY,
    STATE_PAUSED,
    STATE_RUNNING,
    STATE_STARTING,
    WARNING_CAUTION_FRACTION,
    WARNING_OFF,
    WARNING_OFF_GREEN_MS,
    WEB_HOST,
    WEB_PORT,
)


PORTAL_HTML = r'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#111318">
<title>TurnHub</title>
<style>
:root {
  color-scheme: dark;
  --bg: #0b0d11;
  --panel: #141820;
  --panel-2: #1b202a;
  --line: #2b3240;
  --text: #f3f5f7;
  --muted: #9ca6b7;
  --good: #62d58a;
  --warn: #efc55a;
  --bad: #ef6a6a;
  --blue: #72a7ff;
}
* { box-sizing: border-box; }
html, body { margin: 0; min-height: 100%; background: var(--bg); color: var(--text); font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
body { padding: max(16px, env(safe-area-inset-top)) max(16px, env(safe-area-inset-right)) max(18px, env(safe-area-inset-bottom)) max(16px, env(safe-area-inset-left)); }
.shell { width: min(1100px, 100%); margin: 0 auto; }
header { display:flex; align-items:center; justify-content:space-between; gap:12px; margin-bottom:16px; }
.brand { font-size: clamp(1.35rem, 4vw, 2rem); font-weight: 800; letter-spacing: .03em; }
.connection { display:flex; align-items:center; gap:8px; color:var(--muted); font-size:.95rem; }
.dot { width:10px; height:10px; border-radius:50%; background:var(--bad); box-shadow:0 0 0 4px rgba(239,106,106,.1); }
.dot.online { background:var(--good); box-shadow:0 0 0 4px rgba(98,213,138,.1); }
.grid { display:grid; grid-template-columns: repeat(12, 1fr); gap:12px; }
.card { grid-column: span 12; background:var(--panel); border:1px solid var(--line); border-radius:18px; padding:18px; box-shadow:0 12px 35px rgba(0,0,0,.18); }
.hero { min-height:270px; display:flex; flex-direction:column; align-items:center; justify-content:center; text-align:center; }
.eyebrow { color:var(--muted); font-size:.85rem; font-weight:700; letter-spacing:.12em; text-transform:uppercase; }
.hero-title { margin:.25rem 0 .2rem; font-size:clamp(3rem, 13vw, 7.5rem); line-height:.94; font-weight:900; letter-spacing:-.04em; }
.hero-sub { color:var(--muted); font-size:clamp(1rem, 3vw, 1.35rem); }
.timer { margin-top:18px; font-variant-numeric:tabular-nums; font-size:clamp(2rem, 8vw, 4rem); font-weight:800; }
.badges { display:flex; flex-wrap:wrap; justify-content:center; gap:8px; margin-top:15px; }
.badge { border:1px solid var(--line); background:var(--panel-2); border-radius:999px; padding:7px 11px; color:var(--muted); font-size:.85rem; font-weight:700; }
.badge.good { color:var(--good); border-color:rgba(98,213,138,.35); }
.badge.warn { color:var(--warn); border-color:rgba(239,197,90,.35); }
.badge.bad { color:var(--bad); border-color:rgba(239,106,106,.35); }
.badge.blue { color:var(--blue); border-color:rgba(114,167,255,.35); }
.metrics { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:10px; }
.metric { background:var(--panel-2); border:1px solid var(--line); border-radius:14px; padding:13px; }
.metric-label { color:var(--muted); font-size:.78rem; font-weight:700; letter-spacing:.07em; text-transform:uppercase; }
.metric-value { margin-top:5px; font-size:1.18rem; font-weight:800; font-variant-numeric:tabular-nums; overflow-wrap:anywhere; }
.section-title { margin:0 0 12px; font-size:.9rem; color:var(--muted); text-transform:uppercase; letter-spacing:.1em; }
.players { display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:10px; }
.player { background:var(--panel-2); border:1px solid var(--line); border-radius:14px; padding:14px; }
.player.active { border-color:var(--blue); box-shadow:0 0 0 1px rgba(114,167,255,.25) inset; }
.player.winner { border-color:var(--good); }
.player.eliminated { border-color:rgba(239,106,106,.38); opacity:.62; }
.player.eliminated .player-name { text-decoration:line-through; text-decoration-thickness:2px; }
.player-head { display:flex; justify-content:space-between; gap:8px; align-items:center; }
.player-name { font-size:1.22rem; font-weight:850; }
.player-seat { color:var(--muted); font-size:.88rem; margin-top:3px; }
.player-stat { margin-top:12px; display:flex; justify-content:space-between; color:var(--muted); font-size:.9rem; }
.modules { display:flex; flex-wrap:wrap; gap:9px; }
.module { display:flex; align-items:center; gap:8px; background:var(--panel-2); border:1px solid var(--line); border-radius:999px; padding:8px 11px; color:var(--muted); }
.module .mini-dot { width:8px; height:8px; border-radius:50%; background:var(--bad); }
.module.online .mini-dot { background:var(--good); }
.footer { color:var(--muted); text-align:center; font-size:.78rem; margin-top:14px; }
.hidden { display:none !important; }
.header-actions { display:flex; align-items:center; gap:10px; }
.settings-button { border:1px solid var(--line); background:var(--panel); color:var(--text); border-radius:12px; padding:9px 12px; font:inherit; font-weight:750; cursor:pointer; }
.settings-button:active { transform:translateY(1px); }
.overlay { position:fixed; inset:0; z-index:50; background:rgba(0,0,0,.72); display:flex; align-items:flex-start; justify-content:center; padding:max(20px,env(safe-area-inset-top)) 14px 20px; overflow:auto; }
.dialog { width:min(680px,100%); background:var(--panel); border:1px solid var(--line); border-radius:20px; padding:20px; box-shadow:0 24px 70px rgba(0,0,0,.45); }
.dialog-head { display:flex; align-items:center; justify-content:space-between; gap:12px; margin-bottom:8px; }
.dialog-title { font-size:1.35rem; font-weight:850; }
.close-button { border:0; background:transparent; color:var(--muted); font-size:1.8rem; cursor:pointer; }
.settings-note { color:var(--muted); line-height:1.45; font-size:.9rem; margin:0 0 18px; }
.settings-group { margin-top:18px; }
.settings-group h3 { color:var(--muted); font-size:.8rem; text-transform:uppercase; letter-spacing:.1em; margin:0 0 10px; }
.form-grid { display:grid; grid-template-columns:1fr; gap:10px; }
.field { background:var(--panel-2); border:1px solid var(--line); border-radius:14px; padding:11px; }
.field label { display:block; color:var(--muted); font-size:.78rem; font-weight:700; margin-bottom:7px; }
.field input { width:100%; border:1px solid var(--line); background:var(--bg); color:var(--text); border-radius:10px; padding:10px 11px; font:inherit; outline:none; }
.field input:focus { border-color:var(--blue); }
.dialog-actions { display:flex; flex-wrap:wrap; gap:10px; margin-top:18px; }
.primary-button, .secondary-button { border-radius:12px; padding:10px 14px; font:inherit; font-weight:800; cursor:pointer; }
.primary-button { border:1px solid rgba(114,167,255,.5); background:rgba(114,167,255,.14); color:var(--blue); }
.secondary-button { border:1px solid var(--line); background:var(--panel-2); color:var(--text); }
.settings-status { color:var(--muted); font-size:.85rem; min-height:1.2em; margin-top:10px; }
.identity-button { border:1px solid var(--line); background:var(--panel); color:var(--muted); border-radius:12px; padding:9px 12px; font:inherit; font-weight:750; cursor:pointer; max-width:220px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.identity-button.paired { color:var(--blue); border-color:rgba(114,167,255,.38); }
.pass-button { margin-top:18px; width:min(340px,100%); border:1px solid rgba(98,213,138,.55); background:rgba(98,213,138,.15); color:var(--good); border-radius:16px; padding:15px 18px; font:inherit; font-size:1.15rem; font-weight:900; cursor:pointer; }
.pass-button:disabled { opacity:.55; cursor:default; }
.game-actions { margin-top:12px; width:min(520px,100%); display:flex; flex-wrap:wrap; justify-content:center; gap:9px; }
.control-button { border-radius:14px; padding:12px 15px; font:inherit; font-weight:850; cursor:pointer; border:1px solid var(--line); background:var(--panel-2); color:var(--text); }
.control-button.pause { color:var(--warn); border-color:rgba(239,197,90,.48); background:rgba(239,197,90,.10); }
.control-button.win { color:var(--good); border-color:rgba(98,213,138,.48); background:rgba(98,213,138,.10); }
.control-button.confirm { color:var(--good); border-color:rgba(98,213,138,.55); background:rgba(98,213,138,.15); }
.control-button.deny { color:var(--bad); border-color:rgba(239,106,106,.55); background:rgba(239,106,106,.12); }
.control-button:disabled { opacity:.55; cursor:default; }
.choice-list { display:grid; gap:10px; margin-top:14px; }
.choice { width:100%; text-align:left; border:1px solid var(--line); background:var(--panel-2); color:var(--text); border-radius:14px; padding:13px 14px; font:inherit; cursor:pointer; }
.choice:disabled { opacity:.5; cursor:not-allowed; }
.choice strong { display:block; font-size:1.05rem; }
.choice span { display:block; color:var(--muted); font-size:.84rem; margin-top:4px; }
.security-note { border:1px solid rgba(114,167,255,.28); background:rgba(114,167,255,.08); border-radius:14px; padding:12px 13px; color:var(--muted); line-height:1.42; font-size:.88rem; }
@media (min-width:600px) { .form-grid { grid-template-columns:repeat(2,minmax(0,1fr)); } }
@media (min-width:760px) {
  .hero-card { grid-column:span 8; }
  .side-card { grid-column:span 4; }
  .players-card { grid-column:span 8; }
  .modules-card { grid-column:span 4; }
  .metrics { grid-template-columns:1fr; }
}
</style>
</head>
<body>
<div class="shell">
<header>
  <div class="brand">TurnHub</div>
  <div class="header-actions">
    <div class="connection"><span id="connDot" class="dot"></span><span id="connText">Connecting…</span></div>
    <button id="identityButton" class="identity-button" type="button" onclick="openIdentity()">Choose player</button>
    <button class="settings-button" type="button" onclick="openSettings()">Settings</button>
  </div>
</header>

<div class="grid">
  <section class="card hero-card hero">
    <div id="eyebrow" class="eyebrow">TURNHUB</div>
    <div id="heroTitle" class="hero-title">…</div>
    <div id="heroSub" class="hero-sub"></div>
    <div id="turnTimer" class="timer hidden">00:00</div>
    <div id="heroBadges" class="badges"></div>
    <button id="passButton" class="pass-button hidden" type="button" onclick="passTurnFromWeb()">Pass Turn</button>
    <div id="gameActions" class="game-actions">
      <button id="pauseButton" class="control-button pause hidden" type="button" onclick="pauseFromWeb()">Pause Game</button>
      <button id="winButton" class="control-button win hidden" type="button" onclick="claimWinFromWeb()">I Win</button>
      <button id="confirmWinButton" class="control-button confirm hidden" type="button" onclick="confirmWinFromWeb()">Confirm Win</button>
      <button id="denyWinButton" class="control-button deny hidden" type="button" onclick="denyWinFromWeb()">Deny Claim</button>
      <button id="cancelWinButton" class="control-button deny hidden" type="button" onclick="cancelWinFromWeb()">Cancel Win Claim</button>
    </div>
  </section>

  <section class="card side-card">
    <h2 class="section-title">Game</h2>
    <div class="metrics">
      <div class="metric"><div class="metric-label">State</div><div id="stateValue" class="metric-value">—</div></div>
      <div class="metric"><div class="metric-label">Game Time</div><div id="gameTime" class="metric-value">00:00</div></div>
      <div class="metric"><div class="metric-label">Timer Setting</div><div id="timerSetting" class="metric-value">—</div></div>
      <div class="metric"><div class="metric-label">Host</div><div id="hostValue" class="metric-value">—</div></div>
    </div>
  </section>

  <section class="card players-card">
    <h2 class="section-title">Players</h2>
    <div id="players" class="players"><div class="player"><div class="player-seat">Waiting for players…</div></div></div>
  </section>

  <section class="card modules-card">
    <h2 class="section-title">Modules</h2>
    <div id="modules" class="modules"></div>
  </section>
</div>

<div class="footer">Local TurnHub • physical controls always remain available</div>
</div>

<div id="settingsOverlay" class="overlay hidden" role="dialog" aria-modal="true" aria-labelledby="settingsTitle">
  <div class="dialog">
    <div class="dialog-head">
      <div id="settingsTitle" class="dialog-title">TurnHub Settings</div>
      <button class="close-button" type="button" aria-label="Close settings" onclick="closeSettings()">×</button>
    </div>
    <p class="settings-note">Names are stored on this TurnHub and follow the physical module/seat, so a person's name stays with that seat even if player numbers shift during lobby setup.</p>

    <div class="settings-group">
      <h3>Module names</h3>
      <div id="moduleSettings" class="form-grid"></div>
    </div>

    <div class="settings-group">
      <h3>Player / seat names</h3>
      <div id="seatSettings" class="form-grid"></div>
    </div>

    <div class="settings-group">
      <h3>Persistence</h3>
      <p id="persistenceNote" class="settings-note">Game state autosaves locally. If TurnHub restarts during a game, it recovers paused so downtime is never charged to a player.</p>
    </div>

    <div class="dialog-actions">
      <button class="primary-button" type="button" onclick="saveSettings()">Save names</button>
      <button class="secondary-button" type="button" onclick="saveGameStateNow()">Save game state now</button>
      <button class="secondary-button" type="button" onclick="closeSettings()">Cancel</button>
    </div>
    <div id="settingsStatus" class="settings-status"></div>
  </div>
</div>

<div id="identityOverlay" class="overlay hidden" role="dialog" aria-modal="true" aria-labelledby="identityTitle">
  <div class="dialog">
    <div class="dialog-head">
      <div id="identityTitle" class="dialog-title">Which player are you?</div>
      <button class="close-button" type="button" aria-label="Close player selection" onclick="closeIdentity()">×</button>
    </div>
    <p id="identityNote" class="settings-note">Pair this browser to your physical seat. TurnHub will ask for a button press on the table before granting control.</p>
    <div class="security-note">A browser never gains Pass control just by choosing a name. Lobby pairing requires the matching physical module. Mid-game reassignment only works while paused and requires host-module approval.</div>
    <div id="identityChoices" class="choice-list"></div>
    <div id="identityActions" class="dialog-actions"></div>
    <div id="identityStatus" class="settings-status"></div>
  </div>
</div>

<script>
let latest = null;
let fetchedAt = performance.now();
let online = false;
let currentIdentity = null;
let identityPollTimer = null;
let identityAutoPrompted = false;
const WEB_TOKEN_KEY = 'turnhub.webControllerToken';
const DISPLAY_ONLY_KEY = 'turnhub.displayOnly';

function esc(v) {
  return String(v ?? '').replace(/[&<>'"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[c]));
}

function fmt(seconds) {
  seconds = Math.max(0, Math.floor(Number(seconds || 0)));
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return `${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
}

function dynamicSeconds(base, shouldRun) {
  if (!shouldRun) return Number(base || 0);
  return Number(base || 0) + ((performance.now() - fetchedAt) / 1000);
}

function playerLabel(p) {
  if (!p) return 'None';
  return p.display_name || `Player ${p.player_number}`;
}

function seatLabel(p) {
  if (!p) return '';
  const moduleName = p.module_name || `Module ${p.module_id}`;
  return `${moduleName}${p.slot_name ? ' • Seat ' + p.slot_name : ''}`;
}

function playerMeta(p) {
  if (!p) return '';
  return p.has_custom_name ? `Player ${p.player_number} • ${seatLabel(p)}` : seatLabel(p);
}

function badge(text, cls='') {
  return `<span class="badge ${cls}">${esc(text)}</span>`;
}

function phaseBadge(d) {
  const phase = d.warning_phase;
  if (phase === 'WARNING') return badge('TURN WARNING', 'bad');
  if (phase === 'CAUTION') return badge('CAUTION', 'warn');
  if (phase === 'OFF_GREEN') return badge('5+ MINUTES', 'good');
  return '';
}

function renderHero(d) {
  const state = d.state;
  const title = document.getElementById('heroTitle');
  const sub = document.getElementById('heroSub');
  const eye = document.getElementById('eyebrow');
  const timer = document.getElementById('turnTimer');
  const badges = document.getElementById('heroBadges');

  timer.classList.add('hidden');
  badges.innerHTML = '';

  if (state === 'LOBBY') {
    eye.textContent = 'LOBBY';
    title.textContent = d.players.length ? `${d.players.length} PLAYER${d.players.length === 1 ? '' : 'S'}` : 'READY';
    sub.textContent = d.starter ? `${playerLabel(d.starter)} selected to start` : 'Waiting for starter selection';
    if (d.starter) badges.innerHTML += badge(`Starter: ${seatLabel(d.starter)}`, 'blue');
  } else if (state === 'STARTING') {
    eye.textContent = 'STARTING';
    title.textContent = String(Math.max(1, Math.ceil(d.countdown_remaining_seconds || 0)));
    sub.textContent = d.starter ? `${playerLabel(d.starter)} goes first` : 'Game starting';
    badges.innerHTML = d.starter ? badge(seatLabel(d.starter), 'blue') : '';
  } else if (state === 'RUNNING') {
    eye.textContent = 'ACTIVE TURN';
    title.textContent = playerLabel(d.active_player).toUpperCase();
    sub.textContent = seatLabel(d.active_player);
    timer.classList.remove('hidden');
    timer.textContent = fmt(dynamicSeconds(d.turn_elapsed_seconds, true));
    badges.innerHTML = phaseBadge(d);
    if (d.active_player) badges.innerHTML += badge(`Turn ${d.active_turn_number}`, 'blue');
  } else if (state === 'PAUSED') {
    if (d.win_claim) {
      eye.textContent = 'VICTORY CLAIM';
      title.textContent = playerLabel(d.win_claim.claimant).toUpperCase();
      const confirmed = d.win_claim.confirmed_count || 0;
      const required = d.win_claim.required_count || 0;
      sub.textContent = `Waiting for confirmations • ${confirmed}/${required}`;
      timer.classList.add('hidden');
      badges.innerHTML = badge('Game clock frozen', 'warn');
      if (d.win_claim.next_confirmation) {
        badges.innerHTML += badge(`Physical vote: ${playerLabel(d.win_claim.next_confirmation)} • Action = confirm • Pass = deny`, 'good');
      }
    } else if (d.elimination_target) {
      eye.textContent = 'ELIMINATION SELECTED';
      title.textContent = playerLabel(d.elimination_target).toUpperCase();
      sub.textContent = seatLabel(d.elimination_target);
      timer.classList.add('hidden');
      badges.innerHTML = badge(`Press Pass on ${d.elimination_target.module_name || 'Module ' + d.elimination_target.module_id} to confirm`, 'bad');
    } else {
      eye.textContent = 'GAME PAUSED';
      title.textContent = 'PAUSED';
      sub.textContent = d.active_player ? `${playerLabel(d.active_player)} • ${seatLabel(d.active_player)}` : '';
      timer.classList.remove('hidden');
      timer.textContent = fmt(d.turn_elapsed_seconds);
      badges.innerHTML = badge('Timer stopped', 'warn');
    }
  } else if (state === 'GAME_OVER') {
    eye.textContent = 'GAME OVER';
    title.textContent = d.winner ? playerLabel(d.winner).toUpperCase() : 'COMPLETE';
    sub.textContent = d.winner ? `${seatLabel(d.winner)} wins` : '';
    badges.innerHTML = badge('Winner', 'good');
  } else {
    eye.textContent = 'TURNHUB';
    title.textContent = state || '—';
    sub.textContent = '';
  }
}

function renderPlayers(d) {
  const root = document.getElementById('players');
  if (!d.players.length) {
    root.innerHTML = '<div class="player"><div class="player-seat">Waiting for players…</div></div>';
    return;
  }

  root.innerHTML = d.players.map(p => {
    const active = d.active_player && d.active_player.player_number === p.player_number;
    const winner = d.winner && d.winner.player_number === p.player_number;
    const starter = d.starter && d.starter.player_number === p.player_number;
    const eliminated = !!p.eliminated;
    let cls = 'player';
    if (active) cls += ' active';
    if (winner) cls += ' winner';
    if (eliminated) cls += ' eliminated';
    const flags = [];
    if (active) flags.push(badge('ACTIVE','blue'));
    if (starter) flags.push(badge('STARTER','blue'));
    if (winner) flags.push(badge('WINNER','good'));
    if (d.win_claim && d.win_claim.claimant && d.win_claim.claimant.player_number === p.player_number) flags.push(badge('WIN CLAIM','good'));
    if (d.win_claim && (d.win_claim.confirmed_players || []).some(v => v.player_number === p.player_number)) flags.push(badge('CONFIRMED','good'));
    if (eliminated) flags.push(badge('ELIMINATED','bad'));
    return `<div class="${cls}">
      <div class="player-head"><div><div class="player-name">${esc(playerLabel(p))}</div><div class="player-seat">${esc(playerMeta(p))}</div></div><div>${flags.join(' ')}</div></div>
      <div class="player-stat"><span>Completed turns</span><strong>${p.turns_completed ?? 0}</strong></div>
      <div class="player-stat"><span>Turn time total</span><strong>${fmt(p.completed_turn_seconds ?? 0)}</strong></div>
    </div>`;
  }).join('');
}

function moduleLabel(m) {
  if (!m) return 'None';
  return m.module_name || `Module ${m.module_id}`;
}

function renderModules(d) {
  document.getElementById('modules').innerHTML = d.modules.map(m =>
    `<div class="module ${m.connected ? 'online' : ''}"><span class="mini-dot"></span><strong>${esc(moduleLabel(m))}</strong><span>${m.connected ? 'Online' : 'Offline'}</span></div>`
  ).join('');
}

function render(d) {
  document.getElementById('stateValue').textContent = d.state.replaceAll('_',' ');
  const gameRunning = d.state === 'RUNNING';
  document.getElementById('gameTime').textContent = fmt(dynamicSeconds(d.game_elapsed_seconds, gameRunning));
  document.getElementById('timerSetting').textContent = d.timer_setting;
  document.getElementById('hostValue').textContent = d.host_module === null ? 'None' : (d.host_module_name || `Module ${d.host_module}`);
  renderHero(d);
  renderPlayers(d);
  renderModules(d);
  renderWebControls(d);
  updateIdentityChip();
  maybePromptIdentity();
}

function browserToken() {
  return localStorage.getItem(WEB_TOKEN_KEY) || '';
}

function authHeaders(extra={}) {
  const token = browserToken();
  return token ? {...extra, 'Authorization': `Bearer ${token}`} : extra;
}

function sameSeat(a, b) {
  return !!a && !!b && Number(a.module_id) === Number(b.module_id) && Number(a.slot) === Number(b.slot);
}

function updateIdentityChip() {
  const button = document.getElementById('identityButton');
  if (currentIdentity && currentIdentity.player) {
    button.textContent = `You: ${playerLabel(currentIdentity.player)}`;
    button.classList.add('paired');
  } else if (localStorage.getItem(DISPLAY_ONLY_KEY) === '1') {
    button.textContent = 'You: Display only';
    button.classList.remove('paired');
  } else {
    button.textContent = 'Choose player';
    button.classList.remove('paired');
  }
}

function renderWebControls(d) {
  const passButton = document.getElementById('passButton');
  const pauseButton = document.getElementById('pauseButton');
  const winButton = document.getElementById('winButton');
  const confirmButton = document.getElementById('confirmWinButton');
  const denyButton = document.getElementById('denyWinButton');
  const cancelButton = document.getElementById('cancelWinButton');

  for (const button of [passButton, pauseButton, winButton, confirmButton, denyButton, cancelButton]) {
    button.classList.add('hidden');
    button.disabled = false;
  }
  passButton.textContent = 'Pass Turn';
  pauseButton.textContent = 'Pause Game';
  winButton.textContent = 'I Win';
  confirmButton.textContent = 'Confirm Win';
  denyButton.textContent = 'Deny Claim';
  cancelButton.textContent = 'Cancel Win Claim';

  const me = currentIdentity && currentIdentity.player ? currentIdentity.player : null;
  const livingPaired = !!(me && !me.eliminated);
  if (!livingPaired) return;

  if (d.win_claim) {
    const claimant = d.win_claim.claimant;
    const mine = claimant && sameSeat(me, claimant);
    if (mine) {
      cancelButton.classList.remove('hidden');
      return;
    }

    const required = (d.win_claim.required_players || []).some(p => sameSeat(me, p));
    const confirmed = (d.win_claim.confirmed_players || []).some(p => sameSeat(me, p));
    if (required && !confirmed) {
      confirmButton.classList.remove('hidden');
      denyButton.classList.remove('hidden');
    }
    return;
  }

  if (d.state === 'RUNNING') {
    pauseButton.classList.remove('hidden');
    winButton.classList.remove('hidden');
    if (sameSeat(me, d.active_player)) passButton.classList.remove('hidden');
  } else if (d.state === 'PAUSED' && !d.elimination_target) {
    winButton.classList.remove('hidden');
  }
}

async function refreshIdentity() {
  const token = browserToken();
  if (!token) {
    currentIdentity = null;
    updateIdentityChip();
    return;
  }

  try {
    const r = await fetch('/api/web/me', {headers:authHeaders(), cache:'no-store'});
    if (r.status === 401) {
      localStorage.removeItem(WEB_TOKEN_KEY);
      currentIdentity = null;
      updateIdentityChip();
      return;
    }
    if (!r.ok) return;
    currentIdentity = await r.json();
    localStorage.removeItem(DISPLAY_ONLY_KEY);
    updateIdentityChip();
    if (latest) renderWebControls(latest);
  } catch (e) {
    // Status polling will show connection problems; keep the last identity.
  }
}

function maybePromptIdentity() {
  if (!latest || identityAutoPrompted) return;
  if (latest.state !== 'LOBBY' || !latest.players.length) return;
  if (currentIdentity || browserToken() || localStorage.getItem(DISPLAY_ONLY_KEY) === '1') return;
  identityAutoPrompted = true;
  openIdentity();
}

function closeIdentity() {
  document.getElementById('identityOverlay').classList.add('hidden');
  if (identityPollTimer) { clearTimeout(identityPollTimer); identityPollTimer = null; }
}

function identityChoiceText(p) {
  return `${playerLabel(p)} • ${seatLabel(p)}`;
}

function openIdentity() {
  if (!latest) return;
  const root = document.getElementById('identityChoices');
  const actions = document.getElementById('identityActions');
  const note = document.getElementById('identityNote');
  const status = document.getElementById('identityStatus');
  status.textContent = '';
  actions.innerHTML = '';

  if (currentIdentity && currentIdentity.player) {
    note.textContent = `This browser is paired to ${identityChoiceText(currentIdentity.player)}.`;
  } else if (latest.state === 'PAUSED') {
    note.textContent = 'Recovery mode: choose the player whose web controller needs to be reassigned. The host must approve on the physical host module.';
  } else if (latest.state === 'LOBBY') {
    note.textContent = 'Choose your seat, then press Action briefly on that physical module to confirm this browser.';
  } else {
    note.textContent = 'Player identity is locked while a game is in progress. Pause the game for host-approved controller recovery.';
  }

  if (latest.state === 'LOBBY') {
    root.innerHTML = latest.players.map(p => {
      const mine = currentIdentity && sameSeat(currentIdentity.player, p);
      const claimed = !!p.web_claimed;
      const disabled = claimed && !mine;
      const caption = mine ? 'This browser' : (claimed ? 'Already paired to another browser' : 'Tap to pair, then confirm on the physical module');
      return `<button class="choice" type="button" ${disabled || mine ? 'disabled' : ''} onclick="requestSeatClaim(${p.module_id},${p.slot})"><strong>${esc(playerLabel(p))}</strong><span>${esc(playerMeta(p))} • ${esc(caption)}</span></button>`;
    }).join('');

    if (currentIdentity) {
      actions.innerHTML += '<button class="secondary-button" type="button" onclick="releaseIdentity()">Release / change player</button>';
    }
    actions.innerHTML += '<button class="secondary-button" type="button" onclick="chooseDisplayOnly()">Display only</button>';
  } else if (latest.state === 'PAUSED') {
    root.innerHTML = latest.players.map(p => {
      const disabled = !!p.eliminated;
      const caption = disabled ? 'Eliminated' : 'Host approval required';
      return `<button class="choice" type="button" ${disabled ? 'disabled' : ''} onclick="requestSeatReassign(${p.module_id},${p.slot})"><strong>${esc(playerLabel(p))}</strong><span>${esc(playerMeta(p))} • ${esc(caption)}</span></button>`;
    }).join('');
    actions.innerHTML = '<button class="secondary-button" type="button" onclick="closeIdentity()">Cancel</button>';
  } else {
    root.innerHTML = currentIdentity ? `<div class="field"><strong>${esc(identityChoiceText(currentIdentity.player))}</strong></div>` : '<div class="field">Display only during this game.</div>';
    actions.innerHTML = '<button class="secondary-button" type="button" onclick="closeIdentity()">Close</button>';
  }

  document.getElementById('identityOverlay').classList.remove('hidden');
}

async function beginPairRequest(url, moduleId, slot) {
  const status = document.getElementById('identityStatus');
  status.textContent = 'Creating secure pairing request…';
  try {
    const r = await fetch(url, {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({module_id:moduleId, slot})
    });
    const data = await r.json();
    if (!r.ok) throw new Error(data.error || `HTTP ${r.status}`);
    status.textContent = data.message || 'Confirm on the physical module.';
    pollPairRequest(data.request_id);
  } catch (e) {
    status.textContent = e.message || 'Could not begin pairing.';
  }
}

function requestSeatClaim(moduleId, slot) {
  beginPairRequest('/api/web/claim', moduleId, slot);
}

function requestSeatReassign(moduleId, slot) {
  beginPairRequest('/api/web/reassign', moduleId, slot);
}

async function pollPairRequest(requestId) {
  if (identityPollTimer) clearTimeout(identityPollTimer);
  try {
    const r = await fetch(`/api/web/claim-status?request_id=${encodeURIComponent(requestId)}`, {cache:'no-store'});
    const data = await r.json();
    if (data.status === 'confirmed' && data.token) {
      localStorage.setItem(WEB_TOKEN_KEY, data.token);
      localStorage.removeItem(DISPLAY_ONLY_KEY);
      document.getElementById('identityStatus').textContent = 'Paired. This browser now controls only that seat.';
      await refreshIdentity();
      await refresh();
      setTimeout(closeIdentity, 650);
      return;
    }
    if (!r.ok || data.status === 'expired') {
      document.getElementById('identityStatus').textContent = data.error || 'Pairing request expired.';
      return;
    }
    document.getElementById('identityStatus').textContent = data.message || 'Waiting for physical confirmation…';
    identityPollTimer = setTimeout(() => pollPairRequest(requestId), 500);
  } catch (e) {
    document.getElementById('identityStatus').textContent = 'Waiting for TurnHub…';
    identityPollTimer = setTimeout(() => pollPairRequest(requestId), 900);
  }
}

async function releaseIdentity() {
  const status = document.getElementById('identityStatus');
  status.textContent = 'Releasing this browser…';
  try {
    const r = await fetch('/api/web/release', {method:'POST', headers:authHeaders()});
    const data = await r.json();
    if (!r.ok) throw new Error(data.error || `HTTP ${r.status}`);
    localStorage.removeItem(WEB_TOKEN_KEY);
    currentIdentity = null;
    await refresh();
    openIdentity();
  } catch (e) {
    status.textContent = e.message || 'Could not release controller.';
  }
}

function chooseDisplayOnly() {
  if (currentIdentity) {
    document.getElementById('identityStatus').textContent = 'Release this browser first if you want display-only mode.';
    return;
  }
  localStorage.setItem(DISPLAY_ONLY_KEY, '1');
  updateIdentityChip();
  closeIdentity();
}

async function passTurnFromWeb() {
  const button = document.getElementById('passButton');
  button.disabled = true;
  button.textContent = 'Passing…';
  try {
    const r = await fetch('/api/web/pass', {method:'POST', headers:authHeaders()});
    const data = await r.json();
    if (r.status === 401) {
      localStorage.removeItem(WEB_TOKEN_KEY);
      currentIdentity = null;
    }
    if (!r.ok) throw new Error(data.error || `HTTP ${r.status}`);
    await refresh();
    await refreshIdentity();
  } catch (e) {
    button.textContent = 'Pass rejected';
    setTimeout(() => { if (latest) renderWebControls(latest); }, 900);
  }
}

async function authenticatedGameAction(path, buttonId, busyText) {
  const button = document.getElementById(buttonId);
  button.disabled = true;
  const original = button.textContent;
  button.textContent = busyText;
  try {
    const r = await fetch(path, {method:'POST', headers:authHeaders()});
    const data = await r.json();
    if (r.status === 401) {
      localStorage.removeItem(WEB_TOKEN_KEY);
      currentIdentity = null;
    }
    if (!r.ok) throw new Error(data.error || `HTTP ${r.status}`);
    await refresh();
    await refreshIdentity();
  } catch (e) {
    button.textContent = e.message || 'Rejected';
    setTimeout(() => { button.textContent = original; if (latest) renderWebControls(latest); }, 1100);
  }
}

function pauseFromWeb() { return authenticatedGameAction('/api/web/pause', 'pauseButton', 'Pausing…'); }
function claimWinFromWeb() { return authenticatedGameAction('/api/web/win-claim', 'winButton', 'Requesting…'); }
function confirmWinFromWeb() { return authenticatedGameAction('/api/web/win-confirm', 'confirmWinButton', 'Confirming…'); }
function denyWinFromWeb() { return authenticatedGameAction('/api/web/win-deny', 'denyWinButton', 'Denying…'); }
function cancelWinFromWeb() { return authenticatedGameAction('/api/web/win-cancel', 'cancelWinButton', 'Cancelling…'); }

let settingsData = null;

async function openSettings() {
  const status = document.getElementById('settingsStatus');
  status.textContent = 'Loading…';
  document.getElementById('settingsOverlay').classList.remove('hidden');
  try {
    const r = await fetch('/api/settings', {cache:'no-store'});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    settingsData = await r.json();

    document.getElementById('moduleSettings').innerHTML = settingsData.module_ids.map(id => {
      const value = settingsData.module_names[String(id)] || '';
      return `<div class="field"><label>Module ${id}</label><input maxlength="40" data-module-id="${id}" value="${esc(value)}" placeholder="Module ${id}"></div>`;
    }).join('');

    const seatFields = [];
    for (const id of settingsData.module_ids) {
      for (const slot of [1,2]) {
        const key = `${id}:${slot}`;
        const value = settingsData.seat_names[key] || '';
        const slotName = slot === 1 ? 'A' : 'B';
        seatFields.push(`<div class="field"><label>Module ${id} • Seat ${slotName}</label><input maxlength="40" data-seat-key="${key}" value="${esc(value)}" placeholder="Player name"></div>`);
      }
    }
    document.getElementById('seatSettings').innerHTML = seatFields.join('');
    document.getElementById('persistenceNote').textContent = `Game state autosaves after changes and every ${settingsData.active_autosave_seconds} seconds during active play. Active games recover paused after a restart.`;
    status.textContent = '';
  } catch (e) {
    status.textContent = 'Could not load settings.';
  }
}

function closeSettings() {
  document.getElementById('settingsOverlay').classList.add('hidden');
}

async function saveSettings() {
  const status = document.getElementById('settingsStatus');
  const moduleNames = {};
  const seatNames = {};
  document.querySelectorAll('[data-module-id]').forEach(input => { moduleNames[input.dataset.moduleId] = input.value; });
  document.querySelectorAll('[data-seat-key]').forEach(input => { seatNames[input.dataset.seatKey] = input.value; });
  status.textContent = 'Saving…';
  try {
    const r = await fetch('/api/settings', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({module_names:moduleNames, seat_names:seatNames})
    });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    settingsData = await r.json();
    status.textContent = 'Names saved.';
    await refresh();
  } catch (e) {
    status.textContent = 'Could not save names.';
  }
}

async function saveGameStateNow() {
  const status = document.getElementById('settingsStatus');
  status.textContent = 'Saving game state…';
  try {
    const r = await fetch('/api/state/save', {method:'POST'});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    status.textContent = 'Game state saved.';
  } catch (e) {
    status.textContent = 'Could not save game state.';
  }
}

document.getElementById('settingsOverlay').addEventListener('click', e => {
  if (e.target.id === 'settingsOverlay') closeSettings();
});

document.getElementById('identityOverlay').addEventListener('click', e => {
  if (e.target.id === 'identityOverlay') closeIdentity();
});

async function refresh() {
  try {
    const r = await fetch('/api/status', {cache:'no-store'});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    latest = await r.json();
    fetchedAt = performance.now();
    online = true;
    document.getElementById('connDot').classList.add('online');
    document.getElementById('connText').textContent = 'Live';
    render(latest);
  } catch (e) {
    online = false;
    document.getElementById('connDot').classList.remove('online');
    document.getElementById('connText').textContent = 'Reconnecting…';
  }
}

setInterval(() => { if (latest) render(latest); }, 100);
setInterval(refresh, 1000);
setInterval(refreshIdentity, 2000);
refresh().then(refreshIdentity);
</script>
</body>
</html>'''


class _PortalHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class WebPortal:
    """Local status/settings server with physically paired player Pass control."""

    def __init__(
        self,
        turnhub,
        host: str = WEB_HOST,
        port: int = WEB_PORT,
    ) -> None:
        self.turnhub = turnhub
        self.host = host
        self.port = port
        self.server: _PortalHTTPServer | None = None
        self.thread: threading.Thread | None = None

    # ========================================================
    # Snapshot Building
    # ========================================================

    def _player_dict(self, player, stats=None) -> dict | None:
        if player is None:
            return None

        custom_name = self.turnhub.persistence.seat_name(
            player.module_id,
            player.slot,
        )

        result = {
            "player_number": player.player_number,
            "module_id": player.module_id,
            "slot": player.slot,
            "slot_name": player.slot_name,
            "display_name": custom_name or f"Player {player.player_number}",
            "has_custom_name": custom_name is not None,
            "module_name": self.turnhub.persistence.module_name(player.module_id),
            "web_claimed": player.seat_key in self.turnhub.web_control.claimed_seats(),
            "eliminated": bool(
                self.turnhub.game.players
                and self.turnhub.game.is_eliminated(player.player_number)
            ),
        }

        if stats is not None:
            result["turns_completed"] = stats.turns_completed
            result["completed_turn_seconds"] = stats.total_turn_seconds
        else:
            result["turns_completed"] = 0
            result["completed_turn_seconds"] = 0.0

        return result

    def status_snapshot(self) -> dict:
        """Return a JSON-safe, read-only snapshot of current TurnHub state."""

        hub = self.turnhub
        now = time.monotonic()
        connected = set(hub.serial.connected_modules())

        if hub.game.players:
            players = [
                self._player_dict(
                    player,
                    hub.game.stats.get(player.player_number),
                )
                for player in list(hub.game.players)
            ]
        else:
            players = [
                self._player_dict(player)
                for player in list(hub.lobby.players)
            ]

        starter = None
        if hub.game.players:
            starter = self._player_dict(
                hub.game.player_by_number(hub.game.starter_player)
            )
        else:
            starter = self._player_dict(
                hub.lobby.selected_starter_player
            )

        active = self._player_dict(hub.game.active_player)
        winner = self._player_dict(
            hub.game.player_by_number(hub.game.winner_player)
        )
        elimination_target = self._player_dict(
            hub.game.player_by_number(hub.elimination_target_player)
        )

        win_claim = None
        if hub.game.has_win_claim:
            claimant = hub.game.player_by_number(hub.game.win_claim_player)
            required_players = [
                self._player_dict(hub.game.player_by_number(number))
                for number in hub.game.win_claim_required
            ]
            confirmed_players = [
                self._player_dict(hub.game.player_by_number(number))
                for number in hub.game.win_claim_confirmed
            ]
            required_players = [p for p in required_players if p is not None]
            confirmed_players = [p for p in confirmed_players if p is not None]
            next_confirmation = self._player_dict(
                hub.game.player_by_number(
                    hub.game.next_win_confirmation_player
                )
            )
            win_claim = {
                "claimant": self._player_dict(claimant),
                "required_players": required_players,
                "confirmed_players": confirmed_players,
                "required_count": len(required_players),
                "confirmed_count": len(confirmed_players),
                "next_confirmation": next_confirmation,
                "restore_state": hub.game.win_claim_restore_state,
            }

        countdown_remaining = 0.0
        if (
            hub.state == STATE_STARTING
            and hub.start_countdown_at is not None
        ):
            countdown_remaining = max(
                0.0,
                START_COUNTDOWN_SECONDS
                - (now - hub.start_countdown_at),
            )

        warning_phase = "NORMAL"
        if hub.state in (STATE_RUNNING, STATE_PAUSED):
            warning_phase = hub.game.warning_phase(now)

        active_turn_number = 0
        if hub.game.active_player is not None:
            stat = hub.game.stats.get(
                hub.game.active_player.player_number
            )
            if stat is not None:
                active_turn_number = stat.turns_completed + 1

        display_warning_ms = (
            hub.game.current_warning_ms
            if hub.game.players
            else hub.warning_ms
        )

        if display_warning_ms == WARNING_OFF:
            timer_setting = "DISABLED (green at 5 minutes)"
        else:
            timer_minutes = display_warning_ms // 60_000
            timer_setting = f"{timer_minutes} MINUTES"

        if (
            hub.state == STATE_PAUSED
            and hub.game.pause_started_at is not None
        ):
            game_elapsed_seconds = max(
                0.0,
                hub.game.pause_started_at
                - hub.game.game_started_at
                - hub.game.total_paused_seconds,
            )
            paused_seconds = (
                hub.game.total_paused_seconds
                + max(0.0, now - hub.game.pause_started_at)
            )
        else:
            game_elapsed_seconds = (
                hub.game.game_elapsed(now)
                if hub.game.players
                else 0.0
            )
            paused_seconds = hub.game.total_paused_seconds

        return {
            "state": hub.state,
            "generated_at_unix": time.time(),
            "timer_setting": timer_setting,
            "warning_ms": display_warning_ms,
            "warning_off": WARNING_OFF,
            "warning_off_green_ms": WARNING_OFF_GREEN_MS,
            "warning_caution_fraction": WARNING_CAUTION_FRACTION,
            "warning_phase": warning_phase,
            "host_module": hub.lobby.host_module,
            "host_module_name": (
                hub.persistence.module_name(hub.lobby.host_module)
                if hub.lobby.host_module is not None
                else None
            ),
            "starter": starter,
            "active_player": active,
            "winner": winner,
            "elimination_target": elimination_target,
            "win_claim": win_claim,
            "eliminated_players": list(hub.game.eliminated_players),
            "active_turn_number": active_turn_number,
            "turn_elapsed_seconds": hub.game.current_turn_elapsed(now)
            if hub.game.players
            else 0.0,
            "game_elapsed_seconds": game_elapsed_seconds,
            "paused_seconds": paused_seconds,
            "countdown_remaining_seconds": countdown_remaining,
            "players": players,
            "modules": [
                {
                    "module_id": module_id,
                    "module_name": hub.persistence.module_name(module_id),
                    "connected": module_id in connected,
                }
                for module_id in MODULE_IDS
            ],
        }

    def identity_snapshot(self, token: str | None) -> dict | None:
        identity = self.turnhub.web_control.identity_for_token(token)
        if identity is None:
            return None

        seat_key = tuple(identity["seat_key"])
        player = next(
            (
                player
                for player in (self.turnhub.game.players or self.turnhub.lobby.players)
                if player.seat_key == seat_key
            ),
            None,
        )

        if player is None:
            return None

        return {
            "player": self._player_dict(
                player,
                self.turnhub.game.stats.get(player.player_number)
                if self.turnhub.game.players
                else None,
            ),
            "can_pass": bool(identity.get("can_pass")),
            "can_pause": bool(
                self.turnhub.state == STATE_RUNNING
                and not self.turnhub.game.is_eliminated(player.player_number)
            ),
            "state": self.turnhub.state,
        }

    # ========================================================
    # HTTP Server
    # ========================================================

    def _handler_class(self):
        portal = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, format, *args):
                # Avoid returning web request noise to the Pi console.
                return

            def _send_bytes(
                self,
                body: bytes,
                content_type: str,
                status: HTTPStatus = HTTPStatus.OK,
            ) -> None:
                self.send_response(status.value)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.send_header("X-Content-Type-Options", "nosniff")
                self.send_header("Referrer-Policy", "no-referrer")
                self.end_headers()
                self.wfile.write(body)

            def _send_json(
                self,
                payload,
                status: HTTPStatus = HTTPStatus.OK,
            ) -> None:
                body = json.dumps(
                    payload,
                    separators=(",", ":"),
                ).encode("utf-8")
                self._send_bytes(
                    body,
                    "application/json; charset=utf-8",
                    status,
                )

            def _read_json(self):
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                except ValueError:
                    return None

                if length < 0 or length > 32768:
                    return None

                try:
                    raw = self.rfile.read(length) if length else b"{}"
                    value = json.loads(raw.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    return None

                return value if isinstance(value, dict) else None

            def _bearer_token(self) -> str | None:
                value = self.headers.get("Authorization", "").strip()
                if not value.lower().startswith("bearer "):
                    return None
                token = value[7:].strip()
                return token or None

            def do_GET(self):
                parsed = urlparse(self.path)
                path = parsed.path

                if path == "/":
                    self._send_bytes(
                        PORTAL_HTML.encode("utf-8"),
                        "text/html; charset=utf-8",
                    )
                    return

                if path == "/api/status":
                    try:
                        payload = json.dumps(
                            portal.status_snapshot(),
                            separators=(",", ":"),
                        ).encode("utf-8")
                    except Exception as exc:
                        payload = json.dumps({
                            "error": "status unavailable",
                            "detail": str(exc),
                        }).encode("utf-8")
                        self._send_bytes(
                            payload,
                            "application/json; charset=utf-8",
                            HTTPStatus.INTERNAL_SERVER_ERROR,
                        )
                        return

                    self._send_bytes(
                        payload,
                        "application/json; charset=utf-8",
                    )
                    return

                if path == "/api/settings":
                    payload = portal.turnhub.persistence.settings_snapshot()
                    payload["module_ids"] = list(MODULE_IDS)
                    payload["active_autosave_seconds"] = 10
                    self._send_json(payload)
                    return

                if path == "/api/web/me":
                    identity = portal.identity_snapshot(self._bearer_token())
                    if identity is None:
                        self._send_json(
                            {"error": "invalid web-controller token"},
                            HTTPStatus.UNAUTHORIZED,
                        )
                        return
                    self._send_json(identity)
                    return

                if path == "/api/web/claim-status":
                    query = parse_qs(parsed.query)
                    request_id = (query.get("request_id") or [""])[0]
                    if not request_id:
                        self._send_json(
                            {"error": "request_id is required"},
                            HTTPStatus.BAD_REQUEST,
                        )
                        return
                    payload, status_code = portal.turnhub.web_control.claim_status(request_id)
                    self._send_json(payload, HTTPStatus(status_code))
                    return

                if path == "/health":
                    self._send_bytes(
                        b"ok\n",
                        "text/plain; charset=utf-8",
                    )
                    return

                self._send_bytes(
                    b"Not found\n",
                    "text/plain; charset=utf-8",
                    HTTPStatus.NOT_FOUND,
                )

            def do_POST(self):
                path = urlparse(self.path).path

                if path in ("/api/web/claim", "/api/web/reassign"):
                    payload = self._read_json()
                    if payload is None:
                        self._send_json({"error": "invalid JSON"}, HTTPStatus.BAD_REQUEST)
                        return
                    try:
                        seat_key = (int(payload.get("module_id")), int(payload.get("slot")))
                    except (TypeError, ValueError):
                        self._send_json({"error": "module_id and slot are required"}, HTTPStatus.BAD_REQUEST)
                        return
                    if seat_key[1] not in (1, 2):
                        self._send_json({"error": "slot must be 1 or 2"}, HTTPStatus.BAD_REQUEST)
                        return

                    if path.endswith("/reassign"):
                        _ok, result, status_code = portal.turnhub.web_control.request_paused_reassignment(seat_key)
                    else:
                        _ok, result, status_code = portal.turnhub.web_control.request_lobby_claim(seat_key)

                    self._send_json(result, HTTPStatus(status_code))
                    return

                if path == "/api/web/release":
                    _ok, result, status_code = portal.turnhub.web_control.release_token(self._bearer_token())
                    self._send_json(result, HTTPStatus(status_code))
                    return

                if path == "/api/web/pass":
                    ok, seat_key, reason = portal.turnhub.web_control.authorize_pass(self._bearer_token())
                    if not ok or seat_key is None:
                        status = HTTPStatus.UNAUTHORIZED if reason == "invalid web-controller token" else HTTPStatus.FORBIDDEN
                        self._send_json({"error": reason}, status)
                        return

                    if not portal.turnhub.on_web_pass(seat_key[0], seat_key[1]):
                        self._send_json(
                            {"error": "turn changed before the pass could be accepted"},
                            HTTPStatus.CONFLICT,
                        )
                        return

                    self._send_json({"passed": True})
                    return

                if path in (
                    "/api/web/pause",
                    "/api/web/win-claim",
                    "/api/web/win-confirm",
                    "/api/web/win-deny",
                    "/api/web/win-cancel",
                ):
                    ok, player, reason = portal.turnhub.web_control.authorize_living_player(
                        self._bearer_token()
                    )
                    if not ok or player is None:
                        status = HTTPStatus.UNAUTHORIZED if reason == "invalid web-controller token" else HTTPStatus.FORBIDDEN
                        self._send_json({"error": reason}, status)
                        return

                    if path == "/api/web/pause":
                        accepted = portal.turnhub.on_web_pause(player.player_number)
                        action = "pause"
                    elif path == "/api/web/win-claim":
                        accepted = portal.turnhub.on_web_win_claim(player.player_number)
                        action = "win claim"
                    elif path == "/api/web/win-confirm":
                        accepted = portal.turnhub.on_web_win_confirm(player.player_number)
                        action = "win confirmation"
                    elif path == "/api/web/win-deny":
                        accepted = portal.turnhub.on_web_win_deny(player.player_number)
                        action = "win denial"
                    else:
                        accepted = portal.turnhub.on_web_win_cancel(player.player_number)
                        action = "win cancellation"

                    if not accepted:
                        self._send_json(
                            {"error": f"{action} is not allowed in the current state"},
                            HTTPStatus.CONFLICT,
                        )
                        return

                    self._send_json({"accepted": True, "action": action})
                    return

                if path == "/api/settings":
                    payload = self._read_json()
                    if payload is None:
                        self._send_json(
                            {"error": "invalid JSON"},
                            HTTPStatus.BAD_REQUEST,
                        )
                        return

                    module_names = payload.get("module_names")
                    seat_names = payload.get("seat_names")
                    if not isinstance(module_names, dict) or not isinstance(seat_names, dict):
                        self._send_json(
                            {"error": "module_names and seat_names must be objects"},
                            HTTPStatus.BAD_REQUEST,
                        )
                        return

                    try:
                        result = portal.turnhub.persistence.update_names(
                            module_names=module_names,
                            seat_names=seat_names,
                        )
                    except OSError as exc:
                        self._send_json(
                            {"error": "could not save settings", "detail": str(exc)},
                            HTTPStatus.INTERNAL_SERVER_ERROR,
                        )
                        return

                    result["module_ids"] = list(MODULE_IDS)
                    result["active_autosave_seconds"] = 10
                    self._send_json(result)
                    return

                if path == "/api/state/save":
                    try:
                        saved = portal.turnhub.persistence.save_session(
                            portal.turnhub
                        )
                    except OSError as exc:
                        self._send_json(
                            {"error": "could not save game state", "detail": str(exc)},
                            HTTPStatus.INTERNAL_SERVER_ERROR,
                        )
                        return

                    self._send_json({"saved": True, "path": str(saved)})
                    return

                self._send_bytes(
                    b"Not found\n",
                    "text/plain; charset=utf-8",
                    HTTPStatus.NOT_FOUND,
                )

        return Handler

    @staticmethod
    def _display_hostname() -> str:
        hostname = socket.gethostname().strip().lower()
        return hostname or "turnhub"

    def start(self) -> bool:
        if self.server is not None:
            return True

        try:
            self.server = _PortalHTTPServer(
                (self.host, self.port),
                self._handler_class(),
            )
        except OSError as exc:
            print(f"[WEB] Could not start portal: {exc}")
            self.server = None
            return False

        # Useful when port=0 is supplied in tests.
        self.port = int(self.server.server_address[1])

        self.thread = threading.Thread(
            target=self.server.serve_forever,
            kwargs={"poll_interval": 0.5},
            daemon=True,
            name="TurnHub-Web",
        )
        self.thread.start()

        print(
            "[WEB] Local portal: "
            f"http://{self._display_hostname()}.local:{self.port}/"
        )
        print(
            "[WEB] Listening on all network interfaces "
            f"at port {self.port}."
        )

        return True

    def stop(self) -> None:
        server = self.server
        if server is None:
            return

        self.server = None

        try:
            server.shutdown()
        finally:
            server.server_close()

        thread = self.thread
        self.thread = None

        if thread is not None and thread.is_alive():
            thread.join(timeout=2.0)
