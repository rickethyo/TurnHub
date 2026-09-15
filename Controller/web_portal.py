"""Read-only local web status portal for TurnHub."""

from __future__ import annotations

import json
import socket
import threading
import time

from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

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
  <div class="connection"><span id="connDot" class="dot"></span><span id="connText">Connecting…</span></div>
</header>

<div class="grid">
  <section class="card hero-card hero">
    <div id="eyebrow" class="eyebrow">TURNHUB</div>
    <div id="heroTitle" class="hero-title">…</div>
    <div id="heroSub" class="hero-sub"></div>
    <div id="turnTimer" class="timer hidden">00:00</div>
    <div id="heroBadges" class="badges"></div>
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

<div class="footer">Read-only local TurnHub display</div>
</div>

<script>
let latest = null;
let fetchedAt = performance.now();
let online = false;

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
  return `Player ${p.player_number}`;
}

function seatLabel(p) {
  if (!p) return '';
  return `Module ${p.module_id}${p.slot_name ? ' • Seat ' + p.slot_name : ''}`;
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
    eye.textContent = 'GAME PAUSED';
    title.textContent = 'PAUSED';
    sub.textContent = d.active_player ? `${playerLabel(d.active_player)} • ${seatLabel(d.active_player)}` : '';
    timer.classList.remove('hidden');
    timer.textContent = fmt(d.turn_elapsed_seconds);
    badges.innerHTML = badge('Timer stopped', 'warn');
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
    let cls = 'player';
    if (active) cls += ' active';
    if (winner) cls += ' winner';
    const flags = [];
    if (active) flags.push(badge('ACTIVE','blue'));
    if (starter) flags.push(badge('STARTER','blue'));
    if (winner) flags.push(badge('WINNER','good'));
    return `<div class="${cls}">
      <div class="player-head"><div><div class="player-name">Player ${p.player_number}</div><div class="player-seat">Module ${p.module_id} • Seat ${esc(p.slot_name)}</div></div><div>${flags.join(' ')}</div></div>
      <div class="player-stat"><span>Completed turns</span><strong>${p.turns_completed ?? 0}</strong></div>
      <div class="player-stat"><span>Turn time total</span><strong>${fmt(p.completed_turn_seconds ?? 0)}</strong></div>
    </div>`;
  }).join('');
}

function renderModules(d) {
  document.getElementById('modules').innerHTML = d.modules.map(m =>
    `<div class="module ${m.connected ? 'online' : ''}"><span class="mini-dot"></span><strong>Module ${m.module_id}</strong><span>${m.connected ? 'Online' : 'Offline'}</span></div>`
  ).join('');
}

function render(d) {
  document.getElementById('stateValue').textContent = d.state.replaceAll('_',' ');
  const gameRunning = d.state === 'RUNNING';
  document.getElementById('gameTime').textContent = fmt(dynamicSeconds(d.game_elapsed_seconds, gameRunning));
  document.getElementById('timerSetting').textContent = d.timer_setting;
  document.getElementById('hostValue').textContent = d.host_module === null ? 'None' : `Module ${d.host_module}`;
  renderHero(d);
  renderPlayers(d);
  renderModules(d);
}

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
refresh();
</script>
</body>
</html>'''


class _PortalHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class WebPortal:
    """Small read-only HTTP server embedded in the TurnHub process."""

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

    @staticmethod
    def _player_dict(player, stats=None) -> dict | None:
        if player is None:
            return None

        result = {
            "player_number": player.player_number,
            "module_id": player.module_id,
            "slot": player.slot,
            "slot_name": player.slot_name,
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
            "starter": starter,
            "active_player": active,
            "winner": winner,
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
                    "connected": module_id in connected,
                }
                for module_id in MODULE_IDS
            ],
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

            def do_GET(self):
                path = self.path.split("?", 1)[0]

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
            "[WEB] Read-only portal: "
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
