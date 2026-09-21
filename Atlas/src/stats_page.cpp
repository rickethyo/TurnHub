#include "stats_page.h"

namespace TurnHubStatsPage {

const char STATS_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0d11">
<title>TurnHub Statistics</title>
<style>
:root{color-scheme:dark;--bg:#0b0d11;--panel:#141820;--panel2:#1b202a;--line:#2b3240;--text:#f3f5f7;--muted:#9ca6b7;--good:#62d58a;--blue:#72a7ff;--bad:#ef6a6a}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;padding:18px}.shell{width:min(980px,100%);margin:auto}.top{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;margin-bottom:16px}.brand{font-size:1.65rem;font-weight:950}.small{color:var(--muted);font-size:.84rem;line-height:1.45}.actions{display:flex;gap:8px;flex-wrap:wrap}a,button{border:1px solid var(--line);background:var(--panel2);color:var(--text);border-radius:10px;padding:9px 12px;font:inherit;font-weight:800;text-decoration:none;cursor:pointer}.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:12px}.card{grid-column:span 12;background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:18px}.half{grid-column:span 12}.section{font-size:.78rem;color:var(--muted);font-weight:900;letter-spacing:.12em;text-transform:uppercase}.hero{font-size:2rem;font-weight:950;margin-top:7px}.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px;margin-top:13px}.metric{background:var(--panel2);border:1px solid var(--line);border-radius:14px;padding:12px}.metric label{display:block;color:var(--muted);font-size:.72rem;text-transform:uppercase;font-weight:800}.metric strong{display:block;margin-top:4px;font-size:1.05rem}.row{display:flex;justify-content:space-between;gap:12px;padding:9px 0;border-bottom:1px solid var(--line)}.row:last-child{border-bottom:0}.row strong{text-align:right}.notice{background:var(--panel2);border:1px solid var(--line);border-left:3px solid var(--blue);border-radius:12px;padding:13px;line-height:1.5}.notice.bad{border-left-color:var(--bad)}.hidden{display:none}@media(min-width:760px){.half{grid-column:span 6}.metrics{grid-template-columns:repeat(3,minmax(0,1fr))}}
</style>
</head>
<body>
<div class="shell">
  <div class="top">
    <div><div class="brand">TurnHub Statistics</div><div class="small">Durable local player profile statistics</div></div>
    <div class="actions"><a href="/portal">Back to portal</a><button id="exportButton" class="hidden" onclick="exportStats()">Download my stats</button></div>
  </div>

  <div id="signedOut" class="notice hidden">No authenticated TurnHub seat is linked to this browser. Return to the portal and sign into a player seat first.</div>
  <div id="errorBox" class="notice bad hidden"></div>

  <div id="statsRoot" class="grid hidden">
    <section class="card">
      <div class="section">Profile</div>
      <div id="profileName" class="hero">Player</div>
      <div id="profileMeta" class="small"></div>
    </section>

    <section class="card">
      <div class="section">Lifetime</div>
      <div class="metrics">
        <div class="metric"><label>Games played</label><strong id="gamesPlayed">0</strong></div>
        <div class="metric"><label>Games won</label><strong id="gamesWon">0</strong></div>
        <div class="metric"><label>Win rate</label><strong id="winRate">0.0%</strong></div>
        <div class="metric"><label>Started first</label><strong id="gamesStarted">0</strong></div>
        <div class="metric"><label>Eliminated</label><strong id="gamesEliminated">0</strong></div>
        <div class="metric"><label>Completed turns</label><strong id="turnsCompleted">0</strong></div>
      </div>
    </section>

    <section class="card half">
      <div class="section">Turn records</div>
      <div style="margin-top:10px">
        <div class="row"><span>Total completed-turn time</span><strong id="totalTurnTime">—</strong></div>
        <div class="row"><span>Average completed turn</span><strong id="averageTurn">—</strong></div>
        <div class="row"><span>Fastest completed turn</span><strong id="fastestTurn">—</strong></div>
        <div class="row"><span>Longest completed turn</span><strong id="longestTurn">—</strong></div>
      </div>
    </section>

    <section class="card half">
      <div class="section">Game time</div>
      <div style="margin-top:10px">
        <div class="row"><span>Total game time</span><strong id="totalGameTime">—</strong></div>
        <div class="row"><span>Average game time</span><strong id="averageGame">—</strong></div>
      </div>
    </section>

    <section class="card">
      <div class="section">Most recent game</div>
      <div style="margin-top:10px">
        <div class="row"><span>Result</span><strong id="lastResult">None</strong></div>
        <div class="row"><span>Game duration</span><strong id="lastDuration">—</strong></div>
        <div class="row"><span>Completed turns</span><strong id="lastTurns">0</strong></div>
        <div class="row"><span>Average turn</span><strong id="lastAverageTurn">—</strong></div>
        <div class="row"><span>Fastest turn</span><strong id="lastFastestTurn">—</strong></div>
        <div class="row"><span>Longest turn</span><strong id="lastLongestTurn">—</strong></div>
      </div>
    </section>
  </div>
</div>
<script>
const token=localStorage.getItem('turnhubSessionToken')||'';
const signedOut=document.getElementById('signedOut');
const errorBox=document.getElementById('errorBox');
const root=document.getElementById('statsRoot');
const exportButton=document.getElementById('exportButton');
function authHeaders(){return token?{'X-TurnHub-Token':token}:{}}
function duration(ms,hasRecord=true){if(!hasRecord)return '—';ms=Number(ms)||0;const total=Math.floor(ms/1000);const h=Math.floor(total/3600);const m=Math.floor((total%3600)/60);const s=total%60;return h?`${h}h ${m}m ${s}s`:`${m}m ${s}s`}
function setText(id,value){document.getElementById(id).textContent=value}
async function load(){if(!token){signedOut.classList.remove('hidden');return}try{const r=await fetch('/api/session/stats',{headers:authHeaders(),cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not load statistics');const l=d.lifetime||{},g=d.lastGame||{};setText('profileName',d.name||'Unnamed profile');setText('profileMeta',`Profile ${d.profileId} · statistics stay with this profile, not with a physical Sigil`);setText('gamesPlayed',l.gamesPlayed||0);setText('gamesWon',l.gamesWon||0);setText('winRate',d.winRate||'0.0%');setText('gamesStarted',l.gamesStarted||0);setText('gamesEliminated',l.gamesEliminated||0);setText('turnsCompleted',l.turnsCompleted||0);setText('totalTurnTime',duration(l.totalTurnMs,true));setText('averageTurn',duration(l.averageTurnMs,Number(l.turnsCompleted)>0));setText('fastestTurn',duration(l.fastestTurnMs,Number(l.fastestTurnMs)>0));setText('longestTurn',duration(l.longestTurnMs,Number(l.longestTurnMs)>0));setText('totalGameTime',duration(l.totalGameMs,true));setText('averageGame',duration(l.averageGameMs,Number(l.gamesPlayed)>0));setText('lastResult',g.result||'None');setText('lastDuration',duration(g.durationMs,Number(l.gamesPlayed)>0));setText('lastTurns',g.turns||0);setText('lastAverageTurn',duration(g.averageTurnMs,Number(g.turns)>0));setText('lastFastestTurn',duration(g.fastestTurnMs,Number(g.fastestTurnMs)>0));setText('lastLongestTurn',duration(g.longestTurnMs,Number(g.longestTurnMs)>0));root.classList.remove('hidden');exportButton.classList.remove('hidden')}catch(e){errorBox.textContent=e.message;errorBox.classList.remove('hidden')}}
async function exportStats(){try{const r=await fetch('/api/session/stats/export',{headers:authHeaders(),cache:'no-store'});if(!r.ok){let msg='Could not export statistics';try{const d=await r.json();msg=d.error||msg}catch(_){}throw new Error(msg)}const blob=await r.blob();const cd=r.headers.get('Content-Disposition')||'';const match=cd.match(/filename="([^"]+)"/);const name=match?match[1]:'turnhub-stats.txt';const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000)}catch(e){errorBox.textContent=e.message;errorBox.classList.remove('hidden')}}
load();
</script>
</body>
</html>
)HTML";

}  // namespace TurnHubStatsPage
