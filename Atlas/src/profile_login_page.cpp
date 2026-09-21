#include "profile_login_page.h"
namespace TurnHubLoginPage {
const char HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TurnHub · Profile login</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#0b0d11;color:#f3f5f7;font:1rem/1.5 system-ui,sans-serif;padding:20px}main{max-width:760px;margin:auto}a{color:#a9cbff}h1{margin-bottom:8px}.muted{color:#b5becb}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,280px),1fr));gap:16px}section{background:#151a22;border:1px solid #465367;border-radius:16px;padding:20px}label{display:block;margin:14px 0 5px}input,select,button{font:inherit;color:inherit;background:#222b39;border:1px solid #697c94;border-radius:8px;width:100%;min-height:48px;padding:10px}button{margin-top:20px;background:#dce9ff;color:#101824;font-weight:700;cursor:pointer}button:disabled{opacity:.6}a:focus-visible,input:focus-visible,select:focus-visible,button:focus-visible{outline:3px solid #ffda78;outline-offset:3px}#message{min-height:3em;padding:12px;border-left:3px solid #9fc6ff;overflow-wrap:anywhere}.hint{font-size:.9rem;color:#b5becb}h2{margin-top:0}
</style></head><body><main>
<a href="/">← Back to table</a><h1>Your TurnHub profile</h1>
<p class="muted">Your name and statistics stay with you. Play from a phone, a Sigil, or both. No physical Sigil is required.</p>
<p id="message" role="status" aria-live="polite" tabindex="-1">Loading saved profiles…</p>
<div class="grid"><section><h2>Sign in</h2>
<form id="loginForm"><label for="savedProfile">Saved profile</label><select id="savedProfile" required aria-describedby="loginHint"><option value="">Choose a profile</option></select>
<label for="loginPin">PIN</label><input id="loginPin" type="password" inputmode="numeric" pattern="[0-9]{4,8}" minlength="4" maxlength="8" autocomplete="current-password" required>
<p id="loginHint" class="hint">Use your 4–8 digit PIN. A profile without a saved PIN can still use physical sign-in at the table, then set a PIN in Settings.</p>
<button type="submit">Sign in</button></form></section>
<section><h2>Create a profile</h2><form id="registerForm">
<label for="newName">Display name</label><input id="newName" maxlength="32" autocomplete="nickname" required>
<label for="newPin">Choose a PIN</label><input id="newPin" type="password" inputmode="numeric" pattern="[0-9]{4,8}" minlength="4" maxlength="8" autocomplete="new-password" required>
<label for="confirmPin">Confirm PIN</label><input id="confirmPin" type="password" inputmode="numeric" pattern="[0-9]{4,8}" minlength="4" maxlength="8" autocomplete="new-password" required>
<p class="hint">Profiles are saved on this Atlas. Remember your PIN to sign in again from any phone on its network.</p>
<button type="submit">Create profile</button></form></section></div>
<p class="hint">Signing in does not add another player. Use “Join table” in the portal. If your profile is already playing, this phone controls that same player.</p>
</main><script>
const message=document.getElementById('message');
function notify(text){message.textContent=text;message.focus()}
async function loadProfiles(){try{const r=await fetch('/api/profiles',{cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not load profiles');for(const p of d.profiles){const option=document.createElement('option');option.value=p.profileId;option.textContent=`${p.name||'Unnamed profile'} · ${p.profileId}${p.hasPin?'':' · physical sign-in needed'}`;savedProfile.appendChild(option)}const requested=new URLSearchParams(location.search).get('profile');if(requested)savedProfile.value=requested;message.textContent=d.profiles.length?'Choose a saved profile or create your own.':'No saved profiles yet. Create one to begin.'}catch(e){notify(e.message)}}
async function submit(form,path,data){const button=form.querySelector('button');button.disabled=true;try{const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const d=await r.json();if(!r.ok)throw new Error(d.error||'Sign-in failed');localStorage.setItem('turnhubSessionToken',d.token);location.assign('/')}catch(e){notify(e.message);button.disabled=false}}
loginForm.addEventListener('submit',e=>{e.preventDefault();submit(loginForm,'/api/session/login',{profileId:savedProfile.value,pin:loginPin.value})});
registerForm.addEventListener('submit',e=>{e.preventDefault();if(newPin.value!==confirmPin.value){notify('The PINs do not match.');confirmPin.focus();return}submit(registerForm,'/api/profiles/register',{name:newName.value.trim(),pin:newPin.value})});
loadProfiles();
</script></body></html>
)HTML";
}
