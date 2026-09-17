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
:root{
  color-scheme:dark;
  --bg:#0b0d11;
  --panel:#141820;
  --panel2:#1b202a;
  --panel3:#11151c;
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
body{padding:max(16px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) max(20px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left))}
.shell{width:min(1180px,100%);margin:0 auto}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:16px}
.brand-wrap{display:flex;align-items:baseline;gap:10px;flex-wrap:wrap}
.brand{font-size:clamp(1.4rem,4vw,2rem);font-weight:900;letter-spacing:.025em}
.brand-sub{font-size:.82rem;color:var(--muted);font-weight:700}
.header-right{display:flex;align-items:center;gap:8px;flex-wrap:wrap;justify-content:flex-end}
.connection{display:flex;align-items:center;gap:8px;color:var(--muted);font-size:.88rem;font-weight:700}
.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);box-shadow:0 0 0 4px rgba(239,106,106,.1)}
.dot.online{background:var(--good);box-shadow:0 0 0 4px rgba(98,213,138,.1)}
.nav-button{border:1px solid var(--line);background:var(--panel);color:var(--text);border-radius:11px;padding:8px 11px;text-decoration:none;font-size:.82rem;font-weight:800}
.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:12px}
.card{grid-column:span 12;background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 12px 35px rgba(0,0,0,.18)}
.hero{min-height:285px;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;position:relative;overflow:hidden}
.hero:before{content:"";position:absolute;inset:auto -20% -70% -20%;height:85%;background:radial-gradient(circle,rgba(114,167,255,.12),transparent 65%);pointer-events:none}
.eyebrow{color:var(--muted);font-size:.82rem;font-weight:800;letter-spacing:.14em;text-transform:uppercase}
.hero-title{margin:.28rem 0 .18rem;font-size:clamp(3rem,12vw,7rem);line-height:.94;font-weight:950;letter-spacing:-.045em;position:relative}
.hero-sub{color:var(--muted);font-size:clamp(1rem,3vw,1.25rem);position:relative}
.badges{display:flex;flex-wrap:wrap;justify-content:center;gap:8px;margin-top:16px;position:relative}
.badge{border:1px solid var(--line);background:var(--panel2);border-radius:999px;padding:7px 11px;color:var(--muted);font-size:.82rem;font-weight:800}
.badge.good{color:var(--good);border-color:rgba(98,213,138,.35)}
.badge.warn{color:var(--warn);border-color:rgba(239,197,90,.35)}
.badge.bad{color:var(--bad);border-color:rgba(239,106,106,.35)}
.badge.blue{color:var(--blue);border-color:rgba(114,167,255,.35)}
.section-title{margin:0 0 12px;font-size:.84rem;color:var(--muted);text-transform:uppercase;letter-spacing:.11em}
.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.metric{background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:13px;min-width:0}
.metric-label{color:var(--muted);font-size:.74rem;font-weight:800;letter-spacing:.065em;text-transform:uppercase}
.metric-value{margin-top:5px;font-size:1.15rem;font-weight:850;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.players{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px}
.player{background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:14px;transition:border-color .15s ease,opacity .15s ease,transform .15s ease}
.player.active{border-color:var(--blue);box-shadow:0 0 0 1px rgba(114,167,255,.24) inset;transform:translateY(-1px)}
.player.winner{border-color:var(--good);box-shadow:0 0 0 1px rgba(98,213,138,.18) inset}
.player.target{border-color:var(--bad)}
.player.confirm{border-color:var(--good)}
.player-name{font-size:1.15rem;font-weight:900}
.player-seat{color:var(--muted);font-size:.82rem;margin-top:4px}
.player-status{margin-top:12px;font-size:.82rem;color:var(--muted);font-weight:800}
.decision{border-left:3px solid var(--warn);background:var(--panel2);border-radius:12px;padding:13px 14px;color:var(--muted);line-height:1.45}
.decision strong{color:var(--text)}
.flow{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:7px}
.flow-step{border:1px solid var(--line);background:var(--panel3);border-radius:11px;padding:10px 8px;text-align:center;color:var(--muted);font-size:.72rem;font-weight:850;letter-spacing:.04em;text-transform:uppercase}
.flow-step.active{color:var(--text);border-color:var(--blue);background:rgba(114,167,255,.09)}
.guide{display:grid;gap:8px}
.guide-row{display:grid;grid-template-columns:minmax(88px,.8fr) 1.5fr;gap:10px;align-items:start;background:var(--panel2);border:1px solid var(--line);border-radius:12px;padding:11px 12px}
.guide-key{font-weight:900;color:var(--text)}
.guide-text{color:var(--muted);line-height:1.35;font-size:.86rem}
.hardware{display:grid;gap:10px}
.hardware-main{display:flex;justify-content:space-between;gap:12px;align-items:center;background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:14px}
.hardware-count{font-size:2rem;font-weight:950;line-height:1}
.hardware-label{color:var(--muted);font-size:.78rem;font-weight:800;text-transform:uppercase;letter-spacing:.08em;margin-top:5px}
.status-line{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 0;border-bottom:1px solid var(--line)}
.status-line:last-child{border-bottom:0}
.status-label{color:var(--muted);font-size:.84rem;font-weight:700}
.status-value{font-size:.86rem;font-weight:850;text-align:right}
.footer{color:var(--muted);text-align:center;font-size:.76rem;margin-top:15px}
@media(min-width:800px){.hero-card{grid-column:span 8}.table-card{grid-column:span 4}.players-card{grid-column:span 8}.hardware-card{grid-column:span 4}.guide-card{grid-column:span 7}.flow-card{grid-column:span 5}}
@media(max-width:620px){header{align-items:flex-start}.header-right{max-width:58%}.hero{min-height:235px}.card{padding:15px}.flow{grid-template-columns:1fr 1fr}.flow-step:last-child{grid-column:span 2}.guide-row{grid-template-columns:1fr}.guide-key{margin-bottom:-4px}}
</style>
</head>
<body>
<div class="shell">
  <header>
    <div class="brand-wrap"><div class="brand">TurnHub</div><div class="brand-sub">Atlas table console</div></div>
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

    <section class="card table-card">
      <h2 class="section-title">Table</h2>
      <div class="metrics">
        <div class="metric"><div class="metric-label">Players</div><div id="playersMetric" class="metric-value">0</div></div>
        <div class="metric"><div class="metric-label">Sigils</div><div id="sigilsMetric" class="metric-value">0</div></div>
        <div class="metric"><div class="metric-label">Host</div><div id="hostMetric" class="metric-value">None</div></div>
        <div class="metric"><div class="metric-label">Starter</div><div id="starterMetric" class="metric-value">None</div></div>
      </div>
      <div style="margin-top:10px">
        <div class="status-line"><span class="status-label">State</span><span id="stateMetric" class="status-value">—</span></div>
        <div class="status-line"><span class="status-label">Firmware</span><span id="firmwareMetric" class="status-value">—</span></div>
        <div class="status-line"><span class="status-label">ESP-NOW</span><span id="espMetric" class="status-value">—</span></div>
      </div>
    </section>

    <section class="card players-card">
      <h2 class="section-title">Players</h2>
      <div id="players" class="players"></div>
    </section>

    <section class="card hardware-card">
      <h2 class="section-title">Hardware</h2>
      <div class="hardware">
        <div class="hardware-main"><div><div id="hardwareCount" class="hardware-count">0</div><div class="hardware-label">Online Sigils</div></div><span id="hardwareHealth" class="badge">Checking</span></div>
        <div id="decision" class="decision">Physical Sigils control the table.</div>
        <div class="status-line"><span class="status-label">Master button</span><span id="masterMetric" class="status-value">Released</span></div>
        <div class="status-line"><span class="status-label">OTA state</span><span id="otaMetric" class="status-value">Locked</span></div>
      </div>
    </section>

    <section class="card guide-card">
      <h2 class="section-title">Current controls</h2>
      <div id="guide" class="guide"></div>
    </section>

    <section class="card flow-card">
      <h2 class="section-title">Game flow</h2>
      <div class="flow">
        <div id="flowLobby" class="flow-step">Lobby</div>
        <div id="flowStarting" class="flow-step">Starting</div>
        <div id="flowRunning" class="flow-step">Running</div>
        <div id="flowPaused" class="flow-step">Paused</div>
        <div id="flowGameOver" class="flow-step">Game over</div>
      </div>
      <div style="margin-top:12px;color:var(--muted);font-size:.84rem;line-height:1.45">The portal mirrors Atlas state. Physical Sigils remain authoritative while browser controls are migrated back in.</div>
    </section>
  </div>

  <div class="footer">TurnHub Atlas · ESP32 portal migration</div>
</div>
<script>
function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function badge(text,cls=''){return `<span class="badge ${cls}">${esc(text)}</span>`;}
function playerName(n){return Number(n)>0?`Player ${Number(n)}`:'None';}
function hostName(h){return Number(h)>=0?`Sigil ${Number(h)+1}`:'None';}
function guideRow(key,text){return `<div class="guide-row"><div class="guide-key">${esc(key)}</div><div class="guide-text">${esc(text)}</div></div>`;}

function renderHero(s){
  const eye=document.getElementById('eyebrow');
  const title=document.getElementById('heroTitle');
  const sub=document.getElementById('heroSub');
  const badges=document.getElementById('heroBadges');
  let html='';

  if(s.state==='LOBBY'){
    eye.textContent='LOBBY';
    title.textContent=Number(s.players)?`${s.players} PLAYER${Number(s.players)===1?'':'S'}`:'READY';
    sub.textContent=s.starter?`${playerName(s.starter)} selected to start`:'Press Action on a Sigil to join';
    if(s.starter) html+=badge(`Starter: ${playerName(s.starter)}`,'blue');
    if(Number(s.host)>=0) html+=badge(`${hostName(s.host)} host`,'good');
  }else if(s.state==='STARTING'){
    eye.textContent='STARTING';
    title.textContent='3 · 2 · 1';
    sub.textContent=s.starter?`${playerName(s.starter)} goes first`:'Game countdown active';
    html+=badge('Countdown','blue');
  }else if(s.state==='RUNNING'){
    eye.textContent='ACTIVE TURN';
    title.textContent=s.active?playerName(s.active).toUpperCase():'RUNNING';
    sub.textContent='Pass from the active Sigil to advance the turn';
    html+=badge('Game running','good');
  }else if(s.state==='PAUSED'){
    if(s.winConfirm){
      eye.textContent='VICTORY CLAIM';
      title.textContent=playerName(s.winConfirm).toUpperCase();
      sub.textContent='Waiting for this player to confirm or deny';
      html+=badge('Action = confirm','good')+badge('Pass = deny','bad');
    }else if(s.eliminationTarget){
      eye.textContent='ELIMINATION';
      title.textContent=playerName(s.eliminationTarget).toUpperCase();
      sub.textContent='Selected player is awaiting confirmation';
      html+=badge('Pass = confirm','bad');
    }else{
      eye.textContent='GAME PAUSED';
      title.textContent='PAUSED';
      sub.textContent=s.active?`${playerName(s.active)} remains the active seat`:'Game clock stopped';
      html+=badge('Paused','warn');
    }
  }else if(s.state==='GAME_OVER'){
    eye.textContent='GAME OVER';
    title.textContent=s.winner?playerName(s.winner).toUpperCase():'COMPLETE';
    sub.textContent=s.winner?`${playerName(s.winner)} wins`:'Game complete';
    if(s.winner) html+=badge('Winner','good');
  }else{
    eye.textContent='TURNHUB';
    title.textContent=s.state||'—';
    sub.textContent='Atlas state unavailable';
  }
  badges.innerHTML=html;
}

function renderPlayers(s){
  const root=document.getElementById('players');
  const count=Number(s.players)||0;
  if(!count){
    root.innerHTML='<div class="player"><div class="player-name">Waiting for players</div><div class="player-seat">Join from a physical Sigil</div><div class="player-status">Table is ready</div></div>';
    return;
  }

  let html='';
  for(let n=1;n<=count;n++){
    const classes=['player'];
    const statuses=[];
    if(Number(s.active)===n){classes.push('active');statuses.push('Active turn');}
    if(Number(s.winner)===n){classes.push('winner');statuses.push('Winner');}
    if(Number(s.eliminationTarget)===n){classes.push('target');statuses.push('Elimination selected');}
    if(Number(s.winConfirm)===n){classes.push('confirm');statuses.push('Awaiting victory vote');}
    if(Number(s.starter)===n && (s.state==='LOBBY'||s.state==='STARTING')) statuses.push('Starter');
    if(!statuses.length) statuses.push(s.state==='LOBBY'?'Ready':'Waiting');
    html+=`<div class="${classes.join(' ')}"><div class="player-name">Player ${n}</div><div class="player-seat">Logical table seat</div><div class="player-status">${esc(statuses.join(' · '))}</div></div>`;
  }
  root.innerHTML=html;
}

function renderDecision(s){
  const el=document.getElementById('decision');
  if(s.winConfirm){
    el.innerHTML=`<strong>Victory claim:</strong> ${esc(playerName(s.winConfirm))} is next to vote.`;
  }else if(s.eliminationTarget){
    el.innerHTML=`<strong>Elimination:</strong> ${esc(playerName(s.eliminationTarget))} is selected.`;
  }else if(s.state==='GAME_OVER'){
    el.innerHTML='<strong>Game complete.</strong> The host can rematch or clear the table.';
  }else if(s.state==='STARTING'){
    el.innerHTML='<strong>Starting.</strong> Countdown is active on the table.';
  }else if(s.state==='RUNNING'){
    el.innerHTML=`<strong>Active:</strong> ${esc(playerName(s.active))}`;
  }else{
    el.textContent='Physical Sigils control the table.';
  }
}

function renderGuide(s){
  let html='';
  if(s.state==='LOBBY'){
    html+=guideRow('Action short','Join the lobby. Once joined, select that player as starter.');
    html+=guideRow('Action + Pass','Add or remove the second logical player on that Sigil.');
    html+=guideRow('Host Pass','Choose a random starter when at least two players are joined.');
    html+=guideRow('Host Action long','Arm and begin the game countdown.');
  }else if(s.state==='STARTING'){
    html+=guideRow('Action','Cancel the countdown and return to the lobby.');
    html+=guideRow('Wait','Atlas starts the game when the countdown completes.');
  }else if(s.state==='RUNNING'){
    html+=guideRow('Active Pass','Pass to the next living player.');
    html+=guideRow('Action long','Pause the game. The active player can continue holding toward a win claim.');
    html+=guideRow('Atlas button','Master-pass the active turn if a Sigil is unavailable.');
  }else if(s.state==='PAUSED'){
    if(s.winConfirm){
      html+=guideRow('Action short','Confirm the current victory claim when your player is requested.');
      html+=guideRow('Pass','Deny the victory claim when your player is requested.');
    }else if(s.eliminationTarget){
      html+=guideRow('Action short','Cycle the selected local player when a Sigil owns two players.');
      html+=guideRow('Pass','Confirm elimination of the selected player.');
      html+=guideRow('Action long','Cancel elimination selection.');
    }else{
      html+=guideRow('Action long','Resume the game.');
      html+=guideRow('Action + Pass','Begin elimination selection for a player on that Sigil.');
    }
  }else if(s.state==='GAME_OVER'){
    html+=guideRow('Host Action short','Return joined players to a rematch lobby.');
    html+=guideRow('Host Action long','Clear the table and return to an empty lobby.');
  }else{
    html+=guideRow('Sigils','Use the physical modules to control TurnHub.');
  }
  document.getElementById('guide').innerHTML=html;
}

function renderFlow(s){
  const map={LOBBY:'flowLobby',STARTING:'flowStarting',RUNNING:'flowRunning',PAUSED:'flowPaused',GAME_OVER:'flowGameOver'};
  Object.values(map).forEach(id=>document.getElementById(id).classList.remove('active'));
  if(map[s.state]) document.getElementById(map[s.state]).classList.add('active');
}

function render(s){
  document.getElementById('playersMetric').textContent=s.players;
  document.getElementById('sigilsMetric').textContent=s.sigils;
  document.getElementById('hardwareCount').textContent=s.sigils;
  document.getElementById('hostMetric').textContent=hostName(s.host);
  document.getElementById('starterMetric').textContent=playerName(s.starter);
  document.getElementById('stateMetric').textContent=s.state||'—';
  document.getElementById('firmwareMetric').textContent=`v${s.firmware||'?'}`;
  document.getElementById('espMetric').textContent=s.espNow?'Ready':'Error';
  document.getElementById('masterMetric').textContent=s.masterButton?'Pressed':'Released';
  document.getElementById('otaMetric').textContent=s.otaStateAllowed?'Available with button':'Locked during game';

  const health=document.getElementById('hardwareHealth');
  if(!s.espNow){health.textContent='Radio error';health.className='badge bad';}
  else if(Number(s.sigils)>0){health.textContent='Online';health.className='badge good';}
  else{health.textContent='Waiting';health.className='badge warn';}

  renderHero(s);
  renderPlayers(s);
  renderDecision(s);
  renderGuide(s);
  renderFlow(s);
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
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;padding:18px;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;background:#0b0d11;color:#f3f5f7}.shell{width:min(900px,100%);margin:auto}.top{display:flex;justify-content:space-between;gap:12px;align-items:center;flex-wrap:wrap}.links{display:flex;gap:8px}.links a{color:#111318;background:#f3f5f7;padding:8px 11px;border-radius:9px;text-decoration:none;font-family:system-ui,sans-serif;font-weight:800}.card{margin-top:14px;background:#141820;border:1px solid #2b3240;border-radius:14px;padding:14px}pre{white-space:pre-wrap;overflow-wrap:anywhere;margin:0;color:#cdd6e5}.ok{color:#62d58a}.bad{color:#ef6a6a}h1{font-family:system-ui,sans-serif;margin:0}
</style>
</head>
<body>
<div class="shell">
  <div class="top"><div><h1>Atlas Dev</h1><div id="connection">Connecting...</div></div><div class="links"><a href="/portal">Portal</a><a href="/update">Firmware</a></div></div>
  <div class="card"><pre id="json">Waiting for status...</pre></div>
</div>
<script>
async function tick(){
  const c=document.getElementById('connection');
  try{
    const r=await fetch('/api/status',{cache:'no-store'});
    if(!r.ok)throw new Error('status');
    const s=await r.json();
    document.getElementById('json').textContent=JSON.stringify(s,null,2);
    c.textContent='Atlas online';c.className='ok';
  }catch(e){c.textContent='Disconnected';c.className='bad';}
}
tick();setInterval(tick,400);
</script>
</body>
</html>
)HTML";

}  // namespace TurnHubWeb
