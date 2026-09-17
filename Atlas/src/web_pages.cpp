#include "web_pages.h"

namespace TurnHubWeb {

const char PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#111318">
<title>TurnHub</title>
<style>
:root {
  color-scheme: dark;
  --bg:#0b0d11;
  --panel:#141820;
  --panel2:#1b202a;
  --line:#2b3240;
  --text:#f3f5f7;
  --muted:#9ca6b7;
  --good:#62d58a;
  --warn:#efc55a;
  --bad:#ef6a6a;
  --blue:#72a7ff;
}
*{box-sizing:border-box}
html,body{margin:0;min-height:100%;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
body{padding:max(16px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) max(18px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left))}
.shell{width:min(1100px,100%);margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:16px}
.brand{font-size:clamp(1.35rem,4vw,2rem);font-weight:800;letter-spacing:.03em}
.header-right{display:flex;align-items:center;gap:8px;flex-wrap:wrap;justify-content:flex-end}
.connection{display:flex;align-items:center;gap:8px;color:var(--muted);font-size:.9rem}
.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);box-shadow:0 0 0 4px rgba(239,106,106,.1)}
.dot.online{background:var(--good);box-shadow:0 0 0 4px rgba(98,213,138,.1)}
.nav-button{border:1px solid var(--line);background:var(--panel);color:var(--text);border-radius:11px;padding:8px 11px;text-decoration:none;font-size:.84rem;font-weight:750}
.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:12px}
.card{grid-column:span 12;background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 12px 35px rgba(0,0,0,.18)}
.hero{min-height:270px;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center}
.eyebrow{color:var(--muted);font-size:.85rem;font-weight:700;letter-spacing:.12em;text-transform:uppercase}
.hero-title{margin:.25rem 0 .2rem;font-size:clamp(3rem,13vw,7.5rem);line-height:.94;font-weight:900;letter-spacing:-.04em}
.hero-sub{color:var(--muted);font-size:clamp(1rem,3vw,1.35rem)}
.badges{display:flex;flex-wrap:wrap;justify-content:center;gap:8px;margin-top:15px}
.badge{border:1px solid var(--line);background:var(--panel2);border-radius:999px;padding:7px 11px;color:var(--muted);font-size:.85rem;font-weight:700}
.badge.good{color:var(--good);border-color:rgba(98,213,138,.35)}
.badge.warn{color:var(--warn);border-color:rgba(239,197,90,.35)}
.badge.bad{color:var(--bad);border-color:rgba(239,106,106,.35)}
.badge.blue{color:var(--blue);border-color:rgba(114,167,255,.35)}
.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.metric{background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:13px}
.metric-label{color:var(--muted);font-size:.78rem;font-weight:700;letter-spacing:.07em;text-transform:uppercase}
.metric-value{margin-top:5px;font-size:1.18rem;font-weight:800;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.section-title{margin:0 0 12px;font-size:.9rem;color:var(--muted);text-transform:uppercase;letter-spacing:.1em}
.players{display:grid;grid-template-columns:repeat(auto-fit,minmax(175px,1fr));gap:10px}
.player{background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:14px;transition:border-color .15s ease,opacity .15s ease}
.player.active{border-color:var(--blue);box-shadow:0 0 0 1px rgba(114,167,255,.25) inset}
.player.winner{border-color:var(--good);box-shadow:0 0 0 1px rgba(98,213,138,.18) inset}
.player.target{border-color:var(--bad)}
.player.confirm{border-color:var(--good)}
.player-name{font-size:1.2rem;font-weight:850}
.player-seat{color:var(--muted);font-size:.85rem;margin-top:4px}
.player-status{margin-top:12px;font-size:.85rem;color:var(--muted);font-weight:700}
.decision{border-left:3px solid var(--warn);background:var(--panel2);border-radius:12px;padding:13px 14px;color:var(--muted);line-height:1.45}
.decision strong{color:var(--text)}
.footer{color:var(--muted);text-align:center;font-size:.78rem;margin-top:14px}
.hidden{display:none!important}
@media(min-width:760px){.hero-card{grid-column:span 8}.metrics-card{grid-column:span 4}.players-card{grid-column:span 8}.system-card{grid-column:span 4}.metrics{grid-template-columns:1fr}}
@media(max-width:540px){header{align-items:flex-start}.header-right{max-width:58%}.hero{min-height:230px}.card{padding:15px}}
</style>
</head>
<body>
<div class="shell">
  <header>
    <div class="brand">TurnHub</div>
    <div class="header-right">
      <div class="connection"><span id="dot" class="dot"></span><span id="connection">Connecting</span></div>
      <a class="nav-button" href="/dev">Dev</a>
      <a class="nav-button" href="/update">Firmware</a>
    </div>
  </header>

  <div class="grid">
    <section class="card hero hero-card">
      <div id="eyebrow" class="eyebrow">TURNHUB</div>
      <div id="heroTitle" class="hero-title">READY</div>
      <div id="heroSub" class="hero-sub">Waiting for Atlas</div>
      <div id="heroBadges" class="badges"></div>
    </section>

    <section class="card metrics-card">
      <h2 class="section-title">Table</h2>
      <div class="metrics">
        <div class="metric"><div class="metric-label">Players</div><div id="playersMetric" class="metric-value">0</div></div>
        <div class="metric"><div class="metric-label">Online Sigils</div><div id="sigilsMetric" class="metric-value">0</div></div>
        <div class="metric"><div class="metric-label">Host</div><div id="hostMetric" class="metric-value">None</div></div>
        <div class="metric"><div class="metric-label">Firmware</div><div id="firmwareMetric" class="metric-value">—</div></div>
      </div>
    </section>

    <section class="card players-card">
      <h2 class="section-title">Players</h2>
      <div id="players" class="players"></div>
    </section>

    <section class="card system-card">
      <h2 class="section-title">Status</h2>
      <div id="decision" class="decision">Physical Sigils control the table. Browser controls will return as the portal migration continues.</div>
      <div class="metrics" style="margin-top:10px">
        <div class="metric"><div class="metric-label">State</div><div id="stateMetric" class="metric-value">—</div></div>
        <div class="metric"><div class="metric-label">ESP-NOW</div><div id="espMetric" class="metric-value">—</div></div>
        <div class="metric"><div class="metric-label">Master Button</div><div id="masterMetric" class="metric-value">Released</div></div>
        <div class="metric"><div class="metric-label">Starter</div><div id="starterMetric" class="metric-value">None</div></div>
      </div>
    </section>
  </div>

  <div class="footer">TurnHub Atlas · ESP32 portal migration</div>
</div>
<script>
let latest=null;

function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function badge(text,cls=''){return `<span class="badge ${cls}">${esc(text)}</span>`;}
function playerName(n){return n?`Player ${n}`:'None';}
function hostName(h){return Number(h)>=0?`Sigil ${Number(h)+1}`:'None';}

function renderHero(s){
  const eye=document.getElementById('eyebrow');
  const title=document.getElementById('heroTitle');
  const sub=document.getElementById('heroSub');
  const badges=document.getElementById('heroBadges');
  let html='';

  if(s.state==='LOBBY'){
    eye.textContent='LOBBY';
    title.textContent=s.players?`${s.players} PLAYER${s.players===1?'':'S'}`:'READY';
    sub.textContent=s.starter?`${playerName(s.starter)} selected to start`:'Press Action on a Sigil to join';
    if(s.starter) html+=badge(`Starter: ${playerName(s.starter)}`,'blue');
    if(Number(s.host)>=0) html+=badge(`${hostName(s.host)} host`,'good');
  }else if(s.state==='STARTING'){
    eye.textContent='STARTING';
    title.textContent='STARTING';
    sub.textContent=s.starter?`${playerName(s.starter)} goes first`:'Game countdown active';
    html+=badge('Countdown','blue');
  }else if(s.state==='RUNNING'){
    eye.textContent='ACTIVE TURN';
    title.textContent=s.active?playerName(s.active).toUpperCase():'RUNNING';
    sub.textContent='Physical Sigil controls active turn';
    html+=badge('Game running','good');
  }else if(s.state==='PAUSED'){
    if(s.winConfirm){
      eye.textContent='VICTORY CLAIM';
      title.textContent=playerName(s.winConfirm).toUpperCase();
      sub.textContent='Waiting for this player to confirm or deny';
      html+=badge('Action = confirm','good')+badge('Pass = deny','bad');
    }else if(s.eliminationTarget){
      eye.textContent='ELIMINATION SELECTED';
      title.textContent=playerName(s.eliminationTarget).toUpperCase();
      sub.textContent='Pass confirms the selected elimination';
      html+=badge('Game paused','warn');
    }else{
      eye.textContent='GAME PAUSED';
      title.textContent='PAUSED';
      sub.textContent=s.active?`${playerName(s.active)} remains active`:'Game clock stopped';
      html+=badge('Timer stopped','warn');
    }
  }else if(s.state==='GAME_OVER'){
    eye.textContent='GAME OVER';
    title.textContent=s.winner?playerName(s.winner).toUpperCase():'COMPLETE';
    sub.textContent=s.winner?`${playerName(s.winner)} wins`:'Game complete';
    html+=badge('Winner','good');
  }else{
    eye.textContent='TURNHUB';
    title.textContent=s.state||'—';
    sub.textContent='';
  }
  badges.innerHTML=html;
}

function renderPlayers(s){
  const root=document.getElementById('players');
  if(!s.players){
    root.innerHTML='<div class="player"><div class="player-name">Waiting for players</div><div class="player-seat">Join from a physical Sigil</div></div>';
    return;
  }
  let html='';
  for(let n=1;n<=Number(s.players);n++){
    const classes=['player'];
    const statuses=[];
    if(Number(s.active)===n){classes.push('active');statuses.push('Active turn');}
    if(Number(s.winner)===n){classes.push('winner');statuses.push('Winner');}
    if(Number(s.eliminationTarget)===n){classes.push('target');statuses.push('Elimination selected');}
    if(Number(s.winConfirm)===n){classes.push('confirm');statuses.push('Awaiting victory vote');}
    if(Number(s.starter)===n && s.state==='LOBBY'){statuses.push('Selected starter');}
    html+=`<div class="${classes.join(' ')}"><div class="player-name">Player ${n}</div><div class="player-seat">Logical table seat</div><div class="player-status">${esc(statuses.join(' · ')||'Ready')}</div></div>`;
  }
  root.innerHTML=html;
}

function renderDecision(s){
  const el=document.getElementById('decision');
  if(s.winConfirm){
    el.innerHTML=`<strong>Victory claim:</strong> ${esc(playerName(s.winConfirm))} is next to vote. Action confirms; Pass denies.`;
  }else if(s.eliminationTarget){
    el.innerHTML=`<strong>Elimination:</strong> ${esc(playerName(s.eliminationTarget))} is selected. Pass on that Sigil confirms.`;
  }else if(s.state==='GAME_OVER'){
    el.innerHTML='<strong>Game complete.</strong> Host Action Short starts a rematch. Host Action Long clears the table.';
  }else{
    el.textContent='Physical Sigils control the table. Browser controls will return as the portal migration continues.';
  }
}

function render(s){
  latest=s;
  document.getElementById('playersMetric').textContent=s.players;
  document.getElementById('sigilsMetric').textContent=s.sigils;
  document.getElementById('hostMetric').textContent=hostName(s.host);
  document.getElementById('firmwareMetric').textContent=`v${s.firmware||'?'}`;
  document.getElementById('stateMetric').textContent=s.state||'—';
  document.getElementById('espMetric').textContent=s.espNow?'Ready':'Error';
  document.getElementById('masterMetric').textContent=s.masterButton?'Pressed':'Released';
  document.getElementById('starterMetric').textContent=playerName(s.starter);
  renderHero(s);
  renderPlayers(s);
  renderDecision(s);
}

async function refresh(){
  try{
    const response=await fetch('/api/status',{cache:'no-store'});
    if(!response.ok) throw new Error('status');
    const s=await response.json();
    render(s);
    document.getElementById('dot').classList.add('online');
    document.getElementById('connection').textContent='Atlas online';
  }catch(_){
    document.getElementById('dot').classList.remove('online');
    document.getElementById('connection').textContent='Reconnecting';
  }
}
refresh();
setInterval(refresh,400);
</script>
</body>
</html>
)HTML";

const char DEV_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#111318">
  <title>TurnHub Atlas Dev</title>
  <style>
    :root { color-scheme: dark; }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      padding: 16px;
      font-family: system-ui, sans-serif;
      background: #0b0d11;
      color: #f3f5f7;
    }
    main {
      width: min(580px, 100%);
      padding: 28px;
      border: 1px solid #2b3240;
      border-radius: 18px;
      background: #141820;
      box-shadow: 0 18px 60px rgba(0,0,0,.28);
    }
    h1 { margin: 0 0 4px; }
    .sub { color:#9ca6b7; margin-bottom:18px; }
    .status {
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 12px;
      padding: 12px 0;
      border-bottom: 1px solid #2b3240;
    }
    .status:last-of-type { border-bottom: 0; }
    .value { font-weight: 800; }
    .online { color: #62d58a; }
    .pressed { color: #72a7ff; }
    .muted { color: #9ca6b7; }
    .actions { display:flex; gap:10px; margin-top:20px; flex-wrap:wrap; }
    .button {
      display:inline-block;
      padding:10px 14px;
      border-radius:10px;
      background:#e7ebf2;
      color:#111318;
      font-weight:800;
      text-decoration:none;
    }
  </style>
</head>
<body>
  <main>
    <h1>TurnHub Atlas Dev</h1>
    <div class="sub">ESP32 migration diagnostics · <span id="firmware">loading</span></div>
    <div class="status"><span>Atlas</span><span class="value online">Online</span></div>
    <div class="status"><span>State</span><span id="state" class="value">LOBBY</span></div>
    <div class="status"><span>Online Sigils</span><span id="sigils" class="value">0</span></div>
    <div class="status"><span>Players</span><span id="players" class="value">0</span></div>
    <div class="status"><span>Host Sigil</span><span id="host" class="value muted">None</span></div>
    <div class="status"><span>Starter</span><span id="starter" class="value muted">None</span></div>
    <div class="status"><span>Active Player</span><span id="active" class="value muted">None</span></div>
    <div class="status"><span>Winner</span><span id="winner" class="value muted">None</span></div>
    <div class="status"><span>Elimination Target</span><span id="elimination" class="value muted">None</span></div>
    <div class="status"><span>Win Confirmation</span><span id="winconfirm" class="value muted">None</span></div>
    <div class="status"><span>Master Button</span><span id="button" class="value muted">Released</span></div>
    <div class="status"><span>ESP-NOW</span><span id="espnow" class="value">Starting</span></div>
    <div class="actions">
      <a class="button" href="/">Main Portal</a>
      <a class="button" href="/update">Atlas Firmware Update</a>
    </div>
  </main>
  <script>
    function showPlayer(id,value){
      const el=document.getElementById(id);
      if(!value){el.textContent='None';el.className='value muted';}
      else{el.textContent='Player '+value;el.className='value';}
    }
    function showHost(value){
      const el=document.getElementById('host');
      if(value<0){el.textContent='None';el.className='value muted';}
      else{el.textContent='Sigil '+(Number(value)+1);el.className='value';}
    }
    async function refresh(){
      try{
        const response=await fetch('/api/status',{cache:'no-store'});
        const s=await response.json();
        document.getElementById('state').textContent=s.state;
        document.getElementById('sigils').textContent=s.sigils;
        document.getElementById('players').textContent=s.players;
        document.getElementById('firmware').textContent='v'+s.firmware;
        showHost(s.host);
        showPlayer('starter',s.starter);
        showPlayer('active',s.active);
        showPlayer('winner',s.winner);
        showPlayer('elimination',s.eliminationTarget);
        showPlayer('winconfirm',s.winConfirm);
        const button=document.getElementById('button');
        button.textContent=s.masterButton?'Pressed / OTA Armed':'Released';
        button.className=s.masterButton?'value pressed':'value muted';
        document.getElementById('espnow').textContent=s.espNow?'Ready':'Error';
      }catch(_){document.getElementById('espnow').textContent='Disconnected';}
    }
    refresh();
    setInterval(refresh,250);
  </script>
</body>
</html>
)HTML";

}  // namespace TurnHubWeb
