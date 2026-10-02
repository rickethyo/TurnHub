#pragma once
#include <Arduino.h>
namespace TurnHubAtlas {
const char SIGIL_UPDATE_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Sigil firmware</title><link rel="stylesheet" href="/theme.css">
<body><main class="page narrow"><a href="/portal">Back to TurnHub</a><section class="card"><h1>Sigil firmware</h1>
<p>Verify at the table in Device Settings first. Update between games, one Sigil at a time. Each Sigil needs an initial USB installation of firmware with the updater.</p>
<form id="uploadForm"><label for="package">Signed Sigil package (.thfw)</label><input id="package" type="file" accept=".thfw" required><button id="uploadButton" disabled>Upload package</button></form>
<p id="packageInfo">Loading package status…</p>
<label for="sigil">Sigil to update</label><select id="sigil"></select><button id="startButton" type="button" disabled>Update selected Sigil</button>
<p id="job" role="status" aria-live="polite"></p><p id="message" role="status" aria-live="polite"></p></section></main>
<script>
const el=id=>document.getElementById(id); let state=null,devices=[],working=false,lastStage=null;
// Every call carries the portal sign-in; each one gives up after a while so the page never waits forever.
const auth=()=>({'X-TurnHub-Token':localStorage.getItem('turnhubSessionToken')||''});
async function request(url,options={}){const c=new AbortController(),t=setTimeout(()=>c.abort(),10000);
 try{const r=await fetch(url,{...options,cache:'no-store',headers:{...auth(),...(options.headers||{})},signal:c.signal});let j={};try{j=await r.json()}catch(_){}if(!r.ok)throw Error(j.error||'Request failed ('+r.status+')');return j;}
 catch(e){throw e.name==='AbortError'?Error('Atlas did not answer. Check the Wi-Fi connection and try again.'):e}finally{clearTimeout(t)}}
// The upload reports progress and fails with a message instead of hanging.
function uploadPackage(file){return new Promise((resolve,reject)=>{const x=new XMLHttpRequest();x.open('POST','/api/sigil-firmware');
 const token=auth()['X-TurnHub-Token'];x.setRequestHeader('X-TurnHub-Token',token);x.timeout=180000;
 x.upload.onprogress=e=>{if(e.lengthComputable)el('message').textContent='Uploading package… '+Math.round(e.loaded*100/e.total)+'%'};
 x.upload.onload=()=>{el('message').textContent='Checking package…'};
 x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(_){}x.status>=200&&x.status<300?resolve(j):reject(Error(j.error||'Upload failed ('+x.status+')'))};
 x.onerror=()=>reject(Error('The connection to Atlas was lost during the upload. Try again.'));
 x.ontimeout=()=>reject(Error('The upload took too long and was stopped. Try again.'));
 const form=new FormData();form.append('firmware',file);x.send(form);});}
function render(){
 el('packageInfo').textContent=state.staged?'Ready: '+(state.product===3?'OLED':'E-paper')+' '+state.version:'No package staged. Upload a signed package first.';
 const selected=el('sigil').value; el('sigil').replaceChildren();
 for(const d of devices){if(!d.online||!d.metadata||(d.capabilities&128))continue;
 const option=document.createElement('option');option.value=d.id;option.textContent=d.label+' · '+d.display+' · '+d.firmware;
 option.disabled=!state.staged||(d.display==='oled'?3:2)!==state.product;el('sigil').append(option);}
 if([...el('sigil').options].some(o=>o.value===selected&&!o.disabled))el('sigil').value=selected;
 else {const first=[...el('sigil').options].find(o=>!o.disabled);if(first)el('sigil').value=first.value;}
 el('uploadButton').disabled=working||state.busy;el('package').disabled=working||state.busy;
 el('startButton').disabled=working||state.busy||!el('sigil').selectedOptions.length||el('sigil').selectedOptions[0].disabled;
 const text=state.stage==='idle'?'No update running.':'Sigil '+(state.sigilId+1)+(state.stage==='failed'?' update failed: ':': ')+state.message+(state.stage==='downloading'?' '+state.progress+'%':'');
 if(el('job').textContent!==text)el('job').textContent=text;
 // The start reply ("Update started") must not stay as the last word: say how the job ended.
 if(lastStage&&lastStage!==state.stage){
  if(state.stage==='done')el('message').textContent='Update complete: Sigil '+(state.sigilId+1)+' restarted on the new firmware.';
  else if(state.stage==='failed')el('message').textContent='Update failed: '+state.message;
 }
 lastStage=state.stage;
}
async function refresh(){try{const [s,d]=await Promise.all([request('/api/sigil-firmware'),request('/api/devices')]);state=s;devices=d.devices;render();}catch(e){el('message').textContent=e.message;el('startButton').disabled=true;}finally{setTimeout(refresh,2000);}}
el('uploadForm').onsubmit=async e=>{e.preventDefault();if(!el('package').files.length)return;working=true;if(state)render();el('message').textContent='Uploading package…';try{const r=await uploadPackage(el('package').files[0]);el('message').textContent=r.message;}catch(e){el('message').textContent=e.message;}finally{working=false;if(state)render();}};
el('startButton').onclick=async()=>{working=true;render();try{const body=new URLSearchParams({module:el('sigil').value});const r=await request('/api/sigil-update',{method:'POST',body});el('message').textContent=r.message;}catch(e){el('message').textContent=e.message;}finally{working=false;}};
refresh();
</script></body></html>
)HTML";
}
