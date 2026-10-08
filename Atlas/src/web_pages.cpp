#include "web_pages.h"

namespace TurnHubWeb {

// Shared stylesheet for every Atlas page, served at /theme.css. Themes are
// token sets on [data-theme]; the choice is a per-browser preference only.
const char THEME_CSS[] PROGMEM = R"CSS(
/* TurnHub theme. Every page links /theme.css. A theme is only a set of tokens
   on [data-theme]; components never hard-code colors. Themes are a per-browser
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
/* Every checkbox is a real input drawn as a switch: the knob position, not only its color, shows the state. */
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

/* Status atoms: every state carries text; color only reinforces it. */
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
[data-motion=reduce] *,[data-motion=reduce] *::before,[data-motion=reduce] *::after{animation:none!important;transition:none!important;scroll-behavior:auto!important}
@media (forced-colors:active){.card::before{display:none}input[type=checkbox]{appearance:auto;width:22px;height:22px}input[type=checkbox]::before{display:none}.brand-mark,.cog{forced-color-adjust:none}}
)CSS";

// The basic portal in flash, served at /portal (and /stats, /dev) whenever
// the microSD card has no portal pack: a dead or missing card must not take
// away the essentials (owner, 2026-10-02). Game status and the player's own
// controls, accessibility preferences, device settings, updates and the
// portal pack install. The full portal is the SD pack (WEB_PORTAL.md).
const char BASIC_PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><title>TurnHub basic portal</title>
<script>try{const h=document.documentElement;h.dataset.theme=localStorage.getItem('turnhubTheme')||(matchMedia('(prefers-contrast: more)').matches?'contrast':'brass');if(localStorage.getItem('turnhubReduceMotion')==='1')h.dataset.motion='reduce'}catch(_){}</script>
<link rel="stylesheet" href="/theme.css">
<style>.page{display:grid;gap:16px}nav{display:flex;gap:8px;flex-wrap:wrap}h2{margin:0 0 10px}h3{margin:20px 0 6px}fieldset{border:0;padding:0;margin:0;display:grid;gap:10px}.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}.row>*{flex:0 1 auto}label.opt{display:flex;gap:10px;align-items:flex-start}label.opt input{flex:none;margin-top:4px}ul{list-style:none;padding:0;margin:0;display:grid;gap:8px}li.p{display:flex;justify-content:space-between;gap:10px;padding:10px 12px;border:1px solid var(--line);border-radius:var(--radius-sm)}li.p.on{border-color:var(--active)}.big{font-size:1.6rem;font-weight:700;font-variant-numeric:tabular-nums}input[type=file]{min-height:0;padding:10px;max-width:100%}input[type=number]{width:8em}.note{color:var(--muted);font-size:.88rem;margin:4px 0 0}#toast{position:fixed;left:16px;right:16px;bottom:16px;padding:12px 14px;border-radius:var(--radius-sm);background:var(--surface-3);border:1px solid var(--line-strong);font-weight:650}#toast.err{border-color:var(--bad);color:var(--bad)}</style>
</head><body><div class="page narrow">
<header class="page-head"><span class="brand"><span class="brand-mark" aria-hidden="true"></span><span><span class="brand-name">TurnHub</span><span class="brand-sub">Basic portal</span></span></span><span id="acct" class="small"></span></header>
<div class="notice" id="card" role="status">Checking the microSD card…</div>
<nav aria-label="Sections"><a class="btn small" href="#game">Game</a><a class="btn small" href="#access">Accessibility</a><a class="btn small" href="#device">Device</a><a class="btn small" href="#updates">Updates</a></nav>

<section class="card" id="game" aria-labelledby="gameH"><h2 id="gameH">Game</h2>
<p id="gameState" role="status" aria-live="polite">Connecting…</p>
<ul id="players"></ul>
<div class="row" id="mine" style="margin-top:12px"></div></section>

<section class="card" id="access" aria-labelledby="accessH"><h2 id="accessH">Accessibility</h2>
<form id="accessForm"><fieldset id="accessFields" disabled>
<label class="opt"><input type="checkbox" id="sigilSound"><span><strong>Sigil sounds</strong><br><span class="note">Beeps and buzzes on your Sigil.</span></span></label>
<strong>Sigil lights</strong>
<label class="opt"><input type="radio" name="ledStyle" value="standard"><span>Standard</span></label>
<label class="opt"><input type="radio" name="ledStyle" value="reduced-motion"><span>Reduced motion: steady lights and slow blinks only</span></label>
<label class="opt"><input type="radio" name="ledStyle" value="monochrome-safe"><span>Monochrome-safe: no signal differs by color alone</span></label>
<div class="row"><label for="longPressMs">Long press (ms)</label><input type="number" id="longPressMs" inputmode="numeric"></div>
<div class="row"><label for="winHoldMs">Win hold (ms)</label><input type="number" id="winHoldMs" inputmode="numeric"></div>
<p class="note" id="limits"></p>
<div><button class="primary">Save accessibility</button></div>
</fieldset></form><p class="note" id="accessNote">Sign in to change your own settings.</p></section>

<section class="card" id="device" aria-labelledby="deviceH"><h2 id="deviceH">Device settings</h2>
<p class="note">Admin only. Wi-Fi, resets and updates also need you verified at the table.</p>
<h3>Verify at the table</h3><p id="presence" class="note" role="status" aria-live="polite"></p>
<div class="row"><button id="showCode" type="button" class="small">Show a code on Atlas</button><input id="code" inputmode="numeric" autocomplete="one-time-code" maxlength="7" placeholder="6-digit code" aria-label="Code from the Atlas screen"><button id="verify" type="button" class="small">Verify</button></div>
<h3>Atlas</h3><p id="atlasInfo" class="note"></p>
<div class="row"><label for="volume">Speaker</label><select id="volume"></select><label for="pairing">Pairing window</label><select id="pairing"></select><button id="saveHw" type="button" class="small">Save</button></div>
<h3>Wi-Fi</h3><p id="net" class="note"></p>
<div class="row"><label for="wifi" class="sr-only">New Wi-Fi password</label><input id="wifi" type="password" minlength="8" maxlength="63" placeholder="New password (8 to 63)" autocomplete="new-password"><button id="saveWifi" type="button" class="small">Save and restart</button></div>
<h3>Sigils</h3><ul id="sigils"></ul>
<h3>Reset</h3><div class="row"><button id="resetTable" type="button" class="small">Return table to lobby</button><button id="factory" type="button" class="small">Factory reset Atlas</button></div></section>

<section class="card" id="updates" aria-labelledby="updH"><h2 id="updH">Updates</h2>
<div class="row"><a class="btn small" href="/update">Atlas firmware</a><a class="btn small" href="/sigil-update">Sigil firmware</a></div>
<h3>Install the portal pack</h3><p class="note">The full portal lives on the microSD card as a signed pack (<code>portal-x.y.z.thfw</code>). Verify at the table first; Lobby or Game Over only.</p>
<div class="row"><input id="file" type="file" accept=".thfw" aria-label="Portal pack"><button id="upload" class="primary" type="button" disabled>Install</button></div>
<progress id="bar" max="100" value="0" aria-label="Upload progress" hidden></progress></section>
<p id="toast" role="status" aria-live="polite" hidden></p>
</div>
<script>
const $=id=>document.getElementById(id),tok=()=>localStorage.getItem('turnhubSessionToken')||'',H=()=>({'X-TurnHub-Token':tok()});
const who=p=>p.displayName||'Player '+p.playerNumber,esc=v=>String(v??'').replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
let me=null,toastTimer=0;
function toast(t,err){const e=$('toast');e.textContent=t;e.className=err?'err':'';e.hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>e.hidden=true,4000)}
async function api(url,opt={}){const r=await fetch(url,{cache:'no-store',...opt,headers:{...H(),...(opt.body?{'Content-Type':'application/x-www-form-urlencoded'}:{})}});let j={};try{j=await r.json()}catch(_){}if(!r.ok)throw Error(j.presenceRequired?'Verify at the table first (Device settings).':(j.error||'Request failed ('+r.status+')'));return j}
async function act(url,body,done){try{const j=await api(url,{method:'POST',body:body?new URLSearchParams(body):undefined});toast(j.message||done||'Done.');refresh()}catch(e){toast(e.message,true)}}
async function loadMe(){me=null;if(tok())try{me=await api('/api/session/me')}catch(_){}
 $('acct').innerHTML=me?esc(me.name||'Signed in')+' · <a href="#" id="out">Sign out</a>':'<a href="/login">Sign in</a>';
 if(me)$('out').onclick=async e=>{e.preventDefault();try{await api('/api/session/logout',{method:'POST'})}catch(_){}localStorage.removeItem('turnhubSessionToken');load()}}
async function refresh(){try{const s=await api('/api/v1/state');const cur=s.players.find(p=>p.playerNumber===s.activePlayer);
 $('gameState').textContent={LOBBY:'Lobby',STARTING:'Starting',RUNNING:'In play',PAUSED:'Paused',GAME_OVER:'Game over'}[s.state]?.concat(cur&&s.state!=='LOBBY'?': '+who(cur)+"'s turn":'')||s.state;
 $('players').innerHTML=s.players.map(p=>`<li class="p${p.playerNumber===s.activePlayer?' on':''}"><span>${esc(who(p))}${p.eliminated?' (out)':''}</span><span class="big" aria-label="Life ${p.life}">${p.life}</span></li>`).join('')||'<li class="note">No players yet.</li>';
 const b=(l,u,body)=>`<button class="small" type="button" data-u="${u}" data-b='${body?JSON.stringify(body):''}'>${l}</button>`;let h='';
 if(!me)h='<span class="note">Sign in to play from this phone.</span>';
 else if(!me.participating)h=b('Join the table','/api/session/join');
 else{if(s.state==='LOBBY')h+=b('Start game','/api/control/start')+b('Leave the table','/api/session/leave');
  if(s.state==='RUNNING'||s.state==='PAUSED')h+=b('Pass','/api/control/pass')+b(s.state==='PAUSED'?'Resume':'Pause','/api/control/pause')+b('Life −1','/api/control/life',{delta:-1})+b('Life +1','/api/control/life',{delta:1})+b('Concede','/api/control/concede');
  if(s.state==='GAME_OVER')h+=b('Rematch','/api/control/rematch')+b('Leave the table','/api/session/leave')}
 if($('mine').innerHTML!==h)$('mine').innerHTML=h}catch(e){$('gameState').textContent=e.message}}
$('mine').onclick=e=>{const t=e.target.closest('button');if(!t)return;if(t.dataset.u.endsWith('concede')&&!confirm('Concede this game?'))return;act(t.dataset.u,t.dataset.b?JSON.parse(t.dataset.b):null)};
async function loadAccess(){if(!me){$('accessFields').disabled=true;return}try{const a=await api('/api/session/accessibility');$('sigilSound').checked=a.sigilSound;document.querySelectorAll('[name=ledStyle]').forEach(r=>r.checked=r.value===a.ledStyle);$('longPressMs').value=a.longPressMs;$('winHoldMs').value=a.winHoldMs;const L=a.limits||{};$('limits').textContent=`Long press ${L.longPressMinMs}–${L.longPressMaxMs} ms; win hold ${L.winHoldMinMs}–${L.winHoldMaxMs} ms and at least ${L.minGapMs} ms longer.`;$('accessFields').disabled=false;$('accessNote').textContent='Your settings follow you to any Sigil.'}catch(e){$('accessNote').textContent=e.message}}
$('accessForm').onsubmit=e=>{e.preventDefault();const s=document.querySelector('[name=ledStyle]:checked');act('/api/session/accessibility',{sigilSound:$('sigilSound').checked?1:0,ledStyle:s?s.value:'standard',longPressMs:$('longPressMs').value,winHoldMs:$('winHoldMs').value},'Accessibility saved.')};
async function loadDevice(){try{const p=await api('/api/presence');$('presence').textContent=p.verified?'You are verified at the table.':'Not verified.'}catch(e){$('presence').textContent=e.message}
 try{const d=await api('/api/devices');const b=d.atlas.battery;$('atlasInfo').textContent=d.atlas.hardwareId+' · firmware '+d.atlas.firmware+(b?' · battery '+b.percent+'%'+(b.low?' (low)':''):'');
  $('sigils').innerHTML=d.devices.map(x=>`<li class="p"><span>${esc(x.label)} · ${x.online?'online':'offline'} · ${esc(x.firmware||'')}</span><button class="small" type="button" data-forget="${Number(x.id)}">Forget</button></li>`).join('')||'<li class="note">No Sigils paired.</li>'}catch(e){$('atlasInfo').textContent=e.message}
 try{const v=await api('/api/speaker'),names=['Off','Low','Medium','High'];$('volume').innerHTML=names.slice(0,(v.max??3)+1).map((n,i)=>`<option value="${i}">${n}</option>`).join('');$('volume').value=v.volume}catch(_){}
 try{const p=await api('/api/pairing');$('pairing').innerHTML=(p.choicesMs||[]).map(ms=>`<option value="${ms}">${ms/1000} s</option>`).join('');$('pairing').value=p.windowMs}catch(_){}
 try{const n=await api('/api/network');$('net').textContent=n.ssid+(n.passwordIsDefault?' · default password':'')+' · '+n.stations+' connected'}catch(e){$('net').textContent=e.message}}
$('showCode').onclick=()=>act('/api/presence/request',null,'Atlas is showing a code now.');
$('verify').onclick=async()=>{await act('/api/presence/confirm?code='+encodeURIComponent($('code').value.trim()),null,'Verified at the table for 10 minutes.');loadDevice()};
$('saveHw').onclick=async()=>{await act('/api/speaker?volume='+$('volume').value,null,'Speaker saved.');await act('/api/pairing?windowMs='+$('pairing').value,null,'Atlas settings saved.')};
$('saveWifi').onclick=()=>{const p=$('wifi').value;if(p.length<8||p.length>63)return toast('Wi-Fi password must be 8 to 63 characters.',true);if(confirm('Atlas restarts and every device disconnects. Continue?'))act('/api/network/password?password='+encodeURIComponent(p),null,'Saved. Atlas is restarting.')};
$('sigils').onclick=e=>{const id=e.target.dataset.forget;if(id&&confirm('Forget this Sigil? It must be paired again.'))act('/api/device/forget?module='+id).then(loadDevice)};
$('resetTable').onclick=()=>{if(confirm('A match in progress ends as a draw and every player leaves the table.'))act('/api/table/reset')};
$('factory').onclick=()=>{if((prompt('Erases every profile, statistic, pairing and setting. Type RESET to confirm.')||'').trim().toUpperCase()==='RESET')act('/api/device/factory-reset?atlas=1',null,'Atlas is restarting as new.')};
async function loadCard(){try{const p=await api('/api/portal');$('card').textContent=!p.card?'No microSD card found, so this is the basic portal. Insert the card that came with Atlas for the full portal.':p.installed?'Portal v'+p.version+' is installed: reload for the full portal.':'The microSD card has no portal yet. Install the portal pack under Updates.'}catch(e){$('card').textContent=e.message}}
$('file').onchange=()=>{$('upload').disabled=!$('file').files.length};
$('upload').onclick=()=>{const f=$('file').files[0];if(!f)return;const x=new XMLHttpRequest(),form=new FormData();form.append('portal',f);$('upload').disabled=true;$('bar').hidden=false;
 x.open('POST','/api/portal/install');x.setRequestHeader('X-TurnHub-Token',tok());
 x.upload.onprogress=e=>{if(e.lengthComputable)$('bar').value=Math.round(e.loaded*100/e.total)};
 x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(_){}if(x.status===200){toast('Portal v'+j.version+' installed. Opening it…');setTimeout(()=>location.assign('/portal'),1500)}else{toast(j.error||'Install failed ('+x.status+')',true);$('upload').disabled=false}};
 x.onerror=()=>{toast('The connection to Atlas was lost. Try again.',true);$('upload').disabled=false};x.send(form)};
async function load(){await loadMe();loadCard();refresh();loadAccess();loadDevice()}
load();setInterval(()=>{if(!document.hidden)refresh()},2000);
</script></body></html>
)HTML";

}  // namespace TurnHubWeb
