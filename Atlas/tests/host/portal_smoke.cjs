// Optional UI smoke check. Uses a small HTTP fixture; C++ scenarios separately
// exercise the real Atlas HTTP handlers/engine. No hardware is contacted.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const root=path.resolve(__dirname,'../..');
const page=(file,name)=>fs.readFileSync(path.join(root,'src',file),'utf8').match(new RegExp('const char '+name+'\\[\\].*?R"HTML\\(([\\s\\S]*?)\\)HTML";'))[1];
const portal=page('web_pages.cpp','PORTAL_HTML'),login=page('profile_login_page.cpp','HTML');
let authenticated=false,joined=false,state='LOBBY',permissions=0,setupRequired=true;
let policy={allowPhysicalWithoutPin:true,hideStatsWithoutAuthentication:true};
let gameSettings={gameProfile:'generic',startingLife:40},life=40;
const api=http.createServer(async(req,res)=>{
 let body='';for await(const chunk of req)body+=chunk;
 const url=new URL(req.url,'http://localhost');
 if(url.pathname==='/'||url.pathname==='/login'){res.setHeader('Content-Type','text/html');res.end(url.pathname==='/'?portal:login);return}
 if(url.pathname==='/portal-qr.js'){const header=fs.readFileSync(path.join(__dirname,'../../include/portal_qr_asset.h'),'utf8');const bytes=header.match(/= \{([\s\S]*?)\};/)[1].match(/\d+/g).map(Number);res.setHeader('Content-Type','application/javascript');res.setHeader('Content-Encoding','gzip');res.end(Buffer.from(bytes));return}
 res.setHeader('Content-Type','application/json');
 const status={state,players:joined?2:0,sigils:0,host:joined?8:-1,starter:joined?1:0,active:state==='RUNNING'?1:0,winner:0,winConfirm:0,eliminationTarget:0,firmware:'0.6.0-dev',espNow:true};
 let result={};
 switch(url.pathname){

  case '/api/accounts/setup':if(req.method==='POST'){permissions=1;setupRequired=false}result={setupRequired};break;
  case '/api/accounts':result={accounts:[{profileId:'AB12CD34',name:'Phone Tester',permissions,connectionResets:1,gameRemovals:0}]};break;
  case '/api/accounts/permissions':permissions=Number(new URLSearchParams(body).get('permissions'));result={ok:true};break;
  case '/api/status':result=status;break;
  case '/api/devices':result={atlas:{hardwareId:'TEST-ATLAS',firmware:'0.6.0-dev'},devices:[]};break;
  case '/api/seats':result={seats:joined?[{module:8,slot:1,slotName:'A',player:1,name:'Phone Tester',profileId:'AB12CD34',virtual:true,hasPin:true,lifeAvailable:state==='RUNNING',life}]:[]};break;
  case '/api/network':result={ssid:'Test fixture',security:'WPA2-PSK',stations:0};break;
  case '/api/profiles':result={profiles:[]};break;
  case '/api/profiles/register':assert(!url.search.includes('pin'));assert.equal(new URLSearchParams(body).get('pin'),'1234');authenticated=true;result={token:'T'.repeat(32),profileId:'AB12CD34'};break;
  case '/api/session/me':if(!authenticated){res.statusCode=401;break}result={authenticated:true,permissions,participating:joined,host:joined,virtual:true,name:'Phone Tester',profileId:'AB12CD34',hasPin:true,module:joined?8:255,slot:1,player:joined?1:0,active:state==='RUNNING',policyAvailable:true,lifeAvailable:state==='RUNNING',life,...policy};break;
  case '/api/session/policy':assert(authenticated);assert.equal(req.headers['x-turnhub-token'],'T'.repeat(32));{const args=new URLSearchParams(body);policy={allowPhysicalWithoutPin:args.get('allowPhysicalWithoutPin')==='1',hideStatsWithoutAuthentication:args.get('hideStatsWithoutAuthentication')==='1'};}result={ok:true};break;
  case '/api/session/join':joined=true;result={ok:true,message:'Joined'};break;
  case '/api/game/settings':
   if(req.method==='POST'){assert(authenticated&&joined&&state==='LOBBY');const args=new URLSearchParams(body);gameSettings={gameProfile:args.get('gameProfile'),startingLife:Number(args.get('startingLife'))};result={ok:true};}
   else result={...gameSettings,available:true,canEdit:authenticated&&joined&&state==='LOBBY'};
   break;
  case '/api/control/life':assert(authenticated&&state==='RUNNING');life+=Number(new URLSearchParams(body).get('delta'));result={ok:true};break;
  case '/api/game/counters':result={available:joined,editable:state==='RUNNING',commanderEnabled:false,player:1,requests:[],damage:[]};break;
  case '/api/control/start':state='RUNNING';life=gameSettings.startingLife;result={ok:true};break;
  default:res.statusCode=404;
 }
 res.end(JSON.stringify(result));
});
(async()=>{
 await new Promise(resolve=>api.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try{
  const context=await browser.newContext({viewport:{width:390,height:844}}),tab=await context.newPage();
  const errors=[];tab.on('pageerror',e=>(console.error(e.message),errors.push(e.message)));
  const base='http://127.0.0.1:'+api.address().port;
  await tab.goto(base);await tab.getByRole('link',{name:'Sign in or create a profile'}).click();
  await tab.getByLabel('Display name',{exact:true}).fill('Phone Tester');
  await tab.getByLabel('Choose a PIN').fill('1234');await tab.getByLabel('Confirm PIN').fill('1234');
  await tab.getByRole('button',{name:'Create account',exact:true}).click();
  await tab.getByRole('button',{name:'Join table',exact:true}).click();
  assert.equal(await tab.getByRole('button',{name:'Device Settings',exact:true}).count(),0);
  await tab.getByRole('button',{name:'Game',exact:true}).click();
  await tab.getByLabel('Game profile',{exact:true}).selectOption('yugioh');
  assert.equal(await tab.getByLabel('Starting life',{exact:true}).inputValue(),'8000');
  await tab.getByLabel('Game profile',{exact:true}).selectOption('mtg');
  assert.equal(await tab.getByLabel('Starting life',{exact:true}).inputValue(),'20');
  await tab.getByLabel('Starting life',{exact:true}).fill('25');
  await tab.evaluate(()=>refreshAll());
  assert.equal(await tab.getByLabel('Starting life',{exact:true}).inputValue(),'25');
  await tab.getByRole('button',{name:'Save game settings',exact:true}).click();
  await tab.getByText('Game settings saved on Atlas.',{exact:true}).waitFor();
  await tab.reload();await tab.waitForFunction(()=>gameSettingsData?.startingLife===25);
  assert.equal(await tab.getByLabel('Starting life',{exact:true}).inputValue(),'25');
  await tab.getByRole('button',{name:'Game',exact:true}).click();
  await tab.getByRole('button',{name:'Start game',exact:true}).click();
  await tab.getByRole('button',{name:'Pass turn',exact:true}).waitFor();
  await tab.waitForFunction(()=>document.getElementById('myLifeTotal').textContent==='25');
  await tab.getByText('Custom and preset life changes',{exact:true}).click();
  await tab.getByRole('button',{name:'Subtract 5 life',exact:true}).click();
  await tab.waitForFunction(()=>document.getElementById('myLifeTotal').textContent==='20');
  await tab.getByLabel('Custom life change (negative to subtract)').fill('-21');
  await tab.getByRole('button',{name:'Apply life change',exact:true}).click();
  await tab.waitForFunction(()=>document.getElementById('myLifeTotal').textContent==='-1');
  assert.equal(await tab.locator('#tableLifeTotals .life-total').textContent(),'-1');
  await tab.screenshot({path:path.join(__dirname,'build','portal-life-mobile.png'),fullPage:true});
  assert(await tab.getByRole('button',{name:'Save game settings',exact:true}).isDisabled());
  await tab.getByRole('button',{name:'My Account',exact:true}).click();
  const physical=tab.getByLabel('Allow physical use without a PIN',{exact:true});
  const hidden=tab.getByLabel('Hide stats without authentication',{exact:true});
  assert(await physical.isChecked());assert(await hidden.isChecked());
  await physical.uncheck();
  await tab.evaluate(()=>refreshAll()); // Polling must not discard unsaved choices.
  assert.equal(await physical.isChecked(),false);
  await tab.getByRole('button',{name:'Save profile choices',exact:true}).click();
  await tab.getByText('Profile choices saved on Atlas.',{exact:true}).waitFor();
  assert.deepEqual(policy,{allowPhysicalWithoutPin:false,hideStatsWithoutAuthentication:true});
  await tab.reload();await tab.getByRole('button',{name:'My Account',exact:true}).click();
  await tab.waitForFunction(()=>document.getElementById('allowPhysicalWithoutPin').checked===false&&sessionInfo?.policyAvailable);
  assert(await hidden.isChecked());
  assert(await tab.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Mobile horizontal overflow');
  await tab.screenshot({path:path.join(__dirname,'build','portal-mobile.png'),fullPage:true});
  await tab.setViewportSize({width:1920,height:1080});
  assert(await tab.locator('.shell').evaluate(el=>el.getBoundingClientRect().width>1600));
  await tab.screenshot({path:path.join(__dirname,'build','portal-desktop.png'),fullPage:true});

  await tab.getByRole('button',{name:'Make my account the initial Admin',exact:true}).click();
  await tab.getByRole('button',{name:'Device Settings',exact:true}).waitFor();
  await tab.getByLabel('Game Master',{exact:true}).check();
  await tab.getByLabel('Developer',{exact:true}).check();
  await tab.getByRole('button',{name:'Save permissions',exact:true}).click();
  await tab.getByRole('link',{name:'Developer',exact:true}).waitFor();
  await tab.getByRole('button',{name:'Players',exact:true}).click();
  await tab.getByRole('button',{name:'Force pass',exact:true}).waitFor();
  assert.equal(await tab.getByRole('link',{name:'Atlas firmware',exact:true}).count(),0);
  assert.deepEqual(errors,[]);
  console.log('PASS portal smoke: profiles, game setup, life controls/totals, policy save, mobile/desktop fit, no JS errors');
 }finally{await browser.close();api.close()}
})().catch(e=>{console.error(e);api.close();process.exitCode=1});
