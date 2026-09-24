// Render the production portal with two independent browsers. Native scenarios
// verify the authoritative engine/HTTP behavior, including Atlas's timer.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const pages=fs.readFileSync(path.join(__dirname,'../../src/web_pages.cpp'),'utf8');
const portal=pages.match(/const char PORTAL_HTML\[\].*?R"HTML\(([\s\S]*?)\)HTML";/)[1],theme=pages.match(/THEME_CSS\[\].*?R"CSS\(([\s\S]*?)\)CSS";/)[1];
const names=['Alex','Blair','Casey'],life=[40,40,40],damage=Array.from({length:3},()=>Array.from({length:3},()=>[0,0]));
let requests=[],sequence=0,profile='mtg_commander',state='RUNNING',offline=false;
const api=http.createServer(async(req,res)=>{
 let body='';for await(const chunk of req)body+=chunk;
 const url=new URL(req.url,'http://localhost'),args=new URLSearchParams(body),player=Number(req.headers['x-turnhub-token']),i=player-1;
 if(url.pathname==='/'){res.setHeader('Content-Type','text/html');res.end(portal);return}
 if(url.pathname==='/theme.css'){res.setHeader('Content-Type','text/css');res.end(theme);return}
 if(url.pathname==='/portal-qr.js'){const header=fs.readFileSync(path.join(__dirname,'../../include/portal_qr_asset.h'),'utf8');const bytes=header.match(/= \{([\s\S]*?)\};/)[1].match(/\d+/g).map(Number);res.setHeader('Content-Type','application/javascript');res.setHeader('Content-Encoding','gzip');res.end(Buffer.from(bytes));return}
 res.setHeader('Content-Type','application/json');let result={};
 if(offline){res.statusCode=503;res.end('{}');return}
 const seats=names.map((name,n)=>({name,player:n+1,module:n+8,slot:1,slotName:'A',profileId:'PLAYER0'+(n+1),virtual:true,hasPin:true,lifeAvailable:true,life:life[n],active:n===0}));
 switch(url.pathname){
  case '/api/status':result={state,players:3,sigils:0,host:8,starter:1,active:1,winner:0,winConfirm:0,eliminationTarget:0,firmware:'test',espNow:true};break;
  case '/api/devices':result={atlas:{hardwareId:'FIXTURE',firmware:'test'},devices:[]};break;
  case '/api/seats':result={seats};break;
  case '/api/accounts/setup':result={setupRequired:false};break;
  case '/api/accounts':result={accounts:[]};break;
  case '/api/session/me':result={authenticated:true,participating:true,permissions:0,host:player===1,virtual:true,profileId:'PLAYER0'+player,name:names[i],module:i+8,slot:1,player,active:player===1,lifeAvailable:true,life:life[i],policyAvailable:true,hasPin:true};break;
  case '/api/game/settings':result={gameProfile:profile,startingLife:40,available:true,canEdit:false};break;
  case '/api/game/counters':result={available:true,editable:true,commanderEnabled:profile==='mtg_commander',player,requests:requests.filter(r=>r.actor===player||r.target===player),damage:damage[i].map((commanders,n)=>({source:n+1,commanders}))};break;
  case '/api/control/life':life[i]+=Number(args.get('delta'));result={ok:true};break;
  case '/api/control/life/request':{
   const target=Number(args.get('target'));assert.notEqual(target,player);
   requests=requests.filter(r=>r.target!==target);
   requests.push({id:++sequence,actor:player,target,delta:Number(args.get('delta')),state:'pending',remainingMs:15000});
   result={ok:true,message:'Life change requested'};break;
  }
  case '/api/control/life/respond':{
   const request=requests.find(r=>r.id===Number(args.get('requestId')));assert.equal(request.target,player);assert.equal(request.state,'pending');
   request.state=args.get('accept')==='1'?'accepted':'rejected';if(request.state==='accepted')life[i]+=request.delta;
   result={ok:true,message:'Life change '+request.state};break;
  }
  case '/api/control/commander':{
   const delta=Number(args.get('delta')),source=Number(args.get('source'))-1,commander=Number(args.get('commander'))-1;
   if(damage[i][source][commander]+delta<0){res.statusCode=409;result={error:'Counter cannot be negative'};break}
   damage[i][source][commander]+=delta;life[i]-=delta;result={ok:true,message:'Commander damage and life updated'};break;
  }
  default:res.statusCode=404;
 }
 res.end(JSON.stringify(result));
});
(async()=>{
 fs.mkdirSync(path.join(__dirname,'build'),{recursive:true});
 await new Promise(resolve=>api.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({headless:true,channel:'msedge'}),errors=[];
 try{
  const tabs=[];
  for(const player of [1,2]){
   const context=await browser.newContext({viewport:{width:390,height:844},reducedMotion:'reduce'});
   await context.addInitScript(value=>localStorage.setItem('turnhubSessionToken',String(value)),player);
   const tab=await context.newPage();tab.on('pageerror',e=>(console.error(e.message),errors.push(e.message)));tabs.push(tab);
   await tab.goto('http://127.0.0.1:'+api.address().port);
   await tab.waitForFunction(()=>counterData?.available);
  }
  const [sender,recipient]=tabs;
  assert.equal(await sender.locator('.life-player').count(),3);
  assert.equal(await sender.locator('.life-buttons button').count(),12);
  await sender.getByRole('button',{name:'Add 1 life to Alex',exact:true}).click();
  await sender.waitForFunction(()=>document.querySelector('[data-player="1"] .life-total').textContent==='41');
  await sender.getByRole('button',{name:'Subtract 1 life from Alex',exact:true}).click();
  await sender.waitForFunction(()=>document.querySelector('[data-player="1"] .life-total').textContent==='40');
  await sender.getByRole('button',{name:'Players',exact:true}).click();
  assert.equal(await sender.locator('.qr-box svg').count(),2);
  await sender.evaluate(()=>window.scrollTo(0,0));
  await sender.screenshot({path:path.join(__dirname,'build','invite-qr.png'),fullPage:true});
  await sender.getByRole('button',{name:'Game',exact:true}).click();
  await sender.getByRole('button',{name:'Subtract 10 life from Blair',exact:true}).click();
  await recipient.getByRole('button',{name:'Players',exact:true}).click();
  await recipient.evaluate(()=>refreshAll());
  await recipient.getByRole('button',{name:'Review life request',exact:true}).click();
  await recipient.getByRole('button',{name:'Accept life change',exact:true}).waitFor();
  assert.equal(life[1],40);
  assert.match(await recipient.locator('#lifeRequestNotice').textContent(),/Alex requests -10 life for Blair/);
  const accept=recipient.getByRole('button',{name:'Accept life change',exact:true});
  await accept.focus();requests[0].remainingMs=9000;
  await recipient.evaluate(()=>refreshAll());
  assert(await accept.evaluate(el=>document.activeElement===el),'Polling lost approval keyboard focus');
  await recipient.keyboard.press('Enter');
  await recipient.waitForFunction(()=>document.getElementById('myLifeTotal').textContent==='30');
  await sender.evaluate(()=>refreshAll());assert.match(await sender.locator('#outgoingLifeRequests').textContent(),/Accepted/);
  await sender.getByRole('button',{name:'Add 10 life to Blair',exact:true}).click();
  await recipient.evaluate(()=>refreshAll());
  await recipient.getByRole('button',{name:'Reject life change',exact:true}).click();
  await recipient.waitForFunction(()=>document.getElementById('lifeRequestNotice').textContent.includes('Rejected'));
  assert.equal(life[1],30);
  await recipient.getByLabel('Commander owner',{exact:true}).selectOption('1');
  await recipient.getByLabel('Damage change (negative to correct)').fill('21');
  await recipient.getByRole('button',{name:'Update damage and life',exact:true}).click();
  await recipient.waitForFunction(()=>document.getElementById('myLifeTotal').textContent==='9');
  assert.match(await recipient.locator('#commanderTotals').textContent(),/Commander 1: 21/);
  await recipient.getByLabel('Commander',{exact:true}).selectOption('2');
  await recipient.getByLabel('Damage change (negative to correct)').fill('2');
  await recipient.evaluate(()=>refreshAll());
  assert.equal(await recipient.getByLabel('Commander',{exact:true}).inputValue(),'2');
  assert.equal(await recipient.getByLabel('Damage change (negative to correct)').inputValue(),'2');
  await recipient.getByRole('button',{name:'Update damage and life',exact:true}).click();
  await recipient.waitForFunction(()=>document.getElementById('myLifeTotal').textContent==='7');
  await recipient.getByLabel('Damage change (negative to correct)').fill('-3');
  await recipient.getByRole('button',{name:'Update damage and life',exact:true}).click();
  await recipient.getByText(/Counter cannot be negative/).first().waitFor();
  assert.equal(life[1],7);assert.equal(damage[1][0][1],2);
  await recipient.getByLabel('Damage change (negative to correct)').fill('-1');
  await recipient.getByRole('button',{name:'Update damage and life',exact:true}).click();
  await recipient.waitForFunction(()=>document.getElementById('myLifeTotal').textContent==='8');
  await recipient.reload();await recipient.waitForFunction(()=>counterData?.available);
  assert.match(await recipient.locator('#commanderTotals').textContent(),/Commander 2: 1/);
  await sender.getByText('Custom and preset life changes',{exact:true}).click();
  await sender.getByLabel('Player whose life changes').selectOption('2');
  await sender.getByRole('button',{name:'Subtract 5 life',exact:true}).click();
  await recipient.evaluate(()=>refreshAll());
  await recipient.getByRole('button',{name:'Accept life change',exact:true}).waitFor();
  for(const tab of tabs)assert(await tab.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Mobile horizontal overflow');
  await recipient.evaluate(()=>window.scrollTo(0,0));
  await recipient.screenshot({path:path.join(__dirname,'build','counter-mobile.png'),fullPage:true});
  await recipient.setViewportSize({width:1440,height:1000});
  await recipient.evaluate(()=>window.scrollTo(0,0));
  await recipient.screenshot({path:path.join(__dirname,'build','counter-desktop.png'),fullPage:true});
  // Atlas's state, not a client-side timer, resolves the prompt.
  requests[0].state='automatic';life[1]+=requests[0].delta;requests[0].remainingMs=0;
  await recipient.evaluate(()=>refreshAll());
  assert.equal(await recipient.getByRole('button',{name:'Accept life change',exact:true}).count(),0);
  assert.match(await recipient.locator('#lifeRequestNotice').textContent(),/Automatically accepted/);
  offline=true;await recipient.evaluate(()=>refreshAll());await recipient.evaluate(()=>refreshAll());
  assert(await recipient.locator('#lifeSubmit').isDisabled());
  assert(await recipient.locator('#commanderFields').evaluate(el=>el.disabled));
  assert.equal(await recipient.locator('.life-buttons button:enabled').count(),0);
  assert.match(await recipient.locator('#lifeRequestNotice').textContent(),/Reconnect to Atlas/);
  offline=false;profile='generic';await recipient.evaluate(()=>refreshAll());await recipient.evaluate(()=>refreshAll());
  assert(await recipient.locator('#commanderPanel').isHidden());
  assert(await recipient.getByRole('button',{name:'Add 10 life',exact:true}).isHidden());
  assert.deepEqual(errors,[]);
  console.log('PASS counter portal: two browsers, life approval/rejection, keyboard focus, linked Commander corrections, reconnect and responsive layout');
 }finally{await browser.close();api.close()}
})().catch(e=>{console.error(e);api.close();process.exitCode=1});
