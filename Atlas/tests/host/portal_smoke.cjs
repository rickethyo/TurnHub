// Embedded administration: identical assets with and without a card. Real HTML/JS.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let chromium; try { ({ chromium } = require('playwright')); } catch { ({ chromium } = require('playwright-core')); }
const root = path.resolve(__dirname, '../../..');
const source = require('./portal_source.cjs');
const full = source.html;
(async () => {
 const browser = await chromium.launch({headless:true, executablePath:process.env.PLAYWRIGHT_EXECUTABLE_PATH || '/usr/bin/chromium'});
 try {
  for (const [kind, html] of [['with-card', full], ['cardless', full]]) {
   const page = await browser.newPage({viewport:{width:390,height:844}});
   const errors=[], posts=[]; let permissions=1, initial=false, volume=2;
   page.on('pageerror',e=>errors.push(e.message));
   await page.addInitScript(()=>{localStorage.setItem('turnhubSessionToken','T'.repeat(32));localStorage.setItem('turnhubTheme','brass')});
   await page.route('http://atlas.test/**', async route=>{
    const req=route.request(), url=new URL(req.url()); let data={};
    if(url.pathname==='/portal')return route.fulfill({contentType:'text/html',body:html});
    if(url.pathname==='/theme.css')return route.fulfill({contentType:'text/css',body:''});
    if(url.pathname.startsWith('/assets/')) {
     const file=path.join(root,'Atlas/web/dist/site',url.pathname);
     return route.fulfill({body:fs.existsSync(file)?fs.readFileSync(file):'',contentType:file.endsWith('.css')?'text/css':'application/javascript'});
    }
    if(req.method()==='POST')posts.push(url.pathname+' '+(req.postData()||url.search));
    switch(url.pathname){
     case '/api/session/me': data={authenticated:true,name:'Admin',profileId:'ADMIN',permissions,participating:false};break;
     case '/api/accounts/setup':data={setupRequired:initial};break;
     case '/api/setup':data={stage:initial?'welcome':'done',adminExists:!initial,ssid:'TurnHub-Atlas'};break;
     case '/api/status':data={state:'LOBBY',firmware:'0.7.6',build:'test',espNow:true,game:1,games:[{game:1,state:'LOBBY',players:0},{game:2,state:'RUNNING',players:2}]};break;
     case '/api/v1/state':data={state:'LOBBY',players:[]};break;
     case '/api/devices':data={atlas:{hardwareId:'TEST',firmware:'0.7.6'},devices:[{id:0,label:'Sigil 1',firmware:'test',hardwareId:'TEST',online:true}],pendingPairings:[]};break;
     case '/api/network':data={ssid:'TurnHub-Atlas',security:'WPA2',stations:1};break;
     case '/api/presence':data={verified:true,codeRequired:false,canRequest:true};break;
     case '/api/portal':throw Error('Retired portal status must not be requested');
     case '/api/pairing':data={windowMs:60000,choicesMs:[15000,30000,60000]};break;
     case '/api/speaker':if(req.method()==='POST')volume=Number(url.searchParams.get('volume'));data={volume,max:3};break;
     case '/api/accounts':data={accounts:[{profileId:'ADMIN',name:'Admin',permissions:1,primary:true,hasPin:true}]};break;
     default:if(req.method()!=='POST')return route.fulfill({status:404,body:'{}'});data={ok:true};
    }
    await route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   });
   await page.goto('http://atlas.test/portal');
   await page.waitForFunction(()=>document.getElementById('speakerVolumeSelect')?.options.length || document.getElementById('volume')?.options.length);
   for(const name of ['Join table','Pass turn','Claim win','Concede','Tablet mode','Apply life change'])assert.equal(await page.getByRole('button',{name,exact:true}).count(),0);
   assert.equal(await page.locator('a[href="/tablet"],a[href="/stats"]').count(),0);
   assert.deepEqual(await page.locator('#portalTheme option').allTextContents(),['System','Dark','Light','High contrast']);
   assert.equal(await page.inputValue('#portalTheme'),'auto','Old Brass choice becomes System');
   await page.selectOption('#portalTheme','contrast');
   assert.equal(await page.evaluate(()=>document.documentElement.dataset.theme),'contrast');
   assert.equal(await page.evaluate(()=>localStorage.getItem('turnhubTheme')),'brass','Preview must not save');
   await page.getByRole('button',{name:'Save appearance',exact:true}).click();
   assert.equal(await page.evaluate(()=>localStorage.getItem('turnhubTheme')),'contrast');
   await page.selectOption('#speakerVolumeSelect','3');
   await page.evaluate(()=>refreshAll());assert.equal(await page.inputValue('#speakerVolumeSelect'),'3');
   assert.equal(posts.length,0,'Staging settings must not save');
   await page.getByRole('button',{name:'Save Atlas settings',exact:true}).click();
   await page.waitForFunction(()=>document.body.innerText.includes('Atlas settings saved.'));
   assert.equal(volume,3);
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),390,`${kind}: mobile overflow`);
   if(process.env.PORTAL_RENDERS) {
    fs.mkdirSync(path.join(__dirname,'build'),{recursive:true});
    await page.screenshot({path:path.join(__dirname,'build',kind+'-admin-portal.png'),fullPage:true});
   }
   assert(posts.every(p=>!/^\/api\/(control|tablet)\//.test(p)),posts.join('\n'));
   permissions=0;await page.evaluate(()=>typeof refreshAll==='function'?refreshAll():load());
   assert.equal(await page.locator('#main').isVisible(),false);
   initial=true;await page.evaluate(()=>typeof refreshAll==='function'?refreshAll():load());
   assert.equal(await page.locator('#setupWizard').isVisible(),true);
   assert.deepEqual(errors,[],`${kind}: JavaScript errors`);
   await page.close();console.log(`PASS ${kind} portal: admin access, staged hardware settings, bootstrap, no gameplay requests`);
  }
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
