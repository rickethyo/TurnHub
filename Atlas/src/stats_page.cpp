#include "stats_page.h"

namespace TurnHubStatsPage {

const char STATS_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#110d09">
<title>TurnHub Statistics</title>
<script>try{const h=document.documentElement;h.dataset.theme=localStorage.getItem('turnhubTheme')||(matchMedia('(prefers-contrast: more)').matches?'contrast':'brass');if(localStorage.getItem('turnhubReduceMotion')==='1')h.dataset.motion='reduce'}catch(_){}</script>
<link rel="stylesheet" href="/theme.css">
<style>
.hidden{display:none!important}
.profile{display:grid;grid-template-columns:auto minmax(0,1fr);align-items:center;gap:clamp(18px,4vw,40px)}
.dial{position:relative;width:clamp(130px,30vw,170px);aspect-ratio:1}
.dial svg{width:100%;height:100%;display:block;transform:rotate(-90deg)}
.dial .track{fill:none;stroke:var(--surface-3);stroke-width:10}
.dial .fill{fill:none;stroke:url(#dialGrad);stroke-width:10;stroke-linecap:round;transition:stroke-dasharray .9s cubic-bezier(.3,.8,.3,1)}
.dial .rim{fill:var(--inset);stroke:var(--line-strong)}
.stop-hi{stop-color:var(--accent-hi)}.stop-lo{stop-color:var(--accent-lo)}
.dial-text{position:absolute;inset:0;display:grid;place-content:center;text-align:center}
.dial-text strong{font:700 clamp(1.5rem,5vw,2rem)/1 var(--font-display);font-variant-numeric:tabular-nums}
.dial-text span{font-size:.64rem;letter-spacing:var(--label-spacing);text-transform:uppercase;color:var(--muted);margin-top:4px}
#profileName{font-size:clamp(1.8rem,6vw,2.8rem);overflow-wrap:anywhere;margin:.3rem 0}
.metrics{grid-template-columns:repeat(auto-fit,minmax(140px,1fr))}
.metric strong{font-size:1.7rem}
@media(max-width:520px){.profile{grid-template-columns:1fr;justify-items:center;text-align:center}.profile .eyebrow{justify-content:center}.profile .eyebrow::after{display:none}}
</style>
</head>
<body>
<div class="page">
  <header class="page-head">
    <a class="brand" href="/portal"><span class="brand-mark" aria-hidden="true"></span><span><span class="brand-name">TurnHub</span><span class="brand-sub">Statistics</span></span></a>
    <div class="actions"><a class="btn ghost" href="/portal">Back to portal</a><button id="exportButton" class="primary hidden" onclick="exportStats()">Download my stats</button></div>
  </header>
  <h1 class="sr-only">TurnHub Statistics</h1>

  <div id="signedOut" class="notice info hidden"><p>Sign in to see your statistics. They stay with your profile on this Atlas.</p><a class="btn primary" href="/login">Sign in</a></div>
  <div id="errorBox" class="notice bad hidden"></div>
  <div id="noSdNotice" class="notice info hidden"><p>Detailed statistics (turn times and last-game details) are kept on the Atlas microSD card. No card is inserted, so only games played and won, and the last result, are being recorded.</p></div>

  <main id="statsRoot" class="stack hidden">
    <section class="card profile">
      <div class="dial" aria-hidden="true">
        <svg viewBox="0 0 120 120"><defs><linearGradient id="dialGrad" x1="0" y1="0" x2="1" y2="1"><stop offset="0" class="stop-hi"/><stop offset="1" class="stop-lo"/></linearGradient></defs>
          <circle class="rim" cx="60" cy="60" r="58"/><circle class="track" cx="60" cy="60" r="46"/><circle id="winDial" class="fill" cx="60" cy="60" r="46" pathLength="100" stroke-dasharray="0 100"/></svg>
        <div class="dial-text"><strong id="dialRate">0%</strong><span>Win rate</span></div>
      </div>
      <div style="min-width:0"><h2 class="eyebrow">Profile</h2><div id="profileName" class="brand-name">Player</div><div id="profileMeta" class="small"></div></div>
    </section>

    <section class="card">
      <div class="card-head"><h2 class="eyebrow">Lifetime</h2></div>
      <div class="metrics">
        <div class="metric"><label>Games played</label><strong id="gamesPlayed">0</strong></div>
        <div class="metric"><label>Games won</label><strong id="gamesWon">0</strong></div>
        <div class="metric"><label>Win rate</label><strong id="winRate">0.0%</strong></div>
        <div class="metric"><label>Started first</label><strong id="gamesStarted">0</strong></div>
        <div class="metric"><label>Eliminated</label><strong id="gamesEliminated">0</strong></div>
        <div class="metric"><label>Completed turns</label><strong id="turnsCompleted">0</strong></div>
      </div>
    </section>

    <div class="cols">
    <section class="card">
      <div class="card-head"><h2 class="eyebrow">Turn records</h2></div>
      <div class="status-row"><span>Total completed-turn time</span><strong id="totalTurnTime">—</strong></div>
      <div class="status-row"><span>Average completed turn</span><strong id="averageTurn">—</strong></div>
      <div class="status-row"><span>Fastest completed turn</span><strong id="fastestTurn">—</strong></div>
      <div class="status-row"><span>Longest completed turn</span><strong id="longestTurn">—</strong></div>
    </section>

    <section class="card">
      <div class="card-head"><h2 class="eyebrow">Game time</h2></div>
      <div class="status-row"><span>Total game time</span><strong id="totalGameTime">—</strong></div>
      <div class="status-row"><span>Average game time</span><strong id="averageGame">—</strong></div>
    </section>
    </div>

    <section class="card">
      <div class="card-head"><h2 class="eyebrow">Most recent game</h2></div>
      <div class="status-row"><span>Result</span><strong id="lastResult">None</strong></div>
      <div class="status-row"><span>Game duration</span><strong id="lastDuration">—</strong></div>
      <div class="status-row"><span>Completed turns</span><strong id="lastTurns">0</strong></div>
      <div class="status-row"><span>Average turn</span><strong id="lastAverageTurn">—</strong></div>
      <div class="status-row"><span>Fastest turn</span><strong id="lastFastestTurn">—</strong></div>
      <div class="status-row"><span>Longest turn</span><strong id="lastLongestTurn">—</strong></div>
    </section>

    <section class="card" aria-labelledby="moderationHeading">
      <div class="card-head"><div><h2 id="moderationHeading" class="eyebrow">Private moderation history</h2><p class="small">Only you can see this, and only after signing in with your PIN. It is never included in downloads or shown to Game Masters.</p></div></div>
      <div id="moderationCounts" class="hidden">
        <div class="status-row"><span>Connection resets by a Game Master</span><strong id="connectionResets">0</strong></div>
        <div class="status-row"><span>Removals from a game by a Game Master</span><strong id="gameRemovals">0</strong></div>
      </div>
      <p id="moderationLocked" class="small hidden"></p>
    </section>
  </main>
  <p class="foot">Statistics are stored on this Atlas and stay with your profile, not with a physical Sigil.</p>
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
async function load(){if(!token){signedOut.classList.remove('hidden');return}try{const r=await fetch('/api/session/stats',{headers:authHeaders(),cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not load statistics');const l=d.lifetime||{},g=d.lastGame||{};setText('profileName',d.name||'Unnamed profile');setText('profileMeta',`Profile ${d.profileId} · statistics stay with this profile, not with a physical Sigil`);setText('gamesPlayed',l.gamesPlayed||0);setText('gamesWon',l.gamesWon||0);setText('winRate',d.winRate||'0.0%');setText('gamesStarted',l.gamesStarted||0);setText('gamesEliminated',l.gamesEliminated||0);setText('turnsCompleted',l.turnsCompleted||0);setText('totalTurnTime',duration(l.totalTurnMs,true));setText('averageTurn',duration(l.averageTurnMs,Number(l.turnsCompleted)>0));setText('fastestTurn',duration(l.fastestTurnMs,Number(l.fastestTurnMs)>0));setText('longestTurn',duration(l.longestTurnMs,Number(l.longestTurnMs)>0));setText('totalGameTime',duration(l.totalGameMs,true));setText('averageGame',duration(l.averageGameMs,Number(l.gamesPlayed)>0));setText('lastResult',g.result||'None');setText('lastDuration',duration(g.durationMs,Number(l.gamesPlayed)>0));setText('lastTurns',g.turns||0);setText('lastAverageTurn',duration(g.averageTurnMs,Number(g.turns)>0));setText('lastFastestTurn',duration(g.fastestTurnMs,Number(g.fastestTurnMs)>0));setText('lastLongestTurn',duration(g.longestTurnMs,Number(g.longestTurnMs)>0));
const m=d.moderation||{};document.getElementById('moderationCounts').classList.toggle('hidden',!m.visible);const locked=document.getElementById('moderationLocked');locked.classList.toggle('hidden',!!m.visible);if(m.visible){setText('connectionResets',m.connectionResets||0);setText('gameRemovals',m.gameRemovals||0)}else{locked.innerHTML='';locked.append(m.reason||'Sign in with your PIN to see your private moderation history.',' ');const a=document.createElement('a');a.href='/login';a.textContent='Sign in with PIN';locked.append(a)}
const rate=Number(l.gamesPlayed)>0?Math.max(0,Math.min(100,100*Number(l.gamesWon)/Number(l.gamesPlayed))):0;setText('dialRate',Math.round(rate)+'%');if(d.detailed===false)document.getElementById('noSdNotice').classList.remove('hidden');root.classList.remove('hidden');exportButton.classList.remove('hidden');requestAnimationFrame(()=>document.getElementById('winDial').setAttribute('stroke-dasharray',rate.toFixed(1)+' 100'))}catch(e){errorBox.textContent=e.message;errorBox.classList.remove('hidden')}}
async function exportStats(){try{const r=await fetch('/api/session/stats/export',{headers:authHeaders(),cache:'no-store'});if(!r.ok){let msg='Could not export statistics';try{const d=await r.json();msg=d.error||msg}catch(_){}throw new Error(msg)}const blob=await r.blob();const cd=r.headers.get('Content-Disposition')||'';const match=cd.match(/filename="([^"]+)"/);const name=match?match[1]:'turnhub-stats.txt';const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000)}catch(e){errorBox.textContent=e.message;errorBox.classList.remove('hidden')}}
load();
</script>
</body>
</html>
)HTML";

}  // namespace TurnHubStatsPage
