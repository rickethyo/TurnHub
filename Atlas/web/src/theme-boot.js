// Runs in every pack page's <head>, before first paint: a saved theme, else
// what the device asks for (more contrast, light or dark). Midnight and
// Parchment were retired in V1 and map to their successors.
function deviceTheme(){const q=s=>matchMedia(s).matches;return q('(prefers-contrast: more)')?'contrast':q('(prefers-color-scheme: light)')?'daylight':'graphite'}
function savedTheme(){try{const t=localStorage.getItem('turnhubTheme');const m={midnight:'graphite',parchment:'daylight'}[t]||t;return ['graphite','daylight','brass','contrast'].includes(m)?m:null}catch(_){return null}}
try{const h=document.documentElement;h.dataset.theme=savedTheme()||deviceTheme();if(localStorage.getItem('turnhubReduceMotion')==='1')h.dataset.motion='reduce'}catch(_){}
