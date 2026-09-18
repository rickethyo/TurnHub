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
:root{color-scheme:dark;--bg:#0b0d11;--panel:#141820;--panel2:#1b202a;--line:#2b3240;--text:#f3f5f7;--muted:#9ca6b7;--good:#62d58a;--warn:#efc55a;--bad:#ef6a6a;--blue:#72a7ff}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}body{padding:16px}.shell{width:min(1180px,100%);margin:auto}header{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:16px;flex-wrap:wrap}.brand{font-size:1.8rem;font-weight:950}.sub{color:var(--muted);font-size:.84rem;font-weight:700}.right{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.dot{width:10px;height:10px;border-radius:50%;background:var(--bad)}.dot.online{background:var(--good)}a.link,button{border:1px solid var(--line);background:var(--panel2);color:var(--text);border-radius:10px;padding:9px 12px;text-decoration:none;font-weight:800;cursor:pointer}button.primary{background:#e7ebf2;color:#111318;border-color:#e7ebf2}button.good{border-color:rgba(98,213,138,.5);color:var(--good)}button.bad{border-color:rgba(239,106,106,.5);color:var(--bad)}button:disabled{opacity:.42;cursor:not-allowed}.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:12px}.card{grid-column:span 12;background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:18px}.hero{min-height:260px;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center}.eyebrow,.section{font-size:.78rem;color:var(--muted);font-weight:900;letter-spacing:.12em;text-transform:uppercase}.hero h1{font-size:clamp(3rem,10vw,6rem);margin:.2rem 0;line-height:.95}.hero p{color:var(--muted);font-size:1.05rem;margin:.35rem 0}.badges,.actions{display:flex;gap:8px;flex-wrap:wrap}.badges{justify-content:center;margin-top:12px}.badge{border:1px solid var(--line);border-radius:999px;padding:6px 10px;color:var(--muted);font-size:.78rem;font-weight:800}.badge.good{color:var(--good)}.badge.warn{color:var(--warn)}.badge.blue{color:var(--blue)}.metrics{display:grid;grid-template-columns:repeat(2,1fr);gap:9px}.metric,.device,.session-box,.player{background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:13px}.metric label{display:block;color:var(--muted);font-size:.72rem;font-weight:800;text-transform:uppercase}.metric strong{display:block;margin-top:4px;font-size:1.08rem}.players,.devices{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px}.player.active{border-color:var(--blue)}.player.winner{border-color:var(--good)}.player-name{font-size:1.1rem;font-weight:900}.small{font-size:.8rem;color:var(--muted);line-height:1.45}.device-top{display:flex;justify-content:space-between;gap:10px;align-items:flex-start}.device-name{font-weight:900;font-size:1.05rem}.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.76rem;color:var(--muted);overflow-wrap:anywhere}.device-state{font-size:.74rem;font-weight:900}.online-text{color:var(--good)}.offline-text{color:var(--bad)}.device .actions{margin-top:11px}.session-head{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:10px}.session-title{font-size:1.15rem;font-weight:900}.notice{border-left:3px solid var(--warn);background:var(--panel2);padding:12px 13px;border-radius:10px;color:var(--muted);line-height:1.45}.notice strong{color:var(--text)}#toast{min-height:24px;margin-top:10px;color:var(--muted);font-size:.85rem;font-weight:700}.status-row{display:flex;justify-content:space-between;gap:12px;padding:9px 0;border-bottom:1px solid var(--line)}.status-row:last-child{border-bottom:0}.status-row span:first-child{color:var(--muted)}@media(min-width:820px){.hero-card{grid-column:span 8}.table-card{grid-column:span 4}.session-card{grid-column:span 5}.devices-card{grid-column:span 7}.players-card{grid-column:span 7}.system-card{grid-column:span 5}}@media(max-width:600px){body{padding:12px}.card{padding:14px}.hero{min-height:220px}}
</style>
</head>
<body>
<div class="shell">
<header>
  <div><div class="brand">TurnHub</div><div class="sub">Atlas table console</div></div>
  <div class="right"><span id="dot" class="dot"></span><span id="connection" class="sub">Connecting</span><a class="link" href="/dev">Dev</a><a class="link" href="/update">Firmware</a></div>
</header>
<div class="grid">
  <section class="card hero hero-card">
    <div id="eyebrow" class="eyebrow">TURNHUB</div>
    <h1 id="heroTitle">READY</h1>
    <p id="heroSub">Waiting for Atlas</p>
    <div id="heroBadges" class="badges"></div>
  </section>

  <section class="card table-card">
    <div class="section">Table</div>
    <div class="metrics" style="margin-top:12px">
      <div class="metric"><label>Players</label><strong id="playersMetric">0</strong></div>
      <div class="metric"><label>Sigils</label><strong id="sigilsMetric">0</strong></div>
      <div class="metric"><label>Host</label><strong id="hostMetric">None</strong></div>
      <div class="metric"><label>Starter</label><strong id="starterMetric">None</strong></div>
    </div>
    <div style="margin-top:10px">
      <div class="status-row"><span>State</span><strong id="stateMetric">—</strong></div>
      <div class="status-row"><span>Atlas firmware</span><strong id="firmwareMetric">—</strong></div>
      <div class="status-row"><span>ESP-NOW</span><strong id="espMetric">—</strong></div>
    </div>
  </section>

  <section class="card session-card">
    <div class="section">Browser session</div>
    <div id="sessionBox" class="session-box" style="margin-top:12px">
      <div class="session-head"><div><div id="sessionTitle" class="session-title">Not authenticated</div><div id="sessionMeta" class="small">Claim a physical Sigil below.</div></div><button id="logoutButton" class="bad" style="display:none" onclick="logoutSession()">Log out</button></div>
      <div id="sessionControls" style="display:none">
        <div class="actions">
          <button class="primary" onclick="sendControl('pass')">Pass</button>
          <button onclick="sendControl('action')">Action</button>
          <button onclick="sendControl('hold')">Hold Action</button>
          <button class="good" onclick="sendControl('win')">Claim Win</button>
        </div>
        <div class="small" style="margin-top:10px">These controls enter Atlas through the same game-event queue as the claimed physical Sigil.</div>
      </div>
      <div id="claimHelp" class="small">Authentication currently binds a browser to one physical Sigil. Profile/PIN authentication will layer on top of this session system later.</div>
      <div id="toast"></div>
    </div>
  </section>

  <section class="card devices-card">
    <div class="section">Device manager</div>
    <div id="atlasDevice" class="notice" style="margin:12px 0">Loading Atlas identity...</div>
    <div id="devices" class="devices"></div>
  </section>

  <section class="card players-card">
    <div class="section">Players</div>
    <div id="players" class="players" style="margin-top:12px"></div>
  </section>

  <section class="card system-card">
    <div class="section">System</div>
    <div style="margin-top:10px">
      <div class="status-row"><span>Active player</span><strong id="activeMetric">None</strong></div>
      <div class="status-row"><span>Winner</span><strong id="winnerMetric">None</strong></div>
      <div class="status-row"><span>Master button</span><strong id="masterMetric">Released</strong></div>
      <div class="status-row"><span>Atlas OTA</span><strong id="otaMetric">Locked</strong></div>
    </div>
  </section>
</div>
</div>
<script>
let statusData=null,deviceData=null,pendingClaim=null,pendingTimer=null;
let sessionToken=localStorage.getItem('turnhubSessionToken')||'';
function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function pName(n){return Number(n)>0?`Player ${Number(n)}`:'None';}
function hName(n){return Number(n)>=0?`Sigil ${Number(n)+1}`:'None';}
function authHeaders(){return sessionToken?{'X-TurnHub-Token':sessionToken}:{}}
function toast(msg,bad=false){const e=document.getElementById('toast');e.textContent=msg;e.style.color=bad?'var(--bad)':'var(--muted)'}
function badge(t,c=''){return `<span class="badge ${c}">${esc(t)}</span>`}

function renderStatus(s){
 statusData=s;document.getElementById('dot').className='dot online';document.getElementById('connection').textContent='Connected';
 playersMetric.textContent=s.players;sigilsMetric.textContent=s.sigils;hostMetric.textContent=hName(s.host);starterMetric.textContent=pName(s.starter);stateMetric.textContent=s.state;firmwareMetric.textContent='v'+s.firmware;espMetric.textContent=s.espNow?'Ready':'Error';activeMetric.textContent=pName(s.active);winnerMetric.textContent=pName(s.winner);masterMetric.textContent=s.masterButton?'Pressed':'Released';otaMetric.textContent=s.otaStateAllowed?'State ready':'Game locked';
 let title='READY',sub='Press Action on a Sigil to join',eye=s.state,bs='';
 if(s.state==='LOBBY'){title=s.players?`${s.players} PLAYER${Number(s.players)===1?'':'S'}`:'READY';sub=s.starter?`${pName(s.starter)} selected to start`:'Press Action on a Sigil to join';if(Number(s.host)>=0)bs+=badge(`${hName(s.host)} host`,'good');if(s.starter)bs+=badge(`${pName(s.starter)} starter`,'blue')}
 else if(s.state==='STARTING'){title='3 · 2 · 1';sub=s.starter?`${pName(s.starter)} goes first`:'Starting game';bs+=badge('Countdown','blue')}
 else if(s.state==='RUNNING'){eye='ACTIVE TURN';title=s.active?pName(s.active).toUpperCase():'RUNNING';sub='Table is live';bs+=badge('Running','good')}
 else if(s.state==='PAUSED'){title='PAUSED';sub=s.winConfirm?`${pName(s.winConfirm)} must confirm or deny`:s.eliminationTarget?`${pName(s.eliminationTarget)} selected`:'Game clock stopped';bs+=badge('Paused','warn')}
 else if(s.state==='GAME_OVER'){title=s.winner?pName(s.winner).toUpperCase():'COMPLETE';sub=s.winner?'Winner confirmed':'Game complete';bs+=badge('Game over','good')}
 eyebrow.textContent=eye;heroTitle.textContent=title;heroSub.textContent=sub;heroBadges.innerHTML=bs;
 const cards=[];for(let i=1;i<=Number(s.players);i++){let cls='player';if(i===Number(s.active))cls+=' active';if(i===Number(s.winner))cls+=' winner';let st=i===Number(s.active)?'Active turn':i===Number(s.winner)?'Winner':'At table';cards.push(`<div class="${cls}"><div class="player-name">Player ${i}</div><div class="small">${esc(st)}</div></div>`)}players.innerHTML=cards.length?cards.join(''):'<div class="small">No players have joined yet.</div>';
}

function renderDevices(d){
 deviceData=d;atlasDevice.innerHTML=`<strong>Atlas</strong> · ${esc(d.atlas.hardwareId)} · firmware ${esc(d.atlas.firmware)}`;
 devices.innerHTML=d.devices.length?d.devices.map(x=>{const current=sessionToken&&sessionInfo&&Number(sessionInfo.module)===Number(x.id);const state=x.online?'<span class="device-state online-text">ONLINE</span>':'<span class="device-state offline-text">OFFLINE</span>';const fw=x.metadata?`Firmware ${esc(x.firmware)}`:'Firmware metadata unavailable';return `<div class="device"><div class="device-top"><div><div class="device-name">${esc(x.label)}</div><div class="mono">${esc(x.hardwareId)}</div></div>${state}</div><div class="small" style="margin-top:8px">${fw}<br>Last seen ${Math.round(Number(x.ageMs)/100)/10}s ago<br>Capabilities 0x${Number(x.capabilities).toString(16).toUpperCase()}</div><div class="actions">${current?'<button disabled>Current session</button>':`<button ${x.online?'':'disabled'} onclick="claimDevice(${Number(x.id)})">Use this Sigil</button>`}</div></div>`}).join(''):'<div class="small">No Sigils discovered yet.</div>';
}

let sessionInfo=null;
async function refreshSession(){
 if(!sessionToken){sessionInfo=null;renderSession();return}
 try{const r=await fetch('/api/session/me',{headers:authHeaders(),cache:'no-store'});if(!r.ok)throw 0;sessionInfo=await r.json()}catch(_){sessionToken='';sessionInfo=null;localStorage.removeItem('turnhubSessionToken')}renderSession();if(deviceData)renderDevices(deviceData)
}
function renderSession(){
 const authed=!!(sessionInfo&&sessionInfo.authenticated);sessionTitle.textContent=authed?`Controlling Sigil ${Number(sessionInfo.module)+1}`:'Not authenticated';sessionMeta.textContent=authed?(sessionInfo.hardwareId||'Authorized physical Sigil'):'Claim a physical Sigil below.';sessionControls.style.display=authed?'block':'none';logoutButton.style.display=authed?'inline-block':'none';claimHelp.style.display=authed?'none':'block';
}
async function claimDevice(module){
 toast(`Waiting for authorization on Sigil ${module+1}...`);try{const r=await fetch(`/api/session/request?module=${module}`,{method:'POST'});const d=await r.json();if(!r.ok)throw new Error(d.error||'Claim failed');pendingClaim=d.requestId;if(pendingTimer)clearInterval(pendingTimer);pendingTimer=setInterval(pollClaim,450);toast(`Press Action on Sigil ${module+1} within 30 seconds.`)}catch(e){toast(e.message,true)}
}
async function pollClaim(){
 if(!pendingClaim)return;try{const r=await fetch(`/api/session/poll?id=${encodeURIComponent(pendingClaim)}`,{cache:'no-store'});const d=await r.json();if(r.status===404){clearInterval(pendingTimer);pendingClaim=null;toast('Authorization expired. Try again.',true);return}if(d.status==='approved'){clearInterval(pendingTimer);pendingClaim=null;sessionToken=d.token;localStorage.setItem('turnhubSessionToken',sessionToken);toast(`Browser authorized for Sigil ${Number(d.module)+1}.`);await refreshSession();await refreshDevices()}}catch(_){}}
async function logoutSession(){try{await fetch('/api/session/logout',{method:'POST',headers:authHeaders()})}catch(_){}sessionToken='';sessionInfo=null;localStorage.removeItem('turnhubSessionToken');toast('Browser session ended.');renderSession();if(deviceData)renderDevices(deviceData)}
async function sendControl(name){
 if(!sessionToken){toast('Claim a Sigil first.',true);return}try{const r=await fetch(`/api/control/${name}`,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Control rejected');toast(`${name==='hold'?'Hold Action':name==='win'?'Win claim':name[0].toUpperCase()+name.slice(1)} sent.`)}catch(e){toast(e.message,true);if(String(e.message).includes('authorized'))await refreshSession()}}

async function refreshStatus(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw 0;renderStatus(await r.json())}catch(_){dot.className='dot';connection.textContent='Disconnected'}}
async function refreshDevices(){try{const r=await fetch('/api/devices',{cache:'no-store'});if(!r.ok)throw 0;renderDevices(await r.json())}catch(_){devices.innerHTML='<div class="small">Device API unavailable.</div>'}}
async function boot(){await refreshSession();await Promise.all([refreshStatus(),refreshDevices()]);setInterval(refreshStatus,500);setInterval(refreshDevices,1500);setInterval(refreshSession,5000)}
boot();
</script>
</body>
</html>
)HTML";

const char DEV_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#0b0d11"><title>TurnHub Dev</title><style>:root{color-scheme:dark}body{margin:0;padding:18px;background:#0b0d11;color:#f3f5f7;font-family:system-ui,sans-serif}main{max-width:1000px;margin:auto}a{color:#9fc0ff}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#141820;border:1px solid #2b3240;border-radius:14px;padding:14px;color:#cbd3df}h1{margin-bottom:4px}.muted{color:#9ca6b7}</style></head><body><main><h1>TurnHub Dev</h1><p class="muted">Live Atlas status and device inventory.</p><p><a href="/portal">Back to portal</a> · <a href="/update">Firmware update</a></p><h2>Status</h2><pre id="status">Loading...</pre><h2>Devices</h2><pre id="devices">Loading...</pre><script>async function load(){try{const [s,d]=await Promise.all([fetch('/api/status',{cache:'no-store'}),fetch('/api/devices',{cache:'no-store'})]);status.textContent=JSON.stringify(await s.json(),null,2);devices.textContent=JSON.stringify(await d.json(),null,2)}catch(e){status.textContent='Disconnected';devices.textContent='Disconnected'}}load();setInterval(load,1000);</script></main></body></html>
)HTML";

}  // namespace TurnHubWeb
