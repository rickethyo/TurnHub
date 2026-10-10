// Before first paint: accessibility appearance only; old decorative choices use System.
function deviceTheme(){const q=s=>matchMedia(s).matches;return q('(prefers-contrast: more)')?'contrast':q('(prefers-color-scheme: light)')?'daylight':'graphite'}
function savedTheme(){try{const t=localStorage.getItem('turnhubTheme');return ['graphite','daylight','contrast'].includes(t)?t:null}catch(_){return null}}
try{const h=document.documentElement;h.dataset.theme=savedTheme()||deviceTheme();if(localStorage.getItem('turnhubReduceMotion')==='1')h.dataset.motion='reduce'}catch(_){}
