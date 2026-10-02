// Render the basic portal from flash (BASIC_PORTAL_HTML in web_pages.cpp), the
// portal Atlas serves when its microSD card holds no pack. Checks that the
// essentials work against a fake Atlas: game controls, accessibility,
// device settings and the portal install. Native scenarios cover the real API.
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),assert=require('node:assert/strict');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const pages=fs.readFileSync(path.join(__dirname,'../../src/web_pages.cpp'),'utf8');
const portal=pages.match(/BASIC_PORTAL_HTML\[\] PROGMEM = R"HTML\(([\s\S]*?)\)HTML";/)[1],theme=pages.match(/THEME_CSS\[\].*?R"CSS\(([\s\S]*?)\)CSS";/)[1];
const running=JSON.parse(fs.readFileSync(path.join(__dirname,'../../../protocol/examples/running.response.json'),'utf8'));
const posts=[];let access={ok:true,stored:true,sigilSound:true,ledStyle:'standard',longPressMs:600,winHoldMs:2000,limits:{longPressMinMs:300,longPressMaxMs:3000,winHoldMinMs:1300,winHoldMaxMs:6000,minGapMs:1000,stepMs:100}};
const server=http.createServer(async(req,res)=>{
 let body='';for await(const chunk of req)body+=chunk;
 const url=new URL(req.url,'http://localhost');
 if(url.pathname==='/portal'){res.setHeader('Content-Type','text/html');return res.end(portal)}
 if(url.pathname==='/theme.css'){res.setHeader('Content-Type','text/css');return res.end(theme)}
 res.setHeader('Content-Type','application/json');
 if(req.method==='POST')posts.push(url.pathname+url.search+(body?' '+body:''));
 const json={
  '/api/portal':{card:false,installed:false,version:''},
  '/api/session/me':{authenticated:true,name:'Alex',permissions:1,participating:true,player:1,active:true},
  '/api/v1/state':running,
  '/api/session/accessibility':access,
  '/api/presence':{verified:false},
  '/api/devices':{atlas:{hardwareId:'THA-TEST',firmware:'0.6.5'},devices:[{id:8,label:'Sigil 1',online:true,firmware:'0.9.7'}]},
  '/api/speaker':{volume:2,name:'Medium',max:3},
  '/api/pairing':{windowMs:60000,sigilWindowMs:60000,choicesMs:[60000,120000]},
  '/api/network':{ssid:'TurnHub-Atlas',passwordIsDefault:true,stations:2},
 }[url.pathname];
 if(req.method==='POST'&&url.pathname==='/api/session/accessibility'){const a=new URLSearchParams(body);access={...access,sigilSound:a.get('sigilSound')==='1',ledStyle:a.get('ledStyle')};return res.end(JSON.stringify(access))}
 res.end(JSON.stringify(json??{ok:true,message:'Done.'}));
});
server.listen(0,'127.0.0.1',async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.PLAYWRIGHT_CHANNEL??'msedge'});
 try{
  const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>localStorage.setItem('turnhubSessionToken','token'));
  await page.goto(`http://127.0.0.1:${server.address().port}/portal`);
  await page.getByRole('button',{name:'Pass'}).waitFor();
  assert.match(await page.textContent('#card'),/No microSD card/);
  assert.match(await page.textContent('#gameState'),/In play/);
  await page.getByRole('button',{name:'Pass'}).click();
  await page.getByRole('button',{name:'Life +1'}).click();
  await page.getByLabel(/Reduced motion/).check();
  await page.getByLabel(/Sigil sounds/).uncheck();
  await page.getByRole('button',{name:'Save accessibility'}).click();
  await page.locator('#volume').selectOption('3');
  await page.getByRole('button',{name:'Save',exact:true}).click();
  await page.waitForTimeout(300);
  assert.ok(posts.includes('/api/control/pass'),posts.join('\n'));
  assert.ok(posts.includes('/api/control/life delta=1'),posts.join('\n'));
  assert.ok(posts.some(p=>p.startsWith('/api/session/accessibility')&&p.includes('sigilSound=0')&&p.includes('ledStyle=reduced-motion')),posts.join('\n'));
  assert.ok(posts.includes('/api/speaker?volume=3'),posts.join('\n'));
  assert.match(await page.textContent('#sigils'),/Sigil 1/);
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth),390);
  assert.deepEqual(errors,[]);
  fs.mkdirSync(path.join(__dirname,'build'),{recursive:true});
  await page.screenshot({path:path.join(__dirname,'build','basic-portal-mobile.png'),fullPage:true});
  console.log('PASS basic portal: game controls, accessibility, speaker, Sigils, no microSD card, mobile fit, no JS errors');
 }finally{await browser.close();server.close()}
});
