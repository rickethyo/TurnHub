// Real embedded maintenance UI, preserving local signed uploads and Developer tests.
const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path');
const { chromium } = require('playwright-core');
const source = require('./portal_source.cjs');
const site = path.resolve(__dirname, '../../web/dist/site');
(async () => {
 const browser = await chromium.launch({headless:true, executablePath:process.env.PLAYWRIGHT_EXECUTABLE_PATH || '/usr/bin/chromium'});
 try {
  const page = await browser.newPage({viewport:{width:390,height:844}});
  const errors=[], posts=[];
  let staged=false;
  page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>{localStorage.setItem('turnhubSessionToken','T'.repeat(32));localStorage.setItem('turnhubTheme','contrast')});
  await page.route('http://atlas.test/**', async route=>{
   const req=route.request(), u=new URL(req.url());let data={};
   if(['/update','/sigil-update','/dev','/login'].includes(u.pathname))return route.fulfill({contentType:'text/html',body:source.page(u.pathname.slice(1)+'.html')});
   if(u.pathname.startsWith('/assets/')){
    const file=path.join(site,u.pathname);assert(fs.existsSync(file),file);
    return route.fulfill({body:fs.readFileSync(file),contentType:file.endsWith('.css')?'text/css':file.endsWith('.woff2')?'font/woff2':'application/javascript'});
   }
   assert(!u.pathname.startsWith('/api/portal'),'Retired portal API requested');
   if(req.method()==='POST'){
    assert.equal(req.headers()['x-turnhub-token'],'T'.repeat(32));posts.push([u.pathname,u.search,req.postData()]);
   }
   switch(u.pathname){
    case '/api/status':data={firmware:'0.7.7',build:'smoke'};break;
    case '/api/devices':data={devices:[{id:0,label:'E-paper',online:true,metadata:true,display:'epaper',capabilities:0,firmware:'0.9.17'},{id:1,label:'OLED',online:true,metadata:true,display:'oled',capabilities:0,firmware:'0.9.17'}]};break;
    case '/api/sigil-firmware':if(req.method()==='POST')staged=true;data={staged,product:3,version:'0.9.18',busy:false,stage:'idle',message:'Package checked'};break;
    case '/api/sigil-update':data={message:'Update started'};break;
    case '/api/firmware':return route.fulfill({status:400,contentType:'application/json',body:'{"error":"Signed package required"}'});
    case '/api/device/test':data={message:'Test started. Check the hardware.'};break;
    case '/api/diagnostics/activity':data={events:[]};break;
    default:data={};
   }
   return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
  });
  await page.goto('http://atlas.test/update');
  assert.equal(await page.evaluate(()=>document.documentElement.dataset.theme),'contrast');
  await page.locator('#file').setInputFiles({name:'atlas.thfw',mimeType:'application/octet-stream',buffer:Buffer.from('invalid')});
  await page.locator('#upload').click();
  await page.getByText('Signed package required',{exact:true}).waitFor();
  assert(posts.some(p=>p[0]==='/api/firmware'));
  await page.goto('http://atlas.test/sigil-update');
  assert.equal(await page.evaluate(()=>document.documentElement.dataset.theme),'contrast');
  await page.locator('#package').setInputFiles({name:'sigil.thfw',mimeType:'application/octet-stream',buffer:Buffer.from('fixture')});
  await page.locator('#uploadButton').click();
  await page.getByText('Package checked',{exact:true}).waitFor();
  await page.evaluate(()=>refresh());
  await page.locator('#sigil').selectOption('1');
  await page.locator('#startButton').click();
  await page.getByText('Update started',{exact:true}).waitFor();
  const selected=posts.find(p=>p[0]==='/api/sigil-update');assert.equal(selected[2],'module=1');
  await page.goto('http://atlas.test/dev');
  assert.equal(await page.evaluate(()=>document.documentElement.dataset.theme),'contrast');
  await page.getByRole('button',{name:'Screen Test',exact:true}).click();
  await page.locator('#testStatus').getByText('Test started. Check the hardware.',{exact:true}).waitFor();
  await page.locator('#testTarget').selectOption('1');
  await page.getByRole('button',{name:'Status Light Test',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#testStatus').textContent==='Test started. Check the hardware.');
  assert(posts.some(p=>p[0]==='/api/device/test'&&p[1].includes('target=atlas')&&p[1].includes('test=screen')));
  assert(posts.some(p=>p[0]==='/api/device/test'&&p[1].includes('target=sigil')&&p[1].includes('test=lights')&&p[1].includes('module=1')));
  assert.deepEqual(errors,[]);
  console.log('PASS embedded maintenance: Atlas local upload/errors, Sigil upload/target selection, Developer hardware controls and auth headers');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
