#include "web_pages.h"

namespace TurnHubWeb {

// Shared stylesheet for every Atlas page, served at /theme.css. Themes are
// token sets on [data-theme]; the choice is a per-browser preference only.
const char THEME_CSS[] PROGMEM = R"CSS(
/* TurnHub theme. Every page links /theme.css. A theme is only a set of tokens
   on [data-theme]; components never hard-code colours. Themes are a per-browser
   presentation preference and never affect game state. */
:root,[data-theme=brass]{color-scheme:dark;
--bg:#110d09;--glow-a:rgba(224,168,72,.17);--glow-b:rgba(95,212,176,.06);
--surface:#1b1510;--surface-2:#241c14;--surface-3:#30251a;--inset:#0c0906;
--line:rgba(222,176,100,.2);--line-strong:rgba(222,176,100,.44);
--text:#f6ecd9;--muted:#c9b594;--faint:#97866b;
--accent:#e2ae4a;--accent-hi:#f8d98f;--accent-lo:#9a681d;--on-accent:#1c1205;
--accent-grad:linear-gradient(180deg,#f8d98f 0%,#e0a944 45%,#a06d1f 100%);
--good:#93dc8c;--warn:#ffa25c;--bad:#ff7d70;--info:#a6c8ea;--active:#62d6b2;--focus:#ffe28f;
--card:linear-gradient(180deg,rgba(255,232,190,.045),rgba(255,232,190,0) 38%),var(--surface);
--shadow:0 1px 0 rgba(255,230,180,.07) inset,0 22px 44px -22px rgba(0,0,0,.85);
--radius:18px;--radius-sm:12px;
--font-body:system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",sans-serif;
--font-display:"Iowan Old Style","Palatino Linotype","Book Antiqua",Palatino,Georgia,serif;
--label-spacing:.18em;--rivets:block;--rivet-hi:#f7dc9c;--rivet-lo:#6b4a16;
--av-s:42%;--av-l:34%;--av-text:#fff6e4;--meta:#110d09}
[data-theme=midnight]{color-scheme:dark;
--bg:#0a0d14;--glow-a:rgba(232,180,79,.10);--glow-b:rgba(88,140,255,.10);
--surface:#121826;--surface-2:#182033;--surface-3:#202a40;--inset:#0a0f1a;
--line:rgba(160,182,224,.15);--line-strong:rgba(160,182,224,.3);
--text:#eef2f9;--muted:#a8b3c9;--faint:#76819a;
--accent:#e8b44f;--accent-hi:#ffd98a;--accent-lo:#a47520;--on-accent:#171003;
--accent-grad:linear-gradient(180deg,#ffd57f,#e3a73d);
--good:#72e3a2;--warn:#ffb25a;--bad:#ff7d88;--info:#94b9ff;--active:#5ad8ca;--focus:#ffd98a;
--card:var(--surface);--shadow:0 20px 40px -24px rgba(0,0,0,.8);--radius:20px;
--font-display:var(--font-body);--label-spacing:.12em;--rivets:none;
--av-s:48%;--av-l:38%;--av-text:#fff;--meta:#0a0d14}
[data-theme=parchment]{color-scheme:light;
--bg:#ece2cd;--glow-a:rgba(196,146,52,.22);--glow-b:rgba(60,130,110,.08);
--surface:#fbf6ea;--surface-2:#f3e9d4;--surface-3:#e9dbbd;--inset:#fffdf7;
--line:rgba(112,80,28,.22);--line-strong:rgba(112,80,28,.45);
--text:#2a1e10;--muted:#634f33;--faint:#806a4a;
--accent:#8e5f0c;--accent-hi:#c8952f;--accent-lo:#6b4708;--on-accent:#fffaf0;
--accent-grad:linear-gradient(180deg,#c99534,#8a5c0d);
--good:#1f6f3a;--warn:#9c4700;--bad:#ad2319;--info:#1f5a8f;--active:#0d7560;--focus:#1d4ed8;
--card:linear-gradient(180deg,rgba(255,255,255,.6),rgba(255,255,255,0) 40%),var(--surface);
--shadow:0 1px 0 #fff inset,0 16px 32px -20px rgba(80,50,10,.5);
--rivet-hi:#f1d48d;--rivet-lo:#7c5412;--av-s:40%;--av-l:40%;--av-text:#fff;--meta:#ece2cd}
[data-theme=contrast]{color-scheme:dark;
--bg:#000;--glow-a:transparent;--glow-b:transparent;
--surface:#000;--surface-2:#0e0e0e;--surface-3:#1c1c1c;--inset:#000;
--line:#bdbdbd;--line-strong:#fff;--text:#fff;--muted:#ececec;--faint:#d0d0d0;
--accent:#ffd400;--accent-hi:#ffe45c;--accent-lo:#ffd400;--on-accent:#000;--accent-grad:#ffd400;
--good:#7dff9b;--warn:#ffb000;--bad:#ff9a9a;--info:#8fd3ff;--active:#00f0d8;--focus:#fff;
--card:#000;--shadow:none;--font-display:var(--font-body);--label-spacing:.1em;--rivets:none;
--av-s:0%;--av-l:18%;--av-text:#fff;--meta:#000}

*,*::before,*::after{box-sizing:border-box}
html{-webkit-text-size-adjust:100%;text-size-adjust:100%}
body{margin:0;min-height:100vh;color:var(--text);font:16px/1.5 var(--font-body);background-color:var(--bg);
background-image:radial-gradient(1100px 560px at 12% -8%,var(--glow-a),transparent 62%),radial-gradient(900px 520px at 100% 4%,var(--glow-b),transparent 60%);background-repeat:no-repeat}
[hidden]{display:none!important}
a{color:var(--accent-hi)}[data-theme=parchment] a{color:var(--accent)}
h1,h2,h3{font-family:var(--font-display);line-height:1.15;margin:0;font-weight:700;letter-spacing:.005em}
h2{font-size:1.25rem}h3{font-size:1.05rem}
p{margin:.4rem 0}
.small,.hint{color:var(--muted);font-size:.86rem;line-height:1.5}
.faint{color:var(--faint)}
.mono{font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;font-size:.78rem;color:var(--muted);overflow-wrap:anywhere}
.sr-only{position:absolute!important;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}
:focus-visible{outline:3px solid var(--focus);outline-offset:3px}
.skip{position:absolute;left:12px;top:-60px;z-index:100;background:var(--accent);color:var(--on-accent);padding:8px 14px;border-radius:10px;font-weight:700}
.skip:focus{top:12px}
.i{width:1.2em;height:1.2em;flex:none;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}

/* Brand: an original gear mark drawn from plain geometry. */
.brand{display:flex;align-items:center;gap:11px;color:var(--text);text-decoration:none;min-width:0}
.brand-mark,.cog{display:block;flex:none;background:var(--accent-grad);
-webkit-mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E%3Cpath fill-rule='evenodd' d='M44.1 12.5L45.8 3.2A47 47 0 0 1 54.2 3.2L55.9 12.5A38 38 0 0 1 63.6 14.5L69.8 7.4A47 47 0 0 1 77 11.5L73.9 20.5A38 38 0 0 1 79.5 26.1L88.5 23A47 47 0 0 1 92.6 30.2L85.5 36.4A38 38 0 0 1 87.5 44.1L96.8 45.8A47 47 0 0 1 96.8 54.2L87.5 55.9A38 38 0 0 1 85.5 63.6L92.6 69.8A47 47 0 0 1 88.5 77L79.5 73.9A38 38 0 0 1 73.9 79.5L77 88.5A47 47 0 0 1 69.8 92.6L63.6 85.5A38 38 0 0 1 55.9 87.5L54.2 96.8A47 47 0 0 1 45.8 96.8L44.1 87.5A38 38 0 0 1 36.4 85.5L30.2 92.6A47 47 0 0 1 23 88.5L26.1 79.5A38 38 0 0 1 20.5 73.9L11.5 77A47 47 0 0 1 7.4 69.8L14.5 63.6A38 38 0 0 1 12.5 55.9L3.2 54.2A47 47 0 0 1 3.2 45.8L12.5 44.1A38 38 0 0 1 14.5 36.4L7.4 30.2A47 47 0 0 1 11.5 23L20.5 26.1A38 38 0 0 1 26.1 20.5L23 11.5A47 47 0 0 1 30.2 7.4L36.4 14.5A38 38 0 0 1 44.1 12.5ZM65 50A15 15 0 1 0 35 50A15 15 0 1 0 65 50Z'/%3E%3C/svg%3E") center/contain no-repeat;
mask:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E%3Cpath fill-rule='evenodd' d='M44.1 12.5L45.8 3.2A47 47 0 0 1 54.2 3.2L55.9 12.5A38 38 0 0 1 63.6 14.5L69.8 7.4A47 47 0 0 1 77 11.5L73.9 20.5A38 38 0 0 1 79.5 26.1L88.5 23A47 47 0 0 1 92.6 30.2L85.5 36.4A38 38 0 0 1 87.5 44.1L96.8 45.8A47 47 0 0 1 96.8 54.2L87.5 55.9A38 38 0 0 1 85.5 63.6L92.6 69.8A47 47 0 0 1 88.5 77L79.5 73.9A38 38 0 0 1 73.9 79.5L77 88.5A47 47 0 0 1 69.8 92.6L63.6 85.5A38 38 0 0 1 55.9 87.5L54.2 96.8A47 47 0 0 1 45.8 96.8L44.1 87.5A38 38 0 0 1 36.4 85.5L30.2 92.6A47 47 0 0 1 23 88.5L26.1 79.5A38 38 0 0 1 20.5 73.9L11.5 77A47 47 0 0 1 7.4 69.8L14.5 63.6A38 38 0 0 1 12.5 55.9L3.2 54.2A47 47 0 0 1 3.2 45.8L12.5 44.1A38 38 0 0 1 14.5 36.4L7.4 30.2A47 47 0 0 1 11.5 23L20.5 26.1A38 38 0 0 1 26.1 20.5L23 11.5A47 47 0 0 1 30.2 7.4L36.4 14.5A38 38 0 0 1 44.1 12.5ZM65 50A15 15 0 1 0 35 50A15 15 0 1 0 65 50Z'/%3E%3C/svg%3E") center/contain no-repeat}
.brand-mark{width:36px;height:36px}
.brand-name{display:block;font-family:var(--font-display);font-size:1.45rem;font-weight:700;line-height:1;letter-spacing:.02em}
.brand-sub{display:block;color:var(--muted);font-size:.72rem;letter-spacing:var(--label-spacing);text-transform:uppercase;margin-top:3px;white-space:nowrap}
@keyframes turn{to{transform:rotate(360deg)}}
[data-running] .brand-mark{animation:turn 14s linear infinite}

/* Cards: brass panels with corner rivets (only where the theme enables them). */
.card{position:relative;background:var(--card);border:1px solid var(--line);border-radius:var(--radius);padding:clamp(16px,2.2vw,24px);box-shadow:var(--shadow);min-width:0}
.card::before{content:"";position:absolute;inset:7px;pointer-events:none;display:var(--rivets);
background:radial-gradient(circle,var(--rivet-hi) 0 1px,var(--rivet-lo) 2.4px,transparent 3.2px) 0 0/7px 7px no-repeat,radial-gradient(circle,var(--rivet-hi) 0 1px,var(--rivet-lo) 2.4px,transparent 3.2px) 100% 0/7px 7px no-repeat,radial-gradient(circle,var(--rivet-hi) 0 1px,var(--rivet-lo) 2.4px,transparent 3.2px) 0 100%/7px 7px no-repeat,radial-gradient(circle,var(--rivet-hi) 0 1px,var(--rivet-lo) 2.4px,transparent 3.2px) 100% 100%/7px 7px no-repeat}
.card>*{position:relative}
.card+.card{margin-top:0}
.card-head{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;flex-wrap:wrap;margin-bottom:14px}
.card-head>div{min-width:0}
.eyebrow{display:flex;align-items:center;gap:10px;margin:0;color:var(--accent-hi);font:700 .74rem/1.2 var(--font-body);letter-spacing:var(--label-spacing);text-transform:uppercase}
[data-theme=parchment] .eyebrow{color:var(--accent)}
.eyebrow::after{content:"";flex:1;min-width:24px;height:1px;background:linear-gradient(90deg,var(--line-strong),transparent)}
.card-head .eyebrow{flex:1}
.card h2:not(.eyebrow){margin-bottom:6px}

/* Controls */
button,.btn{-webkit-appearance:none;appearance:none;font:inherit;font-weight:650;font-size:.95rem;line-height:1.2;min-height:44px;padding:10px 16px;border-radius:var(--radius-sm);border:1px solid var(--line-strong);background:var(--surface-2);color:var(--text);cursor:pointer;display:inline-flex;align-items:center;justify-content:center;gap:8px;text-decoration:none;text-align:center;transition:background-color .15s,border-color .15s,transform .08s,box-shadow .15s}
button:hover:not(:disabled),.btn:hover{border-color:var(--accent);background:var(--surface-3)}
button:active:not(:disabled),.btn:active{transform:translateY(1px)}
button:disabled{opacity:.42;cursor:not-allowed}
button.primary,.btn.primary{background:var(--accent-grad);color:var(--on-accent);border-color:var(--accent-lo);box-shadow:0 1px 0 rgba(255,255,255,.35) inset,0 8px 22px -12px var(--accent)}
button.primary:disabled{background:var(--surface-2);color:var(--muted);border-color:var(--line-strong);box-shadow:none}
button.primary:hover:not(:disabled),.btn.primary:hover{filter:brightness(1.07);background:var(--accent-grad)}
button.good{color:var(--good);border-color:var(--good)}
button.warn{color:var(--warn);border-color:var(--warn)}
button.bad{color:var(--bad);border-color:color-mix(in srgb,var(--bad) 60%,transparent)}
button.blue{color:var(--info);border-color:color-mix(in srgb,var(--info) 55%,transparent)}
button.ghost,.btn.ghost{background:transparent;border-color:var(--line)}
button.small,.btn.small{min-height:36px;padding:7px 12px;font-size:.86rem}
.actions{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
input,select,textarea{font:inherit;color:var(--text);background:var(--inset);border:1px solid var(--line-strong);border-radius:var(--radius-sm);min-height:46px;padding:10px 13px;width:100%;max-width:100%}
input::placeholder{color:var(--faint)}
input:focus,select:focus{border-color:var(--accent)}
select{-webkit-appearance:none;appearance:none;padding-right:38px;cursor:pointer;background-image:linear-gradient(45deg,transparent 50%,var(--muted) 50%),linear-gradient(135deg,var(--muted) 50%,transparent 50%);background-position:calc(100% - 19px) 52%,calc(100% - 14px) 52%;background-size:5px 5px;background-repeat:no-repeat}
input[type=number]{font-variant-numeric:tabular-nums}
/* Every checkbox is a real input drawn as a switch: the knob position, not only its colour, shows the state. */
input[type=checkbox]{-webkit-appearance:none;appearance:none;width:48px;height:28px;min-height:0;padding:0;margin:0;border-radius:99px;background:var(--inset);border:1px solid var(--line-strong);position:relative;flex:none;cursor:pointer;vertical-align:middle;transition:background-color .15s,border-color .15s}
input[type=checkbox]::before{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:var(--faint);transition:transform .15s,background-color .15s}
input[type=checkbox]:checked{background:var(--accent-lo);border-color:var(--accent)}
input[type=checkbox]:checked::before{transform:translateX(20px);background:var(--accent-hi)}
[data-theme=contrast] input[type=checkbox]:checked::before{background:#000}
input[type=radio]{accent-color:var(--accent);width:20px;height:20px;min-height:0;margin:0;flex:none}
fieldset{border:0;margin:0;padding:0;min-width:0}
legend{padding:0;margin-bottom:6px;font-weight:700;font-family:var(--font-display);font-size:1.05rem}
fieldset:disabled{opacity:.85}
label{display:block;color:var(--muted);font-size:.8rem;font-weight:650;letter-spacing:.04em;margin:14px 0 6px}
.field{display:grid;gap:0}.field label{margin-top:0}
.inline{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:center}
.switch-row{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:12px 0;border-bottom:1px solid var(--line);margin:0;cursor:pointer;color:var(--text);font-size:.95rem;font-weight:600;letter-spacing:0}
.switch-row:last-of-type{border-bottom:0}
.switch-row label{margin:0;color:var(--text);font-size:.95rem;font-weight:600;letter-spacing:0;cursor:pointer}
.switch-row>div{min-width:0}
.switch-row small{display:block;color:var(--muted);font-weight:400;font-size:.84rem;margin-top:2px}

/* Status atoms: every state carries text; colour only reinforces it. */
.badges{display:flex;flex-wrap:wrap;gap:7px}
.badge{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--line-strong);border-radius:99px;padding:4px 11px 4px 9px;font-size:.76rem;font-weight:700;letter-spacing:.02em;color:var(--muted);background:var(--inset);white-space:nowrap}
.badge::before{content:"";width:7px;height:7px;border-radius:50%;background:currentColor}
.badge.good{color:var(--good);border-color:color-mix(in srgb,var(--good) 45%,transparent)}
.badge.warn{color:var(--warn);border-color:color-mix(in srgb,var(--warn) 45%,transparent)}
.badge.bad{color:var(--bad);border-color:color-mix(in srgb,var(--bad) 45%,transparent)}
.badge.blue{color:var(--info);border-color:color-mix(in srgb,var(--info) 45%,transparent)}
.badge.active{color:var(--active);border-color:color-mix(in srgb,var(--active) 50%,transparent)}
.notice{border:1px solid var(--line);border-left:4px solid var(--warn);background:var(--surface-2);padding:13px 15px;border-radius:var(--radius-sm);color:var(--muted);line-height:1.5}
.notice strong{color:var(--text)}
.notice.good{border-left-color:var(--good)}.notice.blue,.notice.info{border-left-color:var(--info)}.notice.bad{border-left-color:var(--bad)}
.notice p{margin:.2rem 0 .7rem}
.status-row{display:flex;justify-content:space-between;align-items:baseline;gap:14px;padding:10px 0;border-bottom:1px dashed var(--line)}
.status-row:last-child{border-bottom:0}
.status-row>span{color:var(--muted)}
.status-row strong{text-align:right;font-weight:650;overflow-wrap:anywhere}
.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}
.metric{background:var(--inset);border:1px solid var(--line);border-radius:var(--radius-sm);padding:11px 13px}
.metric label{margin:0;font-size:.68rem;letter-spacing:var(--label-spacing);text-transform:uppercase;color:var(--faint)}
.metric strong{display:block;margin-top:3px;font:700 1.25rem/1.2 var(--font-display);font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.empty{color:var(--muted);padding:22px 16px;border:1px dashed var(--line-strong);border-radius:var(--radius-sm);text-align:center}
.msg{min-height:1.5em;margin:10px 0 0;color:var(--muted);font-size:.9rem}
.msg:empty{min-height:0;margin:0}
.avatar{--hue:38;display:inline-grid;place-items:center;flex:none;width:44px;height:44px;border-radius:50%;background:hsl(var(--hue) var(--av-s) var(--av-l));color:var(--av-text);font:700 1rem/1 var(--font-display);border:2px solid var(--line-strong);box-shadow:0 0 0 3px var(--inset) inset}
.avatar.lg{width:56px;height:56px;font-size:1.25rem}
details.drawer{border:1px solid var(--line);border-radius:var(--radius-sm);background:var(--inset);margin-top:12px}
details.drawer>summary{list-style:none;cursor:pointer;padding:13px 16px;font-weight:650;display:flex;align-items:center;justify-content:space-between;gap:10px}
details.drawer>summary::-webkit-details-marker{display:none}
details.drawer>summary::after{content:"";width:8px;height:8px;border-right:2px solid var(--muted);border-bottom:2px solid var(--muted);transform:rotate(45deg);transition:transform .15s}
details.drawer[open]>summary::after{transform:rotate(-135deg)}
details.drawer>:not(summary){padding:0 16px 16px}

/* Toast and dialog */
.toast{position:fixed;z-index:80;left:50%;bottom:calc(20px + env(safe-area-inset-bottom));transform:translate(-50%,16px);width:min(520px,calc(100% - 28px));background:var(--surface-3);border:1px solid var(--line-strong);border-left:4px solid var(--accent);border-radius:var(--radius-sm);padding:13px 16px;box-shadow:0 22px 50px -12px rgba(0,0,0,.6);opacity:0;pointer-events:none;transition:opacity .18s,transform .18s;color:var(--text);font-weight:550}
.toast.show{opacity:1;transform:translate(-50%,0)}
.toast.bad{border-left-color:var(--bad)}
dialog{border:1px solid var(--line-strong);border-radius:var(--radius);background:var(--surface);color:var(--text);padding:0;width:min(440px,calc(100% - 32px));box-shadow:0 30px 80px -20px rgba(0,0,0,.8)}
dialog::backdrop{background:rgba(5,3,1,.62);-webkit-backdrop-filter:blur(3px);backdrop-filter:blur(3px)}
dialog form{padding:22px}
dialog h2{margin-bottom:8px}
dialog p{color:var(--muted)}
dialog .actions{justify-content:flex-start;flex-direction:row-reverse;margin-top:18px}

/* Secondary pages (sign in, statistics, developer, firmware) */
.page{width:min(1040px,100%);margin:0 auto;padding:max(16px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) max(28px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left))}
.page.narrow{width:min(680px,100%)}
.page-head{display:flex;justify-content:space-between;align-items:center;gap:14px;flex-wrap:wrap;margin:6px 0 20px}
.page-title{margin:0 0 20px}
.page-title h1{font-size:clamp(1.8rem,5vw,2.6rem)}
.page-title p{color:var(--muted);max-width:60ch}
.stack{display:grid;gap:16px}
.cols{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(min(100%,300px),1fr))}
pre{white-space:pre-wrap;overflow-wrap:anywhere;margin:0;font:.78rem/1.55 ui-monospace,"SF Mono",Menlo,Consolas,monospace;color:var(--muted);background:var(--inset);border:1px solid var(--line);border-radius:var(--radius-sm);padding:14px;max-height:420px;overflow:auto}
.foot{color:var(--faint);font-size:.78rem;text-align:center;margin:26px 0 8px;letter-spacing:.02em}
@media (prefers-reduced-motion:reduce){*,*::before,*::after{animation:none!important;transition:none!important;scroll-behavior:auto!important}}
@media (forced-colors:active){.card::before{display:none}input[type=checkbox]{appearance:auto;width:22px;height:22px}input[type=checkbox]::before{display:none}.brand-mark,.cog{forced-color-adjust:none}}
)CSS";

const char PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#110d09">
<title>TurnHub</title>
<script>try{document.documentElement.dataset.theme=localStorage.getItem('turnhubTheme')||'brass'}catch(_){}</script>
<link rel="stylesheet" href="/theme.css">
<style>
body{padding-bottom:calc(96px + env(safe-area-inset-bottom))}
.shell{width:min(2000px,100%);margin:0 auto;padding:0 max(14px,env(safe-area-inset-right)) 0 max(14px,env(safe-area-inset-left))}
.appbar{position:sticky;top:0;z-index:40;display:flex;align-items:center;gap:16px;padding:max(10px,env(safe-area-inset-top)) 0 10px;margin-bottom:12px;background:color-mix(in srgb,var(--bg) 86%,transparent);-webkit-backdrop-filter:blur(12px);backdrop-filter:blur(12px);border-bottom:1px solid var(--line)}
.appbar-end{display:flex;align-items:center;gap:8px;margin-left:auto}
.conn{display:inline-flex;align-items:center;gap:8px;border:1px solid var(--line-strong);border-radius:99px;padding:6px 12px;font-size:.8rem;font-weight:650;color:var(--muted);background:var(--inset);white-space:nowrap}
.dot{width:9px;height:9px;border-radius:50%;background:var(--bad);box-shadow:0 0 0 3px color-mix(in srgb,var(--bad) 22%,transparent)}
.dot.online{background:var(--good);box-shadow:0 0 0 3px color-mix(in srgb,var(--good) 22%,transparent)}
.signin{min-height:40px;padding:8px 16px}
.menu-wrap{position:relative}
.acct-btn{min-height:44px;padding:4px 12px 4px 4px;border-radius:99px;gap:9px;background:var(--inset)}
.acct-btn .avatar{width:34px;height:34px;font-size:.8rem;box-shadow:none}
.acct-name{max-width:14ch;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.caret{width:7px;height:7px;border-right:2px solid var(--muted);border-bottom:2px solid var(--muted);transform:translateY(-2px) rotate(45deg);transition:transform .15s}
.acct-btn[aria-expanded=true] .caret{transform:translateY(1px) rotate(-135deg)}
.menu{position:absolute;right:0;top:calc(100% + 8px);z-index:60;min-width:240px;padding:6px;background:var(--surface);border:1px solid var(--line-strong);border-radius:var(--radius-sm);box-shadow:0 24px 50px -16px rgba(0,0,0,.75)}
.menu-head{display:grid;padding:10px 12px 12px;border-bottom:1px solid var(--line);margin-bottom:6px}
.menu-head strong{font-family:var(--font-display);font-size:1.05rem}
.menu-head span{color:var(--muted);font-size:.8rem}
.menu-item{display:flex;align-items:center;justify-content:flex-start;gap:10px;width:100%;min-height:44px;padding:10px 12px;border:0;border-radius:10px;background:transparent;color:var(--text);font-weight:600;font-size:.95rem;text-decoration:none}
.menu-item:hover:not(:disabled){background:var(--surface-3);border-color:transparent}
.menu-item.danger{color:var(--bad)}
.tabs{display:flex;gap:4px;padding:4px;background:var(--inset);border:1px solid var(--line);border-radius:15px}
.tab{min-height:40px;border:1px solid transparent;background:transparent;color:var(--muted);padding:8px 14px;border-radius:11px;font-weight:650;white-space:nowrap}
.tab:hover:not(:disabled){background:var(--surface-2);border-color:var(--line)}
.tab.active,.tab.active:hover:not(:disabled){background:var(--accent-grad);color:var(--on-accent);border-color:var(--accent-lo);box-shadow:0 1px 0 rgba(255,255,255,.3) inset}
.req-alert{position:sticky;top:76px;z-index:35;display:grid;grid-template-columns:minmax(0,1fr) auto;align-items:center;gap:12px;margin-bottom:14px;color:var(--text);border-left-color:var(--warn);background:var(--surface-3);box-shadow:0 16px 36px -14px rgba(0,0,0,.7);overflow:hidden}
.req-alert .bar{position:absolute;left:0;bottom:0;height:3px;background:var(--warn);transition:width .8s linear}
#adminSetup{margin-bottom:14px}
.board{display:grid;gap:16px}
.board>.col{display:contents}
.stage{order:1}.seat-card{order:2}.life-card{order:3}#commanderPanel{order:4}.setup-card{order:5}.table-card{order:6}
.grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:16px}
.grid>*{grid-column:span 12}
/* Stage: the turn, told by a brass gauge and in words. */
.stage{display:grid;grid-template-columns:1fr;justify-items:center;text-align:center;gap:18px;overflow:hidden;padding:clamp(20px,3vw,34px)}
.stage-cog{position:absolute!important;right:-110px;top:-120px;width:330px;height:330px;opacity:.07;pointer-events:none}
[data-running] .stage-cog{animation:turn 60s linear infinite reverse}
.stage-text{min-width:0;max-width:100%}
#heroTitle{font-size:clamp(2.1rem,6vw,4.2rem);line-height:1.02;margin:.35rem 0 .45rem;overflow-wrap:anywhere;letter-spacing:.01em}
.stage p{color:var(--muted);font-size:1.02rem;margin:0 0 14px;font-variant-numeric:tabular-nums}
.stage .badges{justify-content:center}
.stage .eyebrow{justify-content:center}
.stage .eyebrow::after{display:none}
.gauge{position:relative;width:min(240px,64vw);aspect-ratio:1;flex:none}
.gauge svg{display:block;width:100%;height:100%;overflow:visible}
.g-bezel{fill:url(#bezelGrad)}
.stop-hi{stop-color:var(--accent-hi)}.stop-mid{stop-color:var(--accent)}.stop-lo{stop-color:var(--accent-lo)}
.g-face{fill:var(--inset);stroke:var(--line-strong);stroke-width:1}
.g-ticks line{stroke:var(--faint);stroke-width:1.4;stroke-linecap:round}
.g-ticks line.major{stroke:var(--muted);stroke-width:2.6}
.g-track{fill:none;stroke:var(--surface-3);stroke-width:9;stroke-linecap:round}
.g-arc{fill:none;stroke:var(--gauge,var(--accent));stroke-width:9;stroke-linecap:round;transition:stroke-dasharray .7s cubic-bezier(.3,.8,.3,1),stroke .3s}
.g-needle{transform-origin:100px 100px;transition:transform .7s cubic-bezier(.3,1.4,.4,1)}
.g-needle path{fill:var(--gauge,var(--accent-hi))}
.g-needle.snap,.g-arc.snap{transition:none}
.g-hub{fill:url(#bezelGrad);stroke:var(--accent-lo);stroke-width:1}
.g-hub-dot{fill:var(--inset)}
.stage[data-phase=counting]{--gauge:var(--active)}
.stage[data-phase=warning]{--gauge:var(--warn)}
.stage[data-phase=expired]{--gauge:var(--bad)}
.gauge-readout{position:absolute;left:50%;bottom:13%;transform:translateX(-50%);width:44%;text-align:center;line-height:1.05}
.gauge-readout strong{display:block;font:700 clamp(1.25rem,4.4vw,1.7rem)/1.05 var(--font-display);font-variant-numeric:tabular-nums;white-space:nowrap}
.gauge-readout span{display:block;color:var(--muted);font-size:.62rem;letter-spacing:var(--label-spacing);text-transform:uppercase;margin-top:3px;white-space:nowrap}
.stage[data-phase=warning] .gauge-readout strong{color:var(--warn)}.stage[data-phase=expired] .gauge-readout strong{color:var(--bad)}
/* Seat */
.who{display:flex;align-items:center;gap:13px;min-width:0}
.who-text{min-width:0;flex:1}
.who-name{font:700 1.3rem/1.2 var(--font-display);overflow-wrap:anywhere}
.seat-card .badges{margin-top:12px}
.seat-card .badges:empty{display:none}
.control-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:9px;margin-top:16px}
.control-grid .wide{grid-column:1/-1}
.control-grid .xl{min-height:64px;font-size:1.15rem;font-family:var(--font-display);letter-spacing:.02em}
#claimHelp{margin-top:14px}
#claimHelp .btn{width:100%}
/* Life */
.life-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(100%,210px),1fr));gap:12px}
.life-grid>.empty,.roster>.empty,.devices>.empty{grid-column:1/-1}
.life-player{position:relative;display:grid;gap:10px;align-content:start;background:var(--surface-2);border:1px solid var(--line);border-radius:16px;padding:14px;min-width:0;transition:border-color .2s,box-shadow .2s}
.life-player.active{border-color:var(--active);box-shadow:0 0 0 1px var(--active) inset,0 0 36px -12px var(--active)}
.life-player.me{background:linear-gradient(180deg,color-mix(in srgb,var(--accent) 9%,transparent),transparent 70%),var(--surface-2)}
.life-player.eliminated{opacity:.55}
.life-head{display:flex;align-items:center;gap:10px;min-width:0}
.life-head .avatar{width:34px;height:34px;font-size:.85rem}
.life-head>div{min-width:0;flex:1}
.life-head h3{font-size:1rem;overflow-wrap:anywhere;line-height:1.2}
.life-tag{display:block;font-size:.66rem;font-weight:700;letter-spacing:.06em;text-transform:uppercase;color:var(--muted)}
.life-player.active .life-tag{color:var(--active)}
.life-num{position:relative;display:flex;justify-content:center;align-items:center;min-height:82px;border-radius:12px;background:var(--inset);border:1px solid var(--line)}
.life-total{font:700 clamp(2.8rem,7vw,3.6rem)/1 var(--font-display);font-variant-numeric:tabular-nums lining-nums;letter-spacing:-.01em}
.life-delta{position:absolute;right:10px;top:8px;font-weight:800;font-size:.9rem;font-variant-numeric:tabular-nums;animation:pop .35s ease-out}
.life-delta.up{color:var(--good)}.life-delta.down{color:var(--bad)}
@keyframes pop{from{transform:translateY(6px);opacity:0}}
.life-buttons{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px}
.life-buttons button{padding:8px 2px;min-height:46px;font-variant-numeric:tabular-nums;font-size:1rem}
.life-player .small{margin:0;font-size:.76rem;text-align:center}
.my-life{display:flex;align-items:baseline;justify-content:space-between;gap:12px;margin-top:16px;padding:12px 16px;border-radius:var(--radius-sm);background:var(--inset);border:1px solid var(--line)}
.my-life h3{font-size:1rem;color:var(--muted);font-family:var(--font-body);font-weight:650}
.my-life strong{font:700 1.9rem/1 var(--font-display);font-variant-numeric:tabular-nums}
.pad{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-top:12px}
.requests{margin-top:16px;padding-top:16px;border-top:1px dashed var(--line-strong)}
.requests h3{margin-bottom:6px}
#lifeRequestActions .actions{margin:8px 0}
#lifeRequestActions button{flex:1}
/* Players and devices */
.roster,.devices{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(100%,290px),1fr));gap:12px}
.player,.device{background:var(--surface-2);border:1px solid var(--line);border-radius:16px;padding:15px;min-width:0}
.player.active{border-color:var(--active);box-shadow:0 0 0 1px var(--active) inset}
.player.winner{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset}
.player.eliminated{opacity:.6}
.player-head,.device-top{display:flex;align-items:center;gap:12px;min-width:0}
.player-head>div,.device-top>div{min-width:0;flex:1}
.player-name,.device-name{font:700 1.08rem/1.25 var(--font-display);overflow-wrap:anywhere}
.player-state{font-size:.7rem;font-weight:800;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);text-align:right}
.player-state.on{color:var(--active)}
.player .badges,.device .badges{margin-top:10px}
.player .actions,.device .actions{margin-top:12px}
.device-state{font-size:.7rem;font-weight:800;letter-spacing:.08em;display:inline-flex;align-items:center;gap:6px}
.device-state::before{content:"";width:8px;height:8px;border-radius:50%;background:currentColor}
.online-text{color:var(--good)}.offline-text{color:var(--bad)}
.seats{display:grid;gap:8px;margin-top:12px}
.seat{display:grid;gap:8px;background:var(--inset);border:1px solid var(--line);border-radius:12px;padding:10px 12px}
.seat.active{border-color:var(--active)}
.seat-top{display:flex;justify-content:space-between;align-items:center;gap:8px}
.slot{display:inline-grid;place-items:center;width:26px;height:26px;border-radius:8px;border:1px solid var(--line-strong);font-weight:800;margin-right:8px;color:var(--accent-hi);font-size:.85rem}
.qr-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:14px}
.qr-box{display:grid;justify-items:center;gap:10px;text-align:center;background:var(--inset);border:1px solid var(--line);border-radius:14px;padding:14px}
.qr-box h3{font-size:.95rem}
.qr-box>div{background:#fff;border-radius:10px;padding:6px;width:min(190px,100%)}
.qr-box svg{display:block;width:100%;height:auto;aspect-ratio:1}
.qr-box a{font-weight:650;font-size:.9rem}
.setting-box{background:var(--surface-2);border:1px solid var(--line);border-radius:14px;padding:14px;min-width:0}
.acct{display:grid;gap:4px}
.acct .switch-row{padding:9px 0;font-size:.9rem}
.acct+.acct,.setting-box+.setting-box{margin-top:10px}
.perm-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:0 20px;margin:6px 0 10px}
/* Appearance picker */
.themes{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:8px}
.theme-opt{display:grid;grid-template-columns:auto 1fr;align-items:center;gap:6px 10px;margin:0;padding:12px;border:1px solid var(--line);border-radius:14px;background:var(--inset);cursor:pointer;color:var(--text);font-size:.92rem;letter-spacing:0}
.theme-opt:has(input:checked){border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset}
.theme-opt strong{font-weight:700}
.swatch{grid-column:1/-1;display:flex;height:26px;border-radius:8px;overflow:hidden;border:1px solid var(--line-strong)}
.swatch i{flex:1}
.theme-opt small{grid-column:1/-1;font-weight:400;color:var(--muted);font-size:.78rem;line-height:1.35}
.foot-note{color:var(--faint);font-size:.78rem;text-align:center;margin:26px 0 6px}
@media (max-width:819px){
 /* backdrop-filter would become the containing block of the fixed tab bar */
 .appbar{gap:10px;-webkit-backdrop-filter:none;backdrop-filter:none;background:var(--bg)}
 .brand-sub{display:none}
 .tabs{position:fixed;z-index:45;left:0;right:0;bottom:0;border-radius:18px 18px 0 0;border-width:1px 0 0;padding:6px max(8px,env(safe-area-inset-left)) calc(6px + env(safe-area-inset-bottom));display:grid;grid-auto-flow:column;grid-auto-columns:1fr;background:color-mix(in srgb,var(--surface) 94%,transparent);-webkit-backdrop-filter:blur(14px);backdrop-filter:blur(14px);box-shadow:0 -12px 30px -18px rgba(0,0,0,.7)}
 .tab{display:flex;flex-direction:column;gap:3px;min-height:56px;padding:6px 2px;font-size:.7rem;border-radius:12px;white-space:normal;line-height:1.1}
 .tab .i{width:22px;height:22px}
 .toast{bottom:calc(88px + env(safe-area-inset-bottom))}
 .req-alert{top:70px;grid-template-columns:1fr}
 .acct-name{display:none}
 .acct-btn{padding:4px 10px 4px 4px}
 /* When connected, the dot is enough on a phone; problems always show in words. */
 .conn.ok .conn-text{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0)}
 .conn.ok{padding:9px}
 .conn{padding:6px 10px}
}
@media (min-width:820px){
 body{padding-bottom:24px}
 .grid .half{grid-column:span 6}.grid .third{grid-column:span 4}.grid .two-third{grid-column:span 8}
 .stage{grid-template-columns:auto minmax(0,1fr);justify-items:start;text-align:left;gap:clamp(20px,4vw,48px)}
 .stage .badges,.stage .eyebrow{justify-content:flex-start}
 .gauge{width:clamp(200px,20vw,260px)}
}
@media (min-width:1000px){
 .board{grid-template-columns:minmax(0,1fr) minmax(320px,400px);align-items:start}
 .board>.col{display:flex;flex-direction:column;gap:16px;min-width:0}
}
@media (min-width:1500px){.board{grid-template-columns:minmax(0,1fr) 440px}}
</style>
</head>
<body>
<svg width="0" height="0" style="position:absolute" aria-hidden="true" focusable="false">
<symbol id="i-game" viewBox="0 0 24 24"><circle cx="12" cy="13" r="8"/><path d="M12 13l4-4M12 5V3M9.5 3h5M19.5 6.5l1-1"/></symbol>
<symbol id="i-players" viewBox="0 0 24 24"><circle cx="9" cy="8" r="3.4"/><path d="M2.8 20c.8-3.5 3.3-5.4 6.2-5.4s5.4 1.9 6.2 5.4"/><circle cx="17.2" cy="9" r="2.6"/><path d="M16.8 14.6c2.4.3 4 2 4.5 5.4"/></symbol>
<symbol id="i-account" viewBox="0 0 24 24"><circle cx="12" cy="8" r="4"/><path d="M4.5 21c.9-4.1 3.9-6.4 7.5-6.4s6.6 2.3 7.5 6.4"/></symbol>
<symbol id="i-settings" viewBox="0 0 24 24"><path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2.2"/><circle cx="9" cy="17" r="2.2"/></symbol>
<symbol id="i-dev" viewBox="0 0 24 24"><rect x="3" y="4.5" width="18" height="15" rx="2.5"/><path d="M7.5 10l2.5 2.5-2.5 2.5M12.5 15h4"/></symbol>
<symbol id="i-stats" viewBox="0 0 24 24"><path d="M4 20V10M10 20V4M16 20v-7M21 20H3"/></symbol>
<symbol id="i-exit" viewBox="0 0 24 24"><path d="M14 4h4.5A1.5 1.5 0 0 1 20 5.5v13a1.5 1.5 0 0 1-1.5 1.5H14M10 16l-4-4 4-4M6 12h10"/></symbol>
<symbol id="i-life" viewBox="0 0 24 24"><path d="M12 20s-7.5-4.6-7.5-10.2A4.2 4.2 0 0 1 12 7.4a4.2 4.2 0 0 1 7.5 2.4C19.5 15.4 12 20 12 20z"/></symbol>
</svg>
<a class="skip" href="#main">Skip to content</a>
<div class="shell">
<header class="appbar">
 <a class="brand" href="/"><span class="brand-mark" aria-hidden="true"></span><span><span class="brand-name">TurnHub</span><span class="brand-sub">Atlas table console</span></span></a>
 <nav class="tabs" aria-label="Portal sections">
  <button class="tab active" data-tab="game" onclick="showTab('game')"><svg class="i" aria-hidden="true"><use href="#i-game"/></svg><span>Game</span></button>
  <button class="tab" data-tab="players" onclick="showTab('players')"><svg class="i" aria-hidden="true"><use href="#i-players"/></svg><span>Players</span></button>
  <button class="tab" data-tab="account" onclick="showTab('account')"><svg class="i" aria-hidden="true"><use href="#i-account"/></svg><span>My Account</span></button>
  <button id="deviceSettingsTab" class="tab" data-tab="settings" onclick="showTab('settings')" hidden><svg class="i" aria-hidden="true"><use href="#i-settings"/></svg><span>Device Settings</span></button>
 </nav>
 <div class="appbar-end">
  <span id="connPill" class="conn"><span id="dot" class="dot"></span><span id="connection" class="conn-text">Connecting</span></span>
  <a id="signInButton" class="btn primary signin" href="/login"><svg class="i" aria-hidden="true"><use href="#i-account"/></svg><span>Sign in</span></a>
  <div id="accountMenuWrap" class="menu-wrap" hidden>
   <button id="accountButton" class="acct-btn" aria-expanded="false" aria-controls="accountMenu" aria-label="Account menu" onclick="toggleAccountMenu()"><span id="accountAvatar" class="avatar" aria-hidden="true">?</span><span id="accountName" class="acct-name"></span><span class="caret" aria-hidden="true"></span></button>
   <div id="accountMenu" class="menu" hidden>
    <div class="menu-head"><strong id="menuName"></strong><span id="menuMeta"></span></div>
    <button class="menu-item" onclick="closeAccountMenu();showTab('account')"><svg class="i" aria-hidden="true"><use href="#i-account"/></svg>Account settings</button>
    <a class="menu-item" href="/stats"><svg class="i" aria-hidden="true"><use href="#i-stats"/></svg>My statistics</a>
    <a id="devLink" class="menu-item" href="/dev" hidden><svg class="i" aria-hidden="true"><use href="#i-dev"/></svg>Developer</a>
    <button class="menu-item danger" onclick="closeAccountMenu();logoutSession()"><svg class="i" aria-hidden="true"><use href="#i-exit"/></svg>Log out</button>
   </div>
  </div>
 </div>
</header>

<section id="lifeRequestAlert" class="notice req-alert" hidden aria-label="Pending life change"><span id="lifeAlertText" role="status" aria-live="polite"></span><button class="primary" onclick="reviewLifeRequest()">Review life request</button><span id="lifeAlertBar" class="bar" aria-hidden="true"></span></section>
<section id="adminSetup" class="notice good" hidden><strong>Set up this Atlas</strong><p>Create or sign into your account in My Account. Then hold the physical Atlas master button and select the button below to establish the initial Admin.</p><button class="primary" onclick="setupAdmin()">Make my account the initial Admin</button></section>

<main id="main">
<section id="view-game" class="view active">
<h1 class="sr-only">Game</h1>
<div class="board">
<div class="col">
 <section id="stage" class="card stage" data-state="" data-phase="idle">
  <span class="cog stage-cog" aria-hidden="true"></span>
  <div class="gauge" aria-hidden="true">
   <svg viewBox="0 0 200 200" focusable="false">
    <defs><linearGradient id="bezelGrad" x1="0" y1="0" x2="0" y2="1"><stop offset="0" class="stop-hi"/><stop offset=".55" class="stop-mid"/><stop offset="1" class="stop-lo"/></linearGradient></defs>
    <circle class="g-bezel" cx="100" cy="100" r="98"/>
    <circle class="g-face" cx="100" cy="100" r="88"/>
    <g id="gaugeTicks" class="g-ticks"></g>
    <path class="g-track" d="M53.3 146.7A66 66 0 1 1 146.7 146.7" pathLength="100"/>
    <path id="gaugeArc" class="g-arc" d="M53.3 146.7A66 66 0 1 1 146.7 146.7" pathLength="100" stroke-dasharray="0 100"/>
    <g id="gaugeNeedle" class="g-needle" style="transform:rotate(-135deg)"><path d="M100 38L104.5 100L100 114L95.5 100Z"/></g>
    <circle class="g-hub" cx="100" cy="100" r="10"/><circle class="g-hub-dot" cx="100" cy="100" r="3.5"/>
   </svg>
   <div class="gauge-readout"><strong id="gaugeValue">—</strong><span id="gaugeCaption">Atlas</span></div>
  </div>
  <div class="stage-text"><div id="eyebrow" class="eyebrow">TurnHub</div><h2 id="heroTitle">READY</h2><p id="heroSub">Waiting for Atlas</p><div id="heroBadges" class="badges"></div></div>
 </section>

 <section id="lifeCard" class="card life-card" hidden>
  <div class="card-head"><div style="flex:1"><h2 class="eyebrow">Life</h2><p id="gameProfileSummary" class="small"></p></div></div>
  <div id="tableLifeTotals" class="life-grid"></div>
  <p id="lifeMessage" class="msg" role="status" aria-live="polite"></p>
  <p class="hint">Your own changes apply immediately. Other players have 15 seconds to respond before Atlas accepts a request.</p>
  <div id="lifePanel" hidden>
   <div class="my-life"><h3>My life</h3><strong id="myLifeTotal" aria-live="polite">—</strong></div>
   <details class="drawer"><summary>Custom and preset life changes</summary><fieldset id="lifeFields"><legend class="sr-only">Adjust life</legend>
    <label for="lifeTarget">Player whose life changes</label><select id="lifeTarget" onchange="renderCounterControls()"></select>
    <p id="lifeTargetHelp" class="small">Your own changes apply immediately.</p>
    <div class="pad"><button type="button" id="lifeMinusBig" onclick="adjustMyLife(-lifeBigStep)">−5</button><button type="button" id="lifePlusBig" onclick="adjustMyLife(lifeBigStep)">+5</button><button type="button" id="lifeMinus" onclick="adjustMyLife(-lifeStep)">−1</button><button type="button" id="lifePlus" onclick="adjustMyLife(lifeStep)">+1</button></div>
    <div id="lifeTens" class="pad" hidden><button type="button" aria-label="Subtract 10 life" onclick="adjustMyLife(-10)">−10</button><button type="button" aria-label="Add 10 life" onclick="adjustMyLife(10)">+10</button></div>
    <form onsubmit="event.preventDefault();adjustMyLife(Number(lifeDelta.value))"><label for="lifeDelta">Custom life change (negative to subtract)</label><div class="inline"><input id="lifeDelta" type="number" step="1" min="-1000000" max="1000000" required><button id="lifeSubmit" type="submit" class="primary">Apply life change</button></div></form>
   </fieldset></details>
  </div>
  <section id="lifeRequestsPanel" class="requests" hidden><h3>Life change requests</h3><p id="lifeRequestNotice" class="small" role="status" aria-live="polite"></p><div id="lifeRequestActions"></div><p id="lifeRequestCountdown" class="small"></p><div id="outgoingLifeRequests" class="small"></div></section>
 </section>

 <section id="commanderPanel" class="card" hidden>
  <div class="card-head"><div><h2 class="eyebrow">My received Commander damage</h2><p class="small">Record damage from each commander separately. Adding damage also subtracts life. A negative correction restores life. Players decide when to concede.</p></div></div>
  <div id="commanderTotals"></div>
  <form onsubmit="saveCommanderDamage(event)"><fieldset id="commanderFields"><legend class="sr-only">Record or correct damage received</legend>
   <div class="grid" style="gap:0 12px"><div class="field third"><label for="commanderSource">Commander owner</label><select id="commanderSource"></select></div>
   <div class="field third"><label for="commanderSlot">Commander</label><select id="commanderSlot"><option value="1">Commander 1</option><option value="2">Commander 2</option></select></div>
   <div class="field third"><label for="commanderDelta">Damage change (negative to correct)</label><input id="commanderDelta" type="number" step="1" min="-1000000" max="1000000" required></div></div>
   <button type="submit" class="primary" style="margin-top:14px">Update damage and life</button></fieldset></form>
  <p id="commanderMessage" class="msg" role="status" aria-live="polite"></p>
 </section>

 <section class="card setup-card">
  <div class="card-head"><h2 class="eyebrow">Game setup</h2></div>
  <form onsubmit="saveGameSettings(event)"><fieldset id="gameSettingsFields"><legend class="sr-only">Game setup</legend>
   <div class="field"><label for="gameProfileSelect">Game profile</label>
   <select id="gameProfileSelect" onchange="chooseGameProfile()"><option value="generic">Generic</option><option value="mtg">Magic: The Gathering</option><option value="mtg_commander">MTG Commander</option><option value="yugioh">Yu-Gi-Oh!</option></select></div>
   <label for="startingLifeInput">Starting life</label><input id="startingLifeInput" type="number" min="0" max="1000000" step="1" required value="40" oninput="gameSettingsDirty=true">
   <label for="turnTimerSelect">Turn timer</label><select id="turnTimerSelect" onchange="gameSettingsDirty=true;turnTimerCustomRow.hidden=this.value!=='custom'"></select>
   <div id="turnTimerCustomRow" hidden><label for="turnTimerCustom">Custom turn length in seconds (15 to 3600)</label><input id="turnTimerCustom" type="number" min="15" max="3600" step="1" value="90" oninput="gameSettingsDirty=true"></div>
   <p class="hint">Off: no countdown; a gentle cue appears after 5 minutes. With a timer, Atlas warns 10 seconds before time runs out. Running out of time never passes the turn.</p>
   <button type="submit" class="primary" style="width:100%;margin-top:6px">Save game settings</button></fieldset></form>
  <p id="gameSettingsMessage" class="msg" role="status" aria-live="polite">The table host can change settings in the lobby.</p>
  <p class="hint">Life and Commander damage do not automatically eliminate a player. Changes to another player's life need their approval; Atlas accepts unanswered requests after 15 seconds.</p>
 </section>
</div>

<div class="col">
 <section id="sessionBox" class="card seat-card">
  <div class="card-head"><h2 class="eyebrow">My seat</h2></div>
  <div class="who"><span id="sessionAvatar" class="avatar lg" aria-hidden="true">?</span><div class="who-text"><div id="sessionTitle" class="who-name">Not signed in</div><div id="sessionMeta" class="small">Sign into your profile to join or reconnect.</div></div></div>
  <div id="sessionState" class="badges"></div>
  <div id="sessionControls" class="control-grid" hidden></div>
  <div id="claimHelp" class="notice info"><p>Sign in to play from this phone. A physical Sigil is optional.</p><a class="btn primary" href="/login">Sign in or create a profile</a></div>
 </section>

 <section class="card table-card">
  <div class="card-head"><h2 class="eyebrow">Table</h2></div>
  <div class="metrics"><div class="metric"><label>Players</label><strong id="playersMetric">0</strong></div><div class="metric"><label>Sigils</label><strong id="sigilsMetric">0</strong></div><div class="metric"><label>Host</label><strong id="hostMetric">None</strong></div><div class="metric"><label>Starter</label><strong id="starterMetric">None</strong></div></div>
  <div style="margin-top:10px">
   <div class="status-row"><span>State</span><strong id="stateMetric">—</strong></div>
   <div class="status-row"><span>Active</span><strong id="activeMetricGame">None</strong></div>
   <div class="status-row"><span>Starting player</span><strong id="starterGame">None</strong></div>
   <div class="status-row"><span>Win response</span><strong id="confirmGame">None</strong></div>
   <div class="status-row"><span>Elimination target</span><strong id="eliminationGame">None</strong></div>
   <div class="status-row"><span>Winner</span><strong id="winnerGame">None</strong></div>
  </div>
 </section>

</div>
</div>
</section>

<section id="view-players" class="view" hidden>
<h1 class="sr-only">Players</h1>
<div class="grid">
 <section class="card two-third"><div class="card-head"><div><h2 class="eyebrow">Players</h2><p class="small">Live table seats, browser links, turn state, and physical Sigil identity.</p></div><button class="small" onclick="refreshAll()">Refresh</button></div><div id="players" class="roster"></div></section>
 <section class="card third"><div class="card-head"><div><h2 class="eyebrow">Invite players</h2><p class="small">Connect to the table’s Wi-Fi, then scan a code to open TurnHub.</p></div></div><div class="qr-grid"><div class="qr-box"><h3>Table portal</h3><div id="portalQr"></div><a id="portalQrLink">Open portal</a></div><div class="qr-box"><h3>Sign in / join</h3><div id="joinQr"></div><a id="joinQrLink">Open sign in</a></div></div></section>
 <section class="card"><div class="card-head"><div><h2 class="eyebrow">Connect a Sigil</h2><p class="small">Sign into your account, then attach a Sigil. Account login and table participation are separate.</p></div></div><div id="devices" class="devices"></div></section>
 <section class="card" id="gmPanel" hidden><div class="card-head"><h2 class="eyebrow">Game Master</h2></div><div id="gmAccounts"></div></section>
</div>
</section>

<section id="view-account" class="view" hidden>
<h1 class="sr-only">My Account</h1>
<div class="grid">
 <section class="card half"><div class="card-head"><h2 class="eyebrow">My account</h2></div>
  <div id="profileSignedOut" class="notice info"><p>Sign in to save your name, PIN, privacy choices and statistics. New here? Create an account on the same page.</p><a class="btn primary" href="/login">Sign in / create account</a></div>
  <div id="profileBox" hidden>
   <div class="who"><span id="profileAvatar" class="avatar lg" aria-hidden="true">?</span><div class="who-text"><div class="who-name" id="profileSeatTitle">My seat</div><div class="mono" id="profileSeatMeta"></div></div></div>
   <div class="actions" style="margin-top:14px"><a class="btn small" href="/stats">My statistics</a><button class="small bad" onclick="logoutSession()">Log out</button></div>
   <label for="profileName">Display name</label><div class="inline"><input id="profileName" maxlength="32" placeholder="Player name" autocomplete="nickname"><button onclick="saveName()">Save name</button></div>
   <label for="profilePin">New PIN</label><div class="inline"><input id="profilePin" type="password" inputmode="numeric" maxlength="8" placeholder="4–8 digits" autocomplete="new-password"><button onclick="savePin()">Set PIN</button></div>
   <button id="clearPinButton" class="bad small" style="margin-top:10px" hidden onclick="clearPin()">Remove PIN</button>
   <form id="profilePolicyForm" style="margin-top:20px" onsubmit="saveProfilePolicy(event)"><fieldset id="profilePolicyFields"><legend>Physical access and privacy</legend>
    <div class="switch-row"><div><label for="allowPhysicalWithoutPin">Allow physical use without a PIN</label><small id="allowPhysicalWithoutPinHelp">When disabled, sign into this profile in the portal before joining with a Sigil. An existing game continues.</small></div><input id="allowPhysicalWithoutPin" type="checkbox" aria-describedby="allowPhysicalWithoutPinHelp" onchange="profilePolicyDirty=true"></div>
    <div class="switch-row"><div><label for="hideStatsWithoutAuthentication">Hide stats without authentication</label><small id="hideStatsWithoutAuthenticationHelp">Statistics always accumulate. This choice also applies to future Sigil statistics screens. Signing into this profile enables its connected Sigil's full display.</small></div><input id="hideStatsWithoutAuthentication" type="checkbox" aria-describedby="hideStatsWithoutAuthenticationHelp" onchange="profilePolicyDirty=true"></div>
    <button type="submit" class="primary" style="margin-top:10px">Save profile choices</button></fieldset>
    <p id="profilePolicyStatus" class="msg" role="status" aria-live="polite"></p></form>
  </div>
 </section>
 <section class="card half"><div class="card-head"><div><h2 class="eyebrow">Appearance and feedback</h2><p class="small">Saved in this browser only. No sign-in needed.</p></div></div>
  <fieldset><legend class="sr-only">Portal theme</legend><p class="small" style="margin-top:0">Themes change only how this browser looks. They never change the game.</p>
   <div class="themes">
    <label class="theme-opt"><input type="radio" name="theme" value="brass" onchange="setTheme(this.value)"><strong>Brass</strong><span class="swatch" aria-hidden="true"><i style="background:#110d09"></i><i style="background:#30251a"></i><i style="background:#e2ae4a"></i><i style="background:#62d6b2"></i></span><small>Steampunk gold, rivets and gauges.</small></label>
    <label class="theme-opt"><input type="radio" name="theme" value="midnight" onchange="setTheme(this.value)"><strong>Midnight</strong><span class="swatch" aria-hidden="true"><i style="background:#0a0d14"></i><i style="background:#202a40"></i><i style="background:#e8b44f"></i><i style="background:#5ad8ca"></i></span><small>Clean modern dark with gold.</small></label>
    <label class="theme-opt"><input type="radio" name="theme" value="parchment" onchange="setTheme(this.value)"><strong>Parchment</strong><span class="swatch" aria-hidden="true"><i style="background:#ece2cd"></i><i style="background:#fbf6ea"></i><i style="background:#8e5f0c"></i><i style="background:#0d7560"></i></span><small>Light sepia for bright rooms.</small></label>
    <label class="theme-opt"><input type="radio" name="theme" value="contrast" onchange="setTheme(this.value)"><strong>High contrast</strong><span class="swatch" aria-hidden="true"><i style="background:#000"></i><i style="background:#fff"></i><i style="background:#ffd400"></i><i style="background:#00f0d8"></i></span><small>Maximum legibility.</small></label>
   </div></fieldset>
  <h3 style="margin:22px 0 4px">Browser feedback</h3>
  <div class="switch-row"><div><label for="soundToggle">Browser sound</label><small id="soundToggleHelp">Play lightweight feedback tones on this device.</small></div><input id="soundToggle" type="checkbox" aria-describedby="soundToggleHelp" onchange="saveBrowserPrefs()"></div>
  <div class="switch-row"><div><label for="vibrationToggle">Vibration</label><small id="vibrationToggleHelp">Use haptics when supported by the browser.</small></div><input id="vibrationToggle" type="checkbox" aria-describedby="vibrationToggleHelp" onchange="saveBrowserPrefs()"></div>
  <div class="inline" style="margin-top:6px;align-items:end"><div class="field"><label for="volumeSelect" style="margin-top:8px">Feedback volume</label><select id="volumeSelect" onchange="saveBrowserPrefs()"><option value="low">Low</option><option value="medium">Medium</option><option value="high">High</option></select></div><button class="blue" onclick="playFeedback('test',true)">Test feedback</button></div>
 </section>
 <section id="moderationCard" class="card" hidden><div class="card-head"><div><h2 class="eyebrow">Private moderation history</h2><p class="small">Visible only to you and Game Masters.</p></div></div><div id="myModeration" class="small">Sign in to view your counts.</div></section>
</div>
</section>

<section id="view-settings" class="view" hidden>
<h1 class="sr-only">Device Settings</h1>
<div class="grid">
 <section class="card half"><div class="card-head"><h2 class="eyebrow">Atlas Wi-Fi security</h2></div>
  <div class="status-row"><span>Network</span><strong id="wifiSsidSetting">—</strong></div><div class="status-row"><span>Security</span><strong id="wifiSecuritySetting">—</strong></div><div class="status-row"><span>Connected clients</span><strong id="wifiClientsSetting">—</strong></div><div class="status-row"><span>Password</span><strong id="wifiPasswordState">—</strong></div>
  <label for="networkPasswordInput">New Wi-Fi password</label><input id="networkPasswordInput" type="password" minlength="8" maxlength="63" autocomplete="new-password" placeholder="8–63 characters">
  <div class="actions" style="margin-top:10px"><button onclick="generateNetworkPassword()">Generate</button><button id="wifiRevealButton" onclick="toggleNetworkPassword()">Show</button><button class="warn" onclick="saveNetworkPassword()">Save &amp; restart Atlas</button></div>
  <div id="networkRestartNotice" class="notice" style="margin-top:14px">Hold the physical Atlas master button while saving. Changing the password restarts Atlas and disconnects every Wi-Fi client until it reconnects with the new password.</div>
 </section>
 <section class="card half"><div class="card-head"><h2 class="eyebrow">Atlas</h2></div>
  <div class="status-row"><span>State</span><strong id="stateMetricSystem">—</strong></div><div class="status-row"><span>Firmware</span><strong id="firmwareMetric">—</strong></div><div class="status-row"><span>Build</span><strong id="buildMetric">—</strong></div><div class="status-row"><span>ESP-NOW</span><strong id="espMetric">—</strong></div><div class="status-row"><span>Master button</span><strong id="masterMetric">Released</strong></div><div class="status-row"><span>Atlas OTA</span><strong id="otaMetric">Locked</strong></div><div class="status-row"><span>Active player</span><strong id="activeMetric">None</strong></div><div class="status-row"><span>Winner</span><strong id="winnerMetric">None</strong></div>
  <div class="actions" style="margin-top:12px"><a class="btn" href="/update">Atlas firmware</a><span class="small">Update from Lobby or Game Over.</span></div>
 </section>
 <section class="card half"><div class="card-head"><h2 class="eyebrow">Portal &amp; network</h2></div>
  <div class="status-row"><span>Address</span><strong id="portalAddress" class="mono"></strong></div><div class="status-row"><span>Wi-Fi</span><strong id="wifiSsidSystem">—</strong></div><div class="status-row"><span>Security</span><strong id="wifiSecuritySystem">—</strong></div><div class="status-row"><span>Wi-Fi clients</span><strong id="wifiClientsSystem">—</strong></div><div class="status-row"><span>Browser seat</span><strong id="browserSeatMetric">Not signed in</strong></div><div class="status-row"><span>Connection</span><strong id="connectionMetric">Connecting</strong></div>
  <div class="actions" style="margin-top:12px"><button onclick="refreshAll()">Refresh now</button></div>
 </section>
 <section class="card half"><div class="card-head"><div><h2 class="eyebrow">Device names</h2><p class="small">Hold the Atlas master button while renaming a device.</p></div></div><div id="atlasDevice"></div><div id="deviceSettingsList" class="stack" style="margin-top:10px;gap:10px"></div></section>
 <section class="card"><div class="card-head"><div><h2 class="eyebrow">Account permissions</h2><p class="small">Permissions can be combined. The initial Admin must remain an Admin. Privileged accounts require a PIN.</p></div><button class="small" onclick="refreshAccountList()">Refresh accounts</button></div><div id="adminAccounts"></div></section>
</div>
</section>
</main>

<div id="toast" class="toast" role="status" aria-live="polite"></div>
<dialog id="uiDialog" aria-labelledby="uiDialogTitle"><form method="dialog"><h2 id="uiDialogTitle"></h2><p id="uiDialogText"></p><div id="uiDialogInputRow" hidden><label for="uiDialogInput" id="uiDialogLabel"></label><input id="uiDialogInput"></div><div class="actions"><button id="uiDialogOk" value="ok" class="primary">OK</button><button value="cancel" formnovalidate>Cancel</button></div></form></dialog>
<p class="foot-note">TurnHub runs locally on Atlas. No cloud connection is required for table control.</p>
</div>
<script src="/portal-qr.js"></script>
<script>
let refreshInFlight=null,gameSettingsData=null,gameSettingsDirty=false,gameSettingsSaving=false,lifeBusy=false,lifeStep=1,lifeBigStep=5;
let profilePolicyDirty=false,profilePolicyOwner=null;
let counterData=null,counterBusy=false,counterOwner=null;
let statusData=null,deviceData={devices:[]},seatData={seats:[]},networkData=null,sessionInfo=null,pendingClaim=null,pendingTimer=null;
let sessionToken=localStorage.getItem('turnhubSessionToken')||'';
let lastObservedState=null,lastObservedActive=0,lastObservedConfirm=0,toastTimer=null,lastGauge=0;
const lifeSeen={};
let browserPrefs={sound:true,vibration:true,volume:'medium'};
try{browserPrefs=Object.assign(browserPrefs,JSON.parse(localStorage.getItem('turnhubBrowserPrefs')||'{}'))}catch(_){}
const TABS=['game','players','account','settings'];

function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function badge(t,c=''){return `<span class="badge ${c}">${esc(t)}</span>`}
function authHeaders(){return sessionToken?{'X-TurnHub-Token':sessionToken}:{}}
function seatByPlayer(n){return seatData.seats.find(s=>Number(s.player)===Number(n))}
function deviceById(id){return (deviceData&&deviceData.devices||[]).find(d=>Number(d.id)===Number(id))||null}
function sigilLabel(id){if(Number(id)>=8&&Number(id)<24)return 'Phone controller';const d=deviceById(id);return d&&d.label?d.label:`Sigil ${Number(id)+1}`}
function playerLabel(n){if(!Number(n))return 'None';const s=seatByPlayer(n);return s&&s.name?s.name:`Player ${n}`}
function sameSessionSeat(s){return !!(sessionInfo&&sessionInfo.authenticated&&Number(sessionInfo.module)===Number(s.module)&&Number(sessionInfo.slot)===Number(s.slot))}
function initials(name){const w=String(name||'?').trim().split(/\s+/).filter(Boolean);return esc(((w[0]||'?')[0]+(w.length>1?w[w.length-1][0]:'')).toUpperCase())}
// Decorative only: the name always sits beside the avatar.
function hue(n){return [38,168,12,205,95,280,330,60][(Number(n)||0)%8]}
function avatar(name,n,cls=''){return `<span class="avatar ${cls}" style="--hue:${hue(n)}" aria-hidden="true">${initials(name)}</span>`}
function showToast(msg,bad=false){const e=document.getElementById('toast');e.textContent=msg;e.className='toast show'+(bad?' bad':'');if(toastTimer)clearTimeout(toastTimer);toastTimer=setTimeout(()=>e.className='toast',3500)}
function setTheme(name){if(!['brass','midnight','parchment','contrast'].includes(name))name='brass';document.documentElement.dataset.theme=name;try{localStorage.setItem('turnhubTheme',name)}catch(_){}const m=document.querySelector('meta[name=theme-color]');if(m)m.content=getComputedStyle(document.documentElement).getPropertyValue('--meta').trim()||'#110d09';document.querySelectorAll('input[name=theme]').forEach(r=>r.checked=r.value===name)}

// Modal confirm/prompt that matches the portal; falls back to the browser's own.
function uiAsk({title,text,ok='Confirm',danger=false,input=null}){
 if(typeof uiDialog.showModal!=='function')return Promise.resolve(input?prompt(text,input.value||''):confirm(text));
 return new Promise(resolve=>{
  uiDialogTitle.textContent=title;uiDialogText.textContent=text;uiDialogOk.textContent=ok;uiDialogOk.className=danger?'bad':'primary';
  uiDialogInputRow.hidden=!input;
  if(input){uiDialogLabel.textContent=input.label;uiDialogInput.type=input.type||'text';uiDialogInput.value=input.value||'';uiDialogInput.maxLength=input.maxLength||64;uiDialogInput.inputMode=input.inputMode||'text';uiDialogInput.autocomplete=input.autocomplete||'off'}
  uiDialog.returnValue='';uiDialog.showModal();if(input)uiDialogInput.focus();
  uiDialog.addEventListener('close',()=>{const accepted=uiDialog.returnValue==='ok';const value=uiDialogInput.value;uiDialogInput.value='';resolve(input?(accepted?value:null):accepted)},{once:true});
 });
}

function toggleAccountMenu(force){const open=typeof force==='boolean'?force:accountMenu.hidden;accountMenu.hidden=!open;accountButton.setAttribute('aria-expanded',String(open));if(open)accountMenu.querySelector('.menu-item')?.focus()}
function closeAccountMenu(){if(!accountMenu.hidden){accountMenu.hidden=true;accountButton.setAttribute('aria-expanded','false')}}
document.addEventListener('click',e=>{if(!accountMenuWrap.contains(e.target))closeAccountMenu()});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&!accountMenu.hidden){closeAccountMenu();accountButton.focus()}});

function showTab(name){
 if(!TABS.includes(name))name='game';
 if(name==='settings'&&!(sessionInfo&&sessionInfo.permissions&1))name='game';
 document.querySelectorAll('.view').forEach(v=>{const on=v.id==='view-'+name;v.classList.toggle('active',on);v.hidden=!on});
 document.querySelectorAll('.tab').forEach(b=>{const on=b.dataset.tab===name;b.classList.toggle('active',on);if(on)b.setAttribute('aria-current','page');else b.removeAttribute('aria-current')});
 try{localStorage.setItem('turnhubPortalTab',name)}catch(_){}
 if(name!=='game')refreshAccountList();
}

function loadBrowserPrefs(){soundToggle.checked=!!browserPrefs.sound;vibrationToggle.checked=!!browserPrefs.vibration;volumeSelect.value=browserPrefs.volume||'medium'}
function saveBrowserPrefs(){browserPrefs={sound:soundToggle.checked,vibration:vibrationToggle.checked,volume:volumeSelect.value};localStorage.setItem('turnhubBrowserPrefs',JSON.stringify(browserPrefs));showToast('Browser feedback settings saved.')}
function playFeedback(kind,force=false){if((browserPrefs.vibration||force)&&navigator.vibrate){const p=kind==='turn'?[70,35,70]:kind==='alert'?[100,50,100]:kind==='gameover'?[130,60,130]:[55];try{navigator.vibrate(p)}catch(_){}}
 if(!browserPrefs.sound&&!force)return;try{const Ctx=window.AudioContext||window.webkitAudioContext;if(!Ctx)return;const ctx=window.turnhubAudioContext||(window.turnhubAudioContext=new Ctx());if(ctx.state==='suspended')ctx.resume();const gain=ctx.createGain();gain.gain.value=({low:.025,medium:.055,high:.1})[browserPrefs.volume]||.055;gain.connect(ctx.destination);let tones=[620];if(kind==='turn')tones=[700,900];else if(kind==='pause')tones=[440,330];else if(kind==='resume')tones=[440,660];else if(kind==='alert')tones=[760,760];else if(kind==='gameover')tones=[620,780,980];else if(kind==='test')tones=[620,820];tones.forEach((hz,i)=>{const o=ctx.createOscillator();o.type='sine';o.frequency.value=hz;o.connect(gain);const t=ctx.currentTime+i*.105;o.start(t);o.stop(t+.075)})}catch(_){}
}
function processBrowserFeedback(s){const me=sessionInfo&&sessionInfo.authenticated?Number(sessionInfo.player):0;if(lastObservedState!==null&&me){if(Number(s.active)===me&&Number(lastObservedActive)!==me)playFeedback('turn');if(Number(s.winConfirm)===me&&Number(lastObservedConfirm)!==me)playFeedback('alert');if(s.state!==lastObservedState){if(s.state==='PAUSED')playFeedback('pause');else if(s.state==='RUNNING'&&lastObservedState==='PAUSED')playFeedback('resume');else if(s.state==='GAME_OVER')playFeedback('gameover')}}lastObservedState=s.state;lastObservedActive=Number(s.active)||0;lastObservedConfirm=Number(s.winConfirm)||0}

function buildGaugeTicks(){let out='';for(let i=0;i<=30;i++){const a=(-135+270*i/30)*Math.PI/180,major=i%5===0,r1=major?71:75,r2=81;out+=`<line class="${major?'major':''}" x1="${(100+r1*Math.sin(a)).toFixed(1)}" y1="${(100-r1*Math.cos(a)).toFixed(1)}" x2="${(100+r2*Math.sin(a)).toFixed(1)}" y2="${(100-r2*Math.cos(a)).toFixed(1)}"/>`}gaugeTicks.innerHTML=out}
// Display only: Atlas owns the timer; the gauge redraws what /api/status reports.
function renderGauge(s){
 const limit=Number(s.turnTimerMs)||0,elapsed=Number(s.turnElapsedMs)||0,live=['RUNNING','PAUSED'].includes(s.state)&&Number(s.active);
 let p=0,value='—',caption='',phase='idle';
 if(live){
  if(s.timerPhase==='EXPIRED'){value='+'+clockText(elapsed-limit);caption="Time's up";phase='expired'}
  else if(limit){p=Math.max(0,Math.min(1,(Number(s.turnRemainingMs)||0)/limit));value=clockText(s.turnRemainingMs);caption=s.timerPhase==='WARNING'?'Almost up':'Remaining';phase=s.timerPhase==='WARNING'?'warning':'counting'}
  else{p=(elapsed%60000)/60000;value=clockText(elapsed);caption=s.timerPhase==='LONG_TURN'?'Long turn':'Turn time';phase=s.timerPhase==='LONG_TURN'?'warning':'counting'}
  if(s.state==='PAUSED')caption='Paused';
 }else if(s.state==='LOBBY'){value=String(Number(s.players)||0);caption=Number(s.players)===1?'Player':'Players'}
 else if(s.state==='STARTING'){p=1;value='3·2·1';caption='Starting'}
 else if(s.state==='GAME_OVER'){p=1;value='Done';caption='Game over'}
 const snap=Math.abs(p-lastGauge)>.5;lastGauge=p;
 gaugeArc.classList.toggle('snap',snap);gaugeNeedle.classList.toggle('snap',snap);
 gaugeArc.setAttribute('stroke-dasharray',`${(p*100).toFixed(2)} 100`);
 gaugeNeedle.style.transform=`rotate(${(-135+270*p).toFixed(1)}deg)`;
 gaugeValue.textContent=value;gaugeCaption.textContent=caption;stage.dataset.phase=phase;stage.dataset.state=s.state||'';
}

function renderStatus(s){statusData=s;dot.className='dot online';connPill.className='conn ok';connection.textContent='Connected';connectionMetric.textContent='Connected';playersMetric.textContent=s.players;sigilsMetric.textContent=s.sigils;hostMetric.textContent=Number(s.host)>=0?sigilLabel(s.host):'None';starterMetric.textContent=playerLabel(s.starter);stateMetric.textContent=s.state;stateMetricSystem.textContent=s.state;firmwareMetric.textContent='v'+s.firmware;buildMetric.textContent=s.build||'—';espMetric.textContent=s.espNow?'Ready':'Error';activeMetric.textContent=playerLabel(s.active);activeMetricGame.textContent=playerLabel(s.active);winnerMetric.textContent=playerLabel(s.winner);masterMetric.textContent=s.masterButton?'Pressed':'Released';otaMetric.textContent=s.otaStateAllowed?'State ready':'Game locked';starterGame.textContent=playerLabel(s.starter);confirmGame.textContent=playerLabel(s.winConfirm);eliminationGame.textContent=playerLabel(s.eliminationTarget);winnerGame.textContent=playerLabel(s.winner);
 let title='Ready',sub='Sign in and join from your phone, or press Action on a Sigil',eye='Lobby',bs='';
 if(s.state==='LOBBY'){title=s.players?`${s.players} player${Number(s.players)===1?'':'s'} seated`:'Ready';sub=s.starter?`${playerLabel(s.starter)} goes first`:'Sign in and join from your phone, or press Action on a Sigil';if(Number(s.host)>=0)bs+=badge(`${sigilLabel(s.host)} host`,'good');if(s.starter)bs+=badge(`${playerLabel(s.starter)} starter`,'blue')}
 else if(s.state==='STARTING'){eye='Countdown';title='3 · 2 · 1';sub=`${playerLabel(s.starter)} starts`;bs+=badge('Countdown','blue')}
 else if(s.state==='RUNNING'){eye='Active turn';title=s.active?playerLabel(s.active):'Running';sub=turnTimerText(s);bs+=badge('Running','active');if(s.timerPhase==='WARNING')bs+=badge('Time almost up','warn');else if(s.timerPhase==='EXPIRED')bs+=badge("Time's up",'bad');else if(s.timerPhase==='LONG_TURN')bs+=badge('Long turn','warn');if(Number(s.passPending))bs+=badge(`${playerLabel(s.passPending)} passing`,'blue')}
 else if(s.state==='PAUSED'){eye='Paused';title=s.winConfirm?'Respond':'Paused';sub=s.winConfirm?`${playerLabel(s.winConfirm)} must confirm or deny`:s.eliminationTarget?`${playerLabel(s.eliminationTarget)} selected`:'Game paused';bs+=badge('Paused','warn')}
 else if(s.state==='GAME_OVER'){eye='Game over';title=s.winner?playerLabel(s.winner):'Complete';sub=s.winner?'Winner confirmed':'Game complete';bs+=badge('Game over','good')}
 else eye=s.state||'TurnHub';
 eyebrow.textContent=eye;heroTitle.textContent=title;heroSub.textContent=sub;if(heroBadges.innerHTML!==bs)heroBadges.innerHTML=bs;
 if(s.state==='RUNNING')document.documentElement.dataset.running='';else delete document.documentElement.dataset.running;
 renderGauge(s);renderPlayers();renderSession();processBrowserFeedback(s)}

function playerStateText(s){if(s.eliminated)return 'Eliminated';if(statusData&&Number(statusData.winner)===Number(s.player))return 'Winner';if(s.active)return 'Active turn';if(statusData&&Number(statusData.winConfirm)===Number(s.player))return 'Response needed';return 'At table'}
function renderPlayers(){const cards=seatData.seats.map(s=>{let cls='player';if(s.active)cls+=' active';if(statusData&&Number(statusData.winner)===Number(s.player))cls+=' winner';if(s.eliminated)cls+=' eliminated';const current=sameSessionSeat(s),name=s.name||`Player ${s.player}`;let controls='';if(s.virtual&&!current)controls=`<a class="btn small" href="/login?profile=${encodeURIComponent(s.profileId)}">Sign into this profile</a>`;else if(current)controls='<button class="small" disabled>Current browser seat</button>';else{controls+=`<button class="small" onclick="claimSeat(${s.module},${s.slot})">Use this seat</button>`;if(s.hasPin&&!s.virtual)controls+=`<button class="small blue" onclick="pinLogin(${s.module},${s.slot})">PIN login</button>`}let flags='';if(s.active)flags+=badge('Active','active');if(s.sessionClaimed)flags+=badge('Browser linked','good');if(s.hasPin)flags+=badge('PIN');if(s.eliminated)flags+=badge('Out','bad');return `<article class="${cls}"><div class="player-head">${avatar(name,s.player)}<div><div class="player-name">${esc(name)}</div><div class="small">Player ${s.player} · ${esc(sigilLabel(s.module))} · Seat ${esc(s.slotName)}</div></div><span class="player-state ${s.active?'on':''}">${esc(playerStateText(s))}</span></div>${flags?`<div class="badges">${flags}</div>`:''}<div class="actions">${controls}</div></article>`});const html=cards.length?cards.join(''):'<div class="empty">No players have joined yet.</div>';if(players.innerHTML!==html)players.innerHTML=html}
function seatsForModule(id){return seatData.seats.filter(s=>Number(s.module)===Number(id))}
function capabilityHtml(x){const caps=[];if(Number(x.capabilities)&1)caps.push(badge('Display','good'));if(!x.metadata)caps.push(badge('Legacy metadata','warn'));if(x.customName)caps.push(badge('Custom name','blue'));return caps.join('')}
function renderDevices(d){
 deviceData=d;atlasDevice.className='notice good';
 atlasDevice.innerHTML=`<strong>Atlas</strong> · <span class="mono">${esc(d.atlas.hardwareId)}</span> · firmware ${esc(d.atlas.firmware)}`;
 const html=d.devices.length?d.devices.map(x=>{
  const state=x.online?'<span class="device-state online-text">ONLINE</span>':'<span class="device-state offline-text">OFFLINE</span>';
  const fw=x.metadata?`Firmware ${esc(x.firmware)}`:'Firmware metadata unavailable';
  const seats=seatsForModule(x.id);
  const seatHtml=seats.length?seats.map(s=>{
   const current=sameSessionSeat(s);let controls=current?'<button class="small" disabled>Current seat</button>':`<button class="small" ${x.online?'':'disabled'} onclick="claimSeat(${s.module},${s.slot})">Use ${esc(s.slotName)}</button>`;
   if(s.hasPin&&!current)controls+=`<button class="small blue" onclick="pinLogin(${s.module},${s.slot})">PIN login</button>`;
   return `<div class="seat ${s.active?'active':''}"><div class="seat-top"><div><span class="slot">${esc(s.slotName)}</span><strong>${esc(s.name||`Player ${s.player}`)}</strong></div><span class="small">P${s.player}</span></div><div class="actions">${controls}</div></div>`
  }).join(''):'<div class="small">No seats have joined yet.</div>';
  const attach=sessionInfo&&sessionInfo.authenticated&&statusData&&statusData.state==='LOBBY'?`<button class="small primary" ${x.online?'':'disabled'} onclick="claimSeat(${x.id},1)">Attach seat A to my profile</button>`:'';
  const signIn=!sessionInfo||!sessionInfo.authenticated?'<a class="btn small" href="/login">Sign in to attach a Sigil</a>':'';
  const defaultLine=x.customName?`<div class="small">${esc(x.defaultLabel)}</div>`:'';
  return `<article class="device"><div class="device-top"><div><div class="device-name">${esc(x.label)}</div>${defaultLine}<div class="mono">${esc(x.hardwareId)}</div></div>${state}</div><div class="small" style="margin-top:8px">${fw}<br>Last seen ${Math.round(Number(x.ageMs)/100)/10}s ago · ${Number(x.sessionCount)} browser session${Number(x.sessionCount)===1?'':'s'}</div><div class="badges">${capabilityHtml(x)}</div><div class="actions">${attach}${signIn}</div><div class="small" style="margin-top:8px">Both seats clear on restart and at game end. Rematch restores the same players.</div><div class="seats">${seatHtml}</div></article>`
 }).join(''):'<div class="empty">No Sigils discovered yet.</div>';
 if(devices.innerHTML!==html)devices.innerHTML=html;
}

function renderNetwork(n){networkData=n;wifiSsidSetting.textContent=n.ssid||'—';wifiSecuritySetting.textContent=n.security||'—';wifiClientsSetting.textContent=String(n.stations??'—');wifiPasswordState.textContent=n.passwordIsDefault?'Factory default (change it)':n.passwordConfigured?`${n.passwordLength} characters`:'Not configured';wifiSsidSystem.textContent=n.ssid||'—';wifiSecuritySystem.textContent=n.security||'—';wifiClientsSystem.textContent=String(n.stations??'—')}

function renderSession(){
 const authed=!!(sessionInfo&&sessionInfo.authenticated);
 if(!authed){gameSettingsDirty=false;lifePanel.hidden=true;profilePolicyOwner=null;profilePolicyDirty=false;sessionTitle.textContent='Not signed in';sessionMeta.textContent='Sign into your profile to join or reconnect.';sessionAvatar.textContent='?';sessionAvatar.style.removeProperty('--hue');sessionState.innerHTML='';sessionControls.hidden=true;claimHelp.hidden=false;signInButton.hidden=false;accountMenuWrap.hidden=true;closeAccountMenu();moderationCard.hidden=true;profileBox.hidden=true;profileSignedOut.hidden=false;browserSeatMetric.textContent='Not signed in';return}
 const policyOwnerChanged=profilePolicyOwner!==sessionInfo.profileId;
 if(policyOwnerChanged){profilePolicyDirty=false;profilePolicyOwner=sessionInfo.profileId}
 profilePolicyFields.disabled=!sessionInfo.policyAvailable;
 if(!profilePolicyDirty&&sessionInfo.policyAvailable){
   allowPhysicalWithoutPin.checked=!!sessionInfo.allowPhysicalWithoutPin;
   hideStatsWithoutAuthentication.checked=!!sessionInfo.hideStatsWithoutAuthentication;
 }
 if(!sessionInfo.policyAvailable)profilePolicyStatus.textContent='Profile settings are unavailable. Reload before trying again.';
 else if(policyOwnerChanged)profilePolicyStatus.textContent='';
 const me=sessionInfo, joined=!!me.participating, name=me.name||'Unnamed profile', state=statusData||{};
 sessionTitle.textContent=name;sessionMeta.textContent=joined?`Player ${me.player} · ${me.virtual?'Phone play':sigilLabel(me.module)+' + phone'}`:'Signed in · not at the table';
 accountName.textContent=name;menuName.textContent=name;menuMeta.textContent=joined?`Player ${me.player} · at the table`:'Signed in · not at the table';accountButton.setAttribute('aria-label',`Account menu, ${name}`);
 for(const a of [sessionAvatar,profileAvatar,accountAvatar]){a.innerHTML=initials(name);a.style.setProperty('--hue',hue(joined?me.player:0))}
 browserSeatMetric.textContent=joined?`${name} · Player ${me.player}`:name+' · signed in';claimHelp.hidden=true;signInButton.hidden=true;accountMenuWrap.hidden=false;moderationCard.hidden=false;profileBox.hidden=false;profileSignedOut.hidden=true;
 profileSeatTitle.textContent=name;profileSeatMeta.textContent=`Profile ${me.profileId}`;if(document.activeElement!==profileName)profileName.value=me.name||'';
 clearPinButton.hidden=!(me.hasPin&&joined&&!me.virtual);
 const stateHtml=(me.host?badge('Table host','good'):'')+(me.active?badge('Your turn','active'):'')+(me.eliminated?badge('Eliminated','bad'):'');
 if(sessionState.innerHTML!==stateHtml)sessionState.innerHTML=stateHtml;
 const buttons=[];
 if(!joined){buttons.push(`<button class="primary wide xl" ${state.state==='LOBBY'?'':'disabled'} onclick="participation('join')">Join table</button>`)}
 else if(state.state==='LOBBY'){
  buttons.push(`<button onclick="sendControl('starter')">I go first</button>`);
  if(me.host)buttons.push(`<button class="primary" ${Number(state.players)>=2?'':'disabled'} onclick="sendControl('start')">Start game</button>`);
  buttons.push(`<button onclick="participation('leave')">Leave table</button>`);
  if(me.host)buttons.push(`<button class="bad" onclick="resetTable()">Reset table</button>`);
 }else if(state.state==='STARTING'){buttons.push(`<button class="wide" onclick="sendControl('cancel-start')">Cancel countdown</button>`)}
 else if(state.state==='RUNNING'&&!me.eliminated){
  buttons.push(`<button class="primary wide xl" ${me.active?'':'disabled'} onclick="sendControl('pass')">${Number(state.passPending)===Number(me.player)?'Cancel pending pass':'Pass turn'}</button>`);
  buttons.push(`<button onclick="sendControl('pause')">Pause</button>`);
  buttons.push(`<button ${me.active?'':'disabled'} onclick="sendControl('win')">Claim win</button>`);
  buttons.push(`<button class="bad wide" onclick="concede()">Concede</button>`);
 }else if(state.state==='PAUSED'&&!me.eliminated){
  if(Number(state.winConfirm)===Number(me.player)){buttons.push(`<button class="primary" onclick="sendControl('confirm')">Confirm win</button><button onclick="sendControl('deny')">Deny claim</button>`)}
  else if(!state.winConfirm&&!state.eliminationTarget){buttons.push(`<button class="primary wide xl" onclick="sendControl('pause')">Resume game</button>`);if(me.active)buttons.push(`<button onclick="sendControl('win')">Claim win</button>`);buttons.push(`<button class="bad" onclick="concede()">Concede</button>`)}
 }else if(state.state==='GAME_OVER'&&me.host){buttons.push(`<button class="primary" onclick="sendControl('rematch')">Rematch</button><button onclick="resetTable()">Reset table</button>`)}
 const html=buttons.join('');if(sessionControls.innerHTML!==html){const action=document.activeElement&&document.activeElement.getAttribute('onclick');sessionControls.innerHTML=html;if(action){const replacement=[...sessionControls.querySelectorAll('button')].find(b=>b.getAttribute('onclick')===action);if(replacement)replacement.focus()}}
 sessionControls.hidden=false;
}
async function resetTable(){if(await uiAsk({title:'Reset the table?',text:'Everyone returns to the lobby. Profiles and saved statistics are kept.',ok:'Reset table',danger:true}))sendControl('reset')}
async function participation(action){try{const r=await fetch('/api/session/'+action,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Request failed');showToast(d.message||'Updated');await refreshAll()}catch(e){showToast(e.message,true)}}

async function refreshSession(){if(!sessionToken){sessionInfo=null;renderSession();return}const r=await fetch('/api/session/me',{headers:authHeaders(),cache:'no-store'});if(r.status===401){sessionToken='';sessionInfo=null;localStorage.removeItem('turnhubSessionToken')}else if(!r.ok){throw new Error('Session unavailable')}else{sessionInfo=await r.json()}renderSession()}

async function claimSeat(module,slot){const label=sigilLabel(module);showToast(`Waiting for ${label} ${slot===1?'A':'B'} authorization...`);try{const r=await fetch(`/api/session/request?module=${module}&slot=${slot}`,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Claim failed');pendingClaim=d.requestId;if(pendingTimer)clearInterval(pendingTimer);pendingTimer=setInterval(pollClaim,450);showToast(`Press Action on ${label} within 30 seconds for seat ${slot===1?'A':'B'}.`)}catch(e){showToast(e.message,true)}}
async function pollClaim(){if(!pendingClaim)return;try{const r=await fetch(`/api/session/poll?id=${encodeURIComponent(pendingClaim)}`,{cache:'no-store'});const d=await r.json();if(!r.ok){clearInterval(pendingTimer);pendingClaim=null;showToast(d.error||'Authorization expired.',true);return}if(d.status==='approved'){clearInterval(pendingTimer);pendingClaim=null;sessionToken=d.token;localStorage.setItem('turnhubSessionToken',sessionToken);playFeedback('test');showToast('Seat authorized.');await refreshAll()}}catch(_){}}
async function pinLogin(module,slot){const pin=await uiAsk({title:'PIN login',text:`Enter the PIN for ${sigilLabel(module)} seat ${slot===1?'A':'B'}.`,ok:'Sign in',input:{label:'PIN',type:'password',inputMode:'numeric',maxLength:8,autocomplete:'current-password'}});if(pin===null)return;try{const r=await fetch('/api/session/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({module,slot,pin})});const d=await r.json();if(!r.ok)throw new Error(d.error||'Login failed');sessionToken=d.token;localStorage.setItem('turnhubSessionToken',sessionToken);playFeedback('test');showToast('PIN accepted.');await refreshAll()}catch(e){showToast(e.message,true)}}
async function logoutSession(){try{await fetch('/api/session/logout',{method:'POST',headers:authHeaders()})}catch(_){}sessionToken='';sessionInfo=null;localStorage.removeItem('turnhubSessionToken');showToast('Logged out.');await refreshAll()}
async function sendControl(name){try{const r=await fetch('/api/control/'+name,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Control rejected');playFeedback('test');showToast(d.message||'Control accepted');setTimeout(refreshAll,100)}catch(e){showToast(e.message,true)}}
async function concede(){if(await uiAsk({title:'Concede?',text:'Concede this player from the current game?',ok:'Concede',danger:true}))sendControl('concede')}
async function saveProfilePolicy(event){
 event.preventDefault();
 const choices={allowPhysicalWithoutPin:allowPhysicalWithoutPin.checked?'1':'0',hideStatsWithoutAuthentication:hideStatsWithoutAuthentication.checked?'1':'0'};
 profilePolicyFields.disabled=true;
 try{
  const r=await fetch('/api/session/policy',{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(choices)});
  const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save profile choices');
  profilePolicyDirty=false;profilePolicyStatus.textContent='Profile choices saved on Atlas.';await refreshAll();
 }catch(e){profilePolicyStatus.textContent=e.message;showToast(e.message,true)}
 finally{profilePolicyFields.disabled=!(sessionInfo&&sessionInfo.policyAvailable)}
}
async function saveName(){const name=profileName.value.trim();try{const r=await fetch('/api/session/profile?name='+encodeURIComponent(name),{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save name');showToast('Player name saved.');await refreshAll()}catch(e){showToast(e.message,true)}}
async function savePin(){const pin=profilePin.value.trim();if(!/^\d{4,8}$/.test(pin)){showToast('PIN must be 4 to 8 digits.',true);return}try{const r=await fetch('/api/session/profile',{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({pin})});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not set PIN');profilePin.value='';showToast('PIN saved.');await refreshAll()}catch(e){showToast(e.message,true)}}
async function clearPin(){if(!await uiAsk({title:'Remove PIN?',text:'Remove the saved PIN for this profile?',ok:'Remove PIN',danger:true}))return;try{const r=await fetch('/api/session/profile?clearPin=1',{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not remove PIN');showToast('PIN removed.');await refreshAll()}catch(e){showToast(e.message,true)}}

async function renameSigil(id){const d=deviceById(id);if(!d)return;const next=await uiAsk({title:'Rename device',text:'Hold the Atlas master button while saving.',ok:'Save name',input:{label:`Custom name for ${d.defaultLabel||('Sigil '+(Number(id)+1))}`,value:d.customName||'',maxLength:32}});if(next===null)return;await saveSigilName(id,next.trim())}
async function clearSigilName(id){const d=deviceById(id);if(!d)return;if(!await uiAsk({title:'Clear custom name?',text:`Clear the custom name “${d.label}”?`,ok:'Clear name',danger:true}))return;await saveSigilName(id,'')}
async function saveSigilName(id,name){try{const r=await fetch(`/api/device/name?module=${Number(id)}&name=${encodeURIComponent(name)}`,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save Sigil name');showToast(name?`Sigil renamed to ${d.label}.`:'Custom Sigil name cleared.');await refreshAll()}catch(e){showToast(e.message,true)}}
async function setSeatPersistence(id,remember){try{const r=await fetch(`/api/device/persistence?module=${Number(id)}&slot=1&remember=${remember?'1':'0'}`,{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save persistence choice');showToast(remember?'Seat A profile will persist across reconnects.':'Seat A profile will clear on reconnect.');await refreshAll()}catch(e){showToast(e.message,true);await refreshAll()}}

function generateNetworkPassword(){const chars='ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789';let out='';if(window.crypto&&crypto.getRandomValues){const a=new Uint32Array(16);crypto.getRandomValues(a);for(const n of a)out+=chars[n%chars.length]}else{for(let i=0;i<16;i++)out+=chars[Math.floor(Math.random()*chars.length)]}networkPasswordInput.value=out;networkPasswordInput.type='text';wifiRevealButton.textContent='Hide'}
function toggleNetworkPassword(){const show=networkPasswordInput.type==='password';networkPasswordInput.type=show?'text':'password';wifiRevealButton.textContent=show?'Hide':'Show'}
async function saveNetworkPassword(){const password=networkPasswordInput.value;if(password.length<8||password.length>63){showToast('Wi-Fi password must be 8 to 63 characters.',true);return}if(!await uiAsk({title:'Change the Wi-Fi password?',text:'Atlas will restart and every connected device will be disconnected. Keep holding the Atlas master button.',ok:'Save & restart',danger:true}))return;networkRestartNotice.className='notice';networkRestartNotice.textContent='Saving network password. Keep holding the Atlas master button...';try{const r=await fetch('/api/network/password?password='+encodeURIComponent(password),{method:'POST',headers:authHeaders()});const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not update Wi-Fi password');if(!d.changed){networkRestartNotice.className='notice good';networkRestartNotice.textContent=d.message||'Password already matches.';showToast(d.message||'No network change needed.');return}networkRestartNotice.className='notice good';networkRestartNotice.textContent=`Password saved. Atlas is restarting. Reconnect to ${networkData&&networkData.ssid?networkData.ssid:'TurnHub-Atlas'} using the new password, then reopen this page.`;showToast('Wi-Fi password saved. Atlas is restarting.')}catch(e){networkRestartNotice.className='notice';networkRestartNotice.textContent=e.message||'Network password update failed.';showToast(e.message,true)}}

async function pollJson(path,headers={}){
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),5000);
 try{const r=await fetch(path,{cache:'no-store',headers,signal:controller.signal});if(!r.ok)throw new Error('Refresh failed');return await r.json()}finally{clearTimeout(timer)}
}
function refreshAll(){
 if(refreshInFlight)return refreshInFlight;
 refreshInFlight=(async()=>{
  try{
   const s=await pollJson('/api/status');
   const d=await pollJson('/api/devices');
   const seats=await pollJson('/api/seats');
   await refreshSession();
   const n=(sessionInfo&&sessionInfo.permissions&1)?await pollJson('/api/network',authHeaders()):null;
   gameSettingsData=await pollJson('/api/game/settings',authHeaders());
   counterData=sessionInfo&&sessionInfo.participating&&sessionInfo.lifeAvailable?await pollJson('/api/game/counters',authHeaders()):null;
   deviceData=d;seatData=seats;networkData=n;
   renderDevices(d);renderDeviceSettings(d);if(n)renderNetwork(n);renderStatus(s);renderGameLife();renderCounterControls();renderAccess();
  }catch(_){dot.className='dot';connPill.className='conn';connection.textContent='Reconnecting';connectionMetric.textContent='Disconnected';counterData=null;lifeFields.disabled=true;renderCounterControls()}
 })().finally(()=>{refreshInFlight=null});
 return refreshInFlight;
}
const PROFILE_LABELS={generic:'Generic',mtg:'Magic: The Gathering',mtg_commander:'MTG Commander',yugioh:'Yu-Gi-Oh!'};
function renderGameLife(){
 const settings=gameSettingsData,me=sessionInfo||{},state=statusData||{};
 gameProfileSummary.textContent=settings?`${PROFILE_LABELS[settings.gameProfile]||settings.gameProfile} · Starting life ${settings.startingLife} · Turn timer ${timerLabel(settings.turnTimerMs||0)}`:'';
 renderLifeCards();
 if(settings&&settings.turnTimer&&!turnTimerSelect.options.length){turnTimerSelect.innerHTML=settings.turnTimer.presetsMs.map(ms=>`<option value="${ms}">${timerLabel(ms)}</option>`).join('')+'<option value="custom">Custom…</option>'}
 if(settings&&!gameSettingsDirty){gameProfileSelect.value=settings.gameProfile;startingLifeInput.value=settings.startingLife;
  const ms=settings.turnTimerMs||0,preset=!settings.turnTimer||settings.turnTimer.presetsMs.includes(ms);
  turnTimerSelect.value=preset?String(ms):'custom';turnTimerCustomRow.hidden=preset;if(!preset)turnTimerCustom.value=ms/1000}
 gameSettingsFields.disabled=gameSettingsSaving||!settings||!settings.canEdit;
 const show=!!(me.authenticated&&me.lifeAvailable);
 lifePanel.hidden=!show;
 if(show&&myLifeTotal.textContent!==String(me.life))myLifeTotal.textContent=String(me.life);
 lifeStep=settings&&settings.gameProfile==='yugioh'?100:1;lifeBigStep=lifeStep===100?1000:5;
 lifeMinus.textContent='−'+lifeStep;lifePlus.textContent='+'+lifeStep;
 lifeMinusBig.textContent='−'+lifeBigStep;lifePlusBig.textContent='+'+lifeBigStep;
 lifeMinus.setAttribute('aria-label',`Subtract ${lifeStep} life`);lifePlus.setAttribute('aria-label',`Add ${lifeStep} life`);
 lifeMinusBig.setAttribute('aria-label',`Subtract ${lifeBigStep} life`);lifePlusBig.setAttribute('aria-label',`Add ${lifeBigStep} life`);
 lifeFields.disabled=lifeBusy||!show||me.eliminated||!['RUNNING','PAUSED'].includes(state.state)||!!state.winConfirm||!!state.eliminationTarget;
 lifeTens.hidden=!settings||!['mtg','mtg_commander'].includes(settings.gameProfile);
 if(settings&&!settings.available)gameSettingsMessage.textContent='Game settings storage is unavailable.';
}
function clockText(ms){const t=Math.max(0,Math.floor(ms/1000));return `${Math.floor(t/60)}:${String(t%60).padStart(2,'0')}`}
function timerLabel(ms){if(!ms)return 'Off (cue after 5 minutes)';return ms%60000?`${ms/1000} seconds`:`${ms/60000} minute${ms===60000?'':'s'}`}
// Atlas decides the phase; the text carries the meaning, the badge and gauge only reinforce it.
function turnTimerText(s){const elapsed=Number(s.turnElapsedMs)||0,limit=Number(s.turnTimerMs)||0;
 if(s.timerPhase==='EXPIRED')return `Time's up · ${clockText(elapsed-limit)} over`;
 if(limit)return `${clockText(s.turnRemainingMs)} left${s.timerPhase==='WARNING'?' · 10-second warning':''}`;
 return s.timerPhase==='LONG_TURN'?`Long turn · ${clockText(elapsed)}`:`Turn time ${clockText(elapsed)}`}
function chooseGameProfile(){gameSettingsDirty=true;startingLifeInput.value=({generic:40,mtg:20,mtg_commander:40,yugioh:8000})[gameProfileSelect.value]}
async function saveGameSettings(event){
 event.preventDefault();if(gameSettingsSaving)return;
 const turnTimerMs=turnTimerSelect.value==='custom'?String(Number(turnTimerCustom.value)*1000):turnTimerSelect.value;
 const settings={gameProfile:gameProfileSelect.value,startingLife:startingLifeInput.value};
 if(turnTimerMs)settings.turnTimerMs=turnTimerMs;
 gameSettingsSaving=true;renderGameLife();
 try{
  const r=await fetch('/api/game/settings',{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(settings)});
  const d=await r.json();if(!r.ok)throw new Error(d.error||'Could not save game settings');
  gameSettingsDirty=false;gameSettingsMessage.textContent='Game settings saved on Atlas.';
  await refreshAll();await refreshAll();
 }catch(e){gameSettingsMessage.textContent=e.message}
 finally{gameSettingsSaving=false;renderGameLife()}
}
async function adjustMyLife(delta,player=Number(lifeTarget.value)){
 if(lifeBusy)return;
 if(!Number.isInteger(delta)||!delta||Math.abs(delta)>1000000){lifeMessage.textContent='Enter a nonzero whole-number change up to 1000000.';return}
 const target=Number(player),own=target===Number(sessionInfo&&sessionInfo.player);
 if(!target||!counterData||!counterData.editable){lifeMessage.textContent='Refresh the current game before changing life.';return}
 lifeBusy=true;renderGameLife();
 try{
  const r=await fetch(own?'/api/control/life':'/api/control/life/request',{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(own?{delta}:{delta,target})});
  const d=await r.json();if(!r.ok)throw new Error(d.error||'Life change rejected');
  lifeMessage.textContent=own?'Life updated.':d.message||'Life change requested.';lifeDelta.value='';
  await refreshAll();await refreshAll();
 }catch(e){lifeMessage.textContent=e.message+' Check the current total before retrying.'}
 finally{lifeBusy=false;renderGameLife();renderCounterControls()}
}

function updatePlayerOptions(select,seats,preferred){
 const options=seats.map(s=>`<option value="${Number(s.player)}">${esc(s.name||'Player '+s.player)}${Number(s.player)===Number(sessionInfo&&sessionInfo.player)?' (me)':''}</option>`).join('');
 if(select.innerHTML!==options){const previous=select.value;select.innerHTML=options;select.value=seats.some(s=>String(s.player)===previous)?previous:String(preferred);if(!select.value&&seats.length)select.value=String(seats[0].player)}
}
// A short-lived +/- marker beside a total that just changed (visual echo only; the total itself is the record).
function lifeDeltaChip(id,life){const now=Date.now(),seen=lifeSeen[id];if(!seen){lifeSeen[id]={life,delta:0,until:0};return ''}if(seen.life!==life){seen.delta=(now<seen.until?seen.delta:0)+(life-seen.life);seen.life=life;seen.until=now+2600}if(now>=seen.until||!seen.delta)return '';return `<span class="life-delta ${seen.delta>0?'up':'down'}" aria-hidden="true">${seen.delta>0?'+':'−'}${Math.abs(seen.delta)}</span>`}
function renderLifeCards(){
 lifeCard.hidden=!(seatData.seats||[]).some(s=>s.lifeAvailable);
 const me=sessionInfo||{},data=counterData;
 const editable=!!(me.authenticated&&!me.eliminated&&data&&data.available&&data.editable&&Number(data.player)===Number(me.player)&&['RUNNING','PAUSED'].includes(statusData?.state)&&!statusData.winConfirm&&!statusData.eliminationTarget);
 const steps=gameSettingsData&&gameSettingsData.gameProfile==='yugioh'?[100,1000]:[1,10];
 const totals=(seatData.seats||[]).filter(s=>s.lifeAvailable).map(s=>{
  const name=s.name||'Player '+s.player,id=Number(s.player),own=id===Number(me.player);
  const tag=s.eliminated?'Eliminated':s.active?'Active turn':own?'You':'';
  return `<article class="life-player${s.active?' active':''}${s.eliminated?' eliminated':''}${own?' me':''}" data-player="${id}" aria-label="${esc(name)} life"><div class="life-head">${avatar(name,id)}<div><h3>${esc(name)}${own?' (me)':''}</h3><span class="life-tag">${tag||'&nbsp;'}</span></div></div><div class="life-num"><span class="life-total">${Number(s.life)}</span>${lifeDeltaChip(id,Number(s.life))}</div><div class="life-buttons">${[-steps[1],-steps[0],steps[0],steps[1]].map(delta=>`<button data-delta="${delta}" aria-label="${delta>0?'Add':'Subtract'} ${Math.abs(delta)} life ${delta>0?'to':'from'} ${esc(name)}" onclick="adjustMyLife(${delta},${id})">${delta>0?'+':'−'}${Math.abs(delta)}</button>`).join('')}</div><p class="small">${own?'Applies immediately':'Requires approval · 15 seconds'}</p></article>`;
 }).join('')||'<div class="empty">Life totals appear when the game starts.</div>';
 if(tableLifeTotals.renderedCards!==totals){tableLifeTotals.renderedCards=totals;const focused=document.activeElement,player=focused?.closest('[data-player]')?.dataset.player,delta=focused?.dataset.delta;tableLifeTotals.innerHTML=totals;if(player&&delta)tableLifeTotals.querySelector(`[data-player="${player}"] [data-delta="${delta}"]`)?.focus({preventScroll:true})}
 tableLifeTotals.querySelectorAll('button').forEach(b=>b.disabled=lifeBusy||!editable||b.closest('.life-player').classList.contains('eliminated'));
}
function renderCounterControls(){
 renderLifeCards();
 const me=sessionInfo||{},data=counterData,available=!!(me.authenticated&&data&&data.available&&Number(data.player)===Number(me.player));
 const owner=me.profileId||'';
 if(counterOwner!==owner){counterOwner=owner;lifeTarget.innerHTML='';commanderSource.innerHTML='';commanderDelta.value='';lifeDelta.value='';lifeMessage.textContent='';commanderMessage.textContent=''}
 updatePlayerOptions(lifeTarget,(seatData.seats||[]).filter(s=>s.lifeAvailable&&!s.eliminated),me.player);
 const own=Number(lifeTarget.value)===Number(me.player);
 lifeTargetHelp.textContent=own?'Your own changes apply immediately.':'This sends a request. The recipient can accept or reject it; Atlas accepts it after 15 seconds without a response.';
 lifeSubmit.textContent=own?'Apply life change':'Request life change';
 if(!available||!data.editable)lifeFields.disabled=true;
 const requests=available?data.requests||[]:[],incoming=requests.find(r=>Number(r.target)===Number(me.player));
 lifeRequestsPanel.hidden=!me.authenticated||(!me.lifeAvailable&&!requests.length);
 const outcomes={accepted:'Accepted',rejected:'Rejected',automatic:'Automatically accepted',cancelled:'Cancelled by a table decision or player departure',failed:'Not applied: life limits or player state changed'};
 const describe=r=>`${playerLabel(r.actor)} requests ${Number(r.delta)>0?'+':''}${r.delta} life for ${playerLabel(r.target)}.`;
 const notice=!available?'Request status unavailable. Reconnect to Atlas; its 15-second timer continues.':incoming?describe(incoming)+(incoming.state==='pending'?' Accept or reject this change.':' '+(outcomes[incoming.state]||incoming.state)+'.'):'No request awaiting your response.';
 if(lifeRequestNotice.textContent!==notice)lifeRequestNotice.textContent=notice;
 const pending=incoming&&incoming.state==='pending';
 lifeRequestAlert.hidden=!pending;
 lifeAlertBar.style.width=pending?Math.max(0,Math.min(100,Number(incoming.remainingMs)/150))+'%':'0';
 const alertText=pending?`${playerLabel(incoming.actor)} requests ${Number(incoming.delta)>0?'+':''}${incoming.delta} life for you. Respond within 15 seconds or Atlas accepts it.`:'';
 if(lifeAlertText.textContent!==alertText)lifeAlertText.textContent=alertText;
 const controls=pending?`<div class="actions"><button class="good" onclick="respondLifeChange(${Number(incoming.id)},true)">Accept life change</button><button class="bad" onclick="respondLifeChange(${Number(incoming.id)},false)">Reject life change</button></div>`:'';
 if(lifeRequestActions.innerHTML!==controls)lifeRequestActions.innerHTML=controls;
 lifeRequestActions.querySelectorAll('button').forEach(button=>button.disabled=counterBusy||!available||!data.editable);
 lifeRequestCountdown.textContent=pending?`Atlas automatically accepts in ${Math.ceil(Number(incoming.remainingMs)/1000)} seconds.`:'';
 const outgoing=requests.filter(r=>Number(r.actor)===Number(me.player)).map(r=>`<p>${esc(describe(r))} ${esc(r.state==='pending'?'Awaiting response · '+Math.ceil(Number(r.remainingMs)/1000)+' seconds':outcomes[r.state]||r.state)}</p>`).join('');
 if(outgoingLifeRequests.innerHTML!==outgoing)outgoingLifeRequests.innerHTML=outgoing;
 commanderPanel.hidden=!available||!data.commanderEnabled;
 commanderFields.disabled=counterBusy||!available||!data.editable;
 if(available&&data.commanderEnabled){
  updatePlayerOptions(commanderSource,seatData.seats||[],(seatData.seats||[]).find(s=>Number(s.player)!==Number(me.player))?.player||me.player);
  const totals=(data.damage||[]).map(d=>`<div class="status-row"><span>${esc(playerLabel(d.source))}</span><strong>Commander 1: ${Number(d.commanders[0])}<br>Commander 2: ${Number(d.commanders[1])}</strong></div>`).join('');
  if(commanderTotals.innerHTML!==totals)commanderTotals.innerHTML=totals;
 }
}
async function sendCounterControl(path,values,messageElement){
 if(counterBusy||!counterData||!counterData.editable)return;
 counterBusy=true;renderCounterControls();
 try{
  const r=await fetch('/api/control/'+path,{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(values)});
  const result=await r.json();if(!r.ok)throw Error(result.error||'Change rejected');
  messageElement.textContent=result.message||'Updated';
  if(path==='commander')commanderDelta.value='';
  await refreshAll();await refreshAll();
 }catch(e){messageElement.textContent=e.message+' Check the current totals before retrying.';showToast(e.message,true);await refreshAll()}
 finally{counterBusy=false;renderCounterControls()}
}
function respondLifeChange(requestId,accept){return sendCounterControl('life/respond',{requestId,accept:accept?'1':'0'},lifeMessage)}
function reviewLifeRequest(){showTab('game');lifeRequestsPanel.scrollIntoView({block:'center'});const button=lifeRequestActions.querySelector('button');if(button)button.focus({preventScroll:true})}
function saveCommanderDamage(event){event.preventDefault();return sendCounterControl('commander',{source:commanderSource.value,commander:commanderSlot.value,delta:commanderDelta.value},commanderMessage)}
function renderInviteCodes(){
 for(const [id,path] of [['portal','/portal'],['join','/login']]){
  const url=location.origin+path,box=document.getElementById(id+'Qr'),link=document.getElementById(id+'QrLink');link.href=url;
  try{const code=qrcode(0,'M');code.addData(url);code.make();box.innerHTML=code.createSvgTag({cellSize:4,margin:8,scalable:true});box.querySelector('svg').setAttribute('aria-label',id==='portal'?'Table portal QR code':'Sign in QR code')}
  catch(_){box.textContent='QR code unavailable. Use the link below.'}
 }
}

let lastAccessKey='';
function renderAccess(){
 const flags=sessionInfo&&sessionInfo.permissions||0;
 deviceSettingsTab.hidden=!(flags&1);devLink.hidden=!(flags&4);gmPanel.hidden=!(flags&2);
 if(!(flags&1)&&!document.getElementById('view-settings').hidden)showTab('game');
 if(!(flags&1))adminAccounts.innerHTML='';if(!(flags&2))gmAccounts.innerHTML='';
 if(!sessionInfo||!sessionInfo.authenticated)myModeration.textContent='Sign in to view your counts.';
 const key=(sessionInfo&&sessionInfo.profileId||'')+':'+flags;
 if(key!==lastAccessKey){lastAccessKey=key;if(sessionInfo&&sessionInfo.authenticated)refreshAccountList()}
}
function renderDeviceSettings(d){const html=(d.devices||[]).map(x=>`<div class="setting-box"><div class="device-top"><div><div class="device-name">${esc(x.label)}</div><div class="mono">${esc(x.hardwareId)}</div></div><div class="actions"><button class="small" onclick="renameSigil(${x.id})">Rename</button>${x.customName?`<button class="small" onclick="clearSigilName(${x.id})">Clear name</button>`:''}</div></div></div>`).join('')||'<div class="empty">No Sigils discovered yet.</div>';if(deviceSettingsList.innerHTML!==html)deviceSettingsList.innerHTML=html}
async function accountPost(path,data){const r=await fetch('/api/accounts/'+path,{method:'POST',headers:{...authHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const d=await r.json();if(!r.ok)throw Error(d.error||'Request failed');return d}
async function setupAdmin(){try{await accountPost('setup',{});adminSetup.hidden=true;await refreshAll();showTab('settings');showToast('Admin account established.')}catch(e){showToast(e.message,true)}}
async function loadSetup(){try{const d=await pollJson('/api/accounts/setup');adminSetup.hidden=!d.setupRequired}catch(e){showToast('Account setup unavailable',true)}}
const PERMISSIONS=[[1,'Admin'],[2,'Game Master'],[4,'Developer'],[8,'GM: reset connections'],[16,'GM: remove from game']];
async function refreshAccountList(){
 if(!sessionToken||!sessionInfo||!sessionInfo.authenticated)return;
 const requestedToken=sessionToken;
 try{const d=await pollJson('/api/accounts',authHeaders()),flags=sessionInfo&&sessionInfo.permissions||0;
 if(requestedToken!==sessionToken||!sessionInfo)return;
 d.accounts.sort((a,b)=>Number(!!a.archived)-Number(!!b.archived));
 const mine=d.accounts.find(a=>a.profileId===sessionInfo.profileId);
 if(mine)myModeration.textContent=`Connection resets: ${mine.connectionResets||0} · Game removals: ${mine.gameRemovals||0}`;
 adminAccounts.innerHTML=flags&1?d.accounts.map(a=>`<form class="setting-box acct" onsubmit="savePermissions(event,'${esc(a.profileId)}')"><div class="player-head">${avatar(a.name||a.profileId,0)}<div><div class="player-name">${esc(a.name||a.profileId)}${a.archived?' · Archived':''}</div><div class="mono">${esc(a.profileId)}</div></div></div><div class="perm-grid">${PERMISSIONS.map(([bit,label])=>`<label class="switch-row"><span>${label}</span><input type="checkbox" value="${bit}" ${a.permissions&bit?'checked':''}></label>`).join('')}</div><div class="actions"><button class="primary small">Save permissions</button><button type="button" class="small ${a.archived?'':'bad'}" onclick="archiveAccount('${esc(a.profileId)}',${!a.archived})">${a.archived?'Restore account':'Archive account'}</button></div></form>`).join(''):'';
 gmAccounts.innerHTML=flags&2?'<div class="roster">'+d.accounts.filter(a=>!a.archived).map(a=>`<div class="setting-box"><div class="player-head">${avatar(a.name||a.profileId,0)}<div><div class="player-name">${esc(a.name||a.profileId)}</div><div class="small">Private counts · resets: ${Number(a.connectionResets)} · removals: ${Number(a.gameRemovals)}</div></div></div><div class="actions" style="margin-top:12px"><button class="small" onclick="moderate('${esc(a.profileId)}','pass')">Force pass</button><button class="small" onclick="moderate('${esc(a.profileId)}','${a.nudgeMuted?'unmute':'mute'}')">${a.nudgeMuted?'Unmute':'Mute'} nudges</button>${flags&8?`<button class="small" onclick="moderate('${esc(a.profileId)}','reset')">Reset connections</button>`:''}${flags&16?`<button class="small bad" onclick="moderate('${esc(a.profileId)}','remove')">Remove from game</button>`:''}</div></div>`).join('')+'</div><p class="hint" style="margin-top:12px">Archived accounts are hidden from Game Master actions. Nudge mute is saved for the future nudge feature. Connection resets keep the seat and life totals. Removal ends active participation.</p>':'';
 }catch(e){showToast(e.message,true)}
}
async function savePermissions(event,id){event.preventDefault();const form=event.currentTarget;const permissions=[...form.querySelectorAll('input:checked')].reduce((n,x)=>n|Number(x.value),0);try{await accountPost('permissions',{profileId:id,permissions});await refreshAll();await refreshAccountList();showToast('Permissions saved')}catch(e){showToast(e.message,true)}}
async function archiveAccount(id,archived){if(!await uiAsk({title:archived?'Archive account?':'Restore account?',text:id+': '+(archived?'Sign-in and Sigil use will be blocked; statistics stay saved. The account must first leave the table.':'Restore this account and its existing permissions?'),ok:archived?'Archive':'Restore',danger:archived}))return;try{await accountPost('archive',{profileId:id,archived:archived?'1':'0'});await refreshAll();await refreshAccountList();showToast(archived?'Account archived':'Account restored')}catch(e){showToast(e.message,true)}}
async function moderate(id,action){const meaning={reset:'Invalidate all browser sessions and suspend Sigil controls until this account signs in again? The seat and life totals stay.',remove:'Remove this player from the game and invalidate their connections?',pass:'Immediately pass this player’s turn?',mute:'Block this account from sending future nudges?',unmute:'Allow this account to send future nudges?'};if(!await uiAsk({title:'Game Master action',text:id+': '+meaning[action],ok:'Apply',danger:action==='remove'||action==='reset'}))return;try{await accountPost('moderate',{profileId:id,action});await refreshAll();await refreshAccountList();showToast('Moderation applied')}catch(e){showToast(e.message,true)}}

let savedTheme='brass';try{savedTheme=localStorage.getItem('turnhubTheme')||'brass'}catch(_){}
setTheme(savedTheme);buildGaugeTicks();renderInviteCodes();portalAddress.textContent=location.host;loadBrowserPrefs();
let savedTab='game';try{savedTab=localStorage.getItem('turnhubPortalTab')||'game'}catch(_){}
showTab(savedTab);refreshAll();setInterval(refreshAll,800);loadSetup();
</script>
</body>
</html>
)HTML";

const char DEV_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><meta name="theme-color" content="#110d09"><title>TurnHub Dev</title>
<script>try{document.documentElement.dataset.theme=localStorage.getItem('turnhubTheme')||'brass'}catch(_){}</script>
<link rel="stylesheet" href="/theme.css">
<style>.activity{max-height:440px;overflow:auto;background:var(--inset);border:1px solid var(--line);border-radius:var(--radius-sm);padding:4px 14px}.event{display:grid;grid-template-columns:78px auto 1fr;gap:10px;align-items:baseline;border-bottom:1px dashed var(--line);padding:8px 0;font:.8rem/1.45 ui-monospace,"SF Mono",Menlo,Consolas,monospace}.event:last-child{border-bottom:0}.time{color:var(--faint)}.kind{color:var(--accent-hi);font-weight:700}[data-theme=parchment] .kind{color:var(--accent)}.message{overflow-wrap:anywhere}@media(max-width:560px){.event{grid-template-columns:1fr}}</style>
</head><body><div class="page">
<header class="page-head"><a class="brand" href="/portal"><span class="brand-mark" aria-hidden="true"></span><span><span class="brand-name">TurnHub</span><span class="brand-sub">Developer</span></span></a><div class="actions"><button class="primary" onclick="downloadLog()">Download serial log</button><button onclick="refresh()">Refresh now</button></div></header>
<main class="stack">
<p id="logStatus" class="msg" role="status" aria-live="polite"></p>
<p class="actions"><a class="btn small ghost" href="/portal">Back to portal</a><a class="btn small ghost" href="/stats">Player stats</a></p>
<section class="card"><div class="card-head"><div><h1 class="eyebrow">Activity monitor</h1><p class="small">Recent in-memory Atlas events. The feed clears when Atlas restarts and is never written to flash.</p></div></div><div id="activity" class="activity">Loading...</div></section>
<div class="cols">
<section class="card"><div class="card-head"><h2 class="eyebrow">Status</h2></div><pre id="status">Loading...</pre></section>
<section class="card"><div class="card-head"><h2 class="eyebrow">Devices</h2></div><pre id="devices">Loading...</pre></section>
<section class="card"><div class="card-head"><h2 class="eyebrow">Seats</h2></div><pre id="seats">Loading...</pre></section>
<section class="card"><div class="card-head"><h2 class="eyebrow">Runtime diagnostics</h2></div><pre id="diagnostics">Loading...</pre></section>
</div></main></div>
<script>const token=()=>localStorage.getItem('turnhubSessionToken')||'';function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}async function fetchJson(id){const x=await fetch('/api/'+id,{cache:'no-store',headers:{'X-TurnHub-Token':token()}});return await x.json()}async function refresh(){try{const a=await fetchJson('diagnostics/activity');document.getElementById('activity').innerHTML=(a.events||[]).map(e=>`<div class="event"><span class="time">${Math.round(Number(e.ageMs||0)/100)/10}s ago</span><span class="kind">${esc(e.kind)}</span><span class="message">${esc(e.message)}</span></div>`).join('')||'<div class="event">Waiting for activity…</div>';for(const id of ['status','devices','seats','diagnostics']){try{document.getElementById(id).textContent=JSON.stringify(await fetchJson(id),null,2)}catch(_){document.getElementById(id).textContent='Disconnected'}}}catch(_){document.getElementById('activity').textContent='Disconnected'}}async function downloadLog(){const st=document.getElementById('logStatus');st.textContent='Preparing serial log…';try{const r=await fetch('/api/diagnostics/log',{cache:'no-store',headers:{'X-TurnHub-Token':token()}});if(!r.ok){let m='Could not download the serial log';try{m=(await r.json()).error||m}catch(_){}throw new Error(m)}const blob=await r.blob();const cd=r.headers.get('Content-Disposition')||'';const f=(cd.match(/filename="([^"]+)"/)||[])[1]||'turnhub-atlas.log';const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download=f;document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);st.textContent='Saved '+f+'. It holds recent output since this boot; RAM only, cleared on restart.'}catch(e){st.textContent=e.message}}refresh();setInterval(refresh,1000)</script></body></html>
)HTML";

}  // namespace TurnHubWeb
