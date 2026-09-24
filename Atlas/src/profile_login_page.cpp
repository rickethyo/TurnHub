#include "profile_login_page.h"
namespace TurnHubLoginPage {
const char HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#110d09">
<title>TurnHub · Account login</title>
<script>try{const h=document.documentElement;h.dataset.theme=localStorage.getItem('turnhubTheme')||(matchMedia('(prefers-contrast: more)').matches?'contrast':'brass');if(localStorage.getItem('turnhubReduceMotion')==='1')h.dataset.motion='reduce'}catch(_){}</script>
<link rel="stylesheet" href="/theme.css">
<style>
#message{min-height:3em;padding:12px 14px;border-left:4px solid var(--info);background:var(--surface-2);border-radius:var(--radius-sm);overflow-wrap:anywhere;margin:0 0 18px}
form button[type=submit]{width:100%;margin-top:20px;min-height:50px}
</style></head><body><div class="page narrow">
<header class="page-head"><a class="brand" href="/"><span class="brand-mark" aria-hidden="true"></span><span><span class="brand-name">TurnHub</span><span class="brand-sub">Accounts</span></span></a><a class="btn small ghost" href="/">← Back to table</a></header>
<main>
<div class="page-title"><h1>Your TurnHub account</h1>
<p>Your name and statistics stay with you. Play from a phone, a Sigil, or both. No physical Sigil is required.</p></div>
<p id="message" role="status" aria-live="polite" tabindex="-1">Loading saved accounts…</p>
<div class="cols"><section class="card"><h2 class="eyebrow">Sign in</h2>
<form id="loginForm"><label for="savedProfile">Saved account</label><select id="savedProfile" required aria-describedby="loginHint"><option value="">Choose an account</option></select>
<label for="loginPin">PIN</label><input id="loginPin" type="password" inputmode="numeric" pattern="[0-9]{4,8}" minlength="4" maxlength="8" autocomplete="current-password" required>
<p id="loginHint" class="hint">Use your 4–8 digit PIN. An account without a saved PIN can still use physical sign-in at the table, then set a PIN in My Account.</p>
<button type="submit" class="primary">Sign in</button></form></section>
<section class="card"><h2 class="eyebrow">Create an account</h2><form id="registerForm">
<label for="newName">Display name</label><input id="newName" maxlength="32" autocomplete="nickname" required>
<label for="newPin">Choose a PIN</label><input id="newPin" type="password" inputmode="numeric" pattern="[0-9]{4,8}" minlength="4" maxlength="8" autocomplete="new-password" required>
<label for="confirmPin">Confirm PIN</label><input id="confirmPin" type="password" inputmode="numeric" pattern="[0-9]{4,8}" minlength="4" maxlength="8" autocomplete="new-password" required>
<p class="hint">Accounts are saved on this Atlas. Remember your PIN to sign in again from any phone on its network.</p>
<button type="submit" class="primary">Create account</button></form></section></div>
<p class="foot">Signing in does not add another player. Use “Join table” in the portal. If your account is already playing, this phone controls that same player.</p>
</main></div><script>
const message=document.getElementById('message');
function notify(text){message.textContent=text;message.focus()}
async function loadProfiles(){try{const r=await fetch('/api/profiles',{cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not load accounts');for(const p of d.profiles){const option=document.createElement('option');option.value=p.profileId;option.textContent=`${p.name||'Unnamed account'} · ${p.profileId}${p.hasPin?'':' · physical sign-in needed'}`;savedProfile.appendChild(option)}const requested=new URLSearchParams(location.search).get('profile');if(requested)savedProfile.value=requested;message.textContent=d.profiles.length?'Choose a saved account or create your own.':'No saved accounts yet. Create one to begin.'}catch(e){notify(e.message)}}
async function submit(form,path,data){const button=form.querySelector('button');button.disabled=true;try{const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const d=await r.json();if(!r.ok)throw new Error(d.error||'Sign-in failed');localStorage.setItem('turnhubSessionToken',d.token);location.assign('/')}catch(e){notify(e.message);button.disabled=false}}
loginForm.addEventListener('submit',e=>{e.preventDefault();submit(loginForm,'/api/session/login',{profileId:savedProfile.value,pin:loginPin.value})});
registerForm.addEventListener('submit',e=>{e.preventDefault();if(newPin.value!==confirmPin.value){notify('The PINs do not match.');confirmPin.focus();return}submit(registerForm,'/api/profiles/register',{name:newName.value.trim(),pin:newPin.value})});
loadProfiles();
</script></body></html>
)HTML";
}
