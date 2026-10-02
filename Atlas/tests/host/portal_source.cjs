// Which portal the smoke checks render. Default: the built-in flash portal
// (PORTAL_HTML in web_pages.cpp). PORTAL_PACK=1: the SD portal pack, built
// first with `python3 Atlas/web/build.py` (dist/site/), whose /assets/ files
// are served the way Atlas serves them from the card (PORTAL_PACK.md).
const fs=require('node:fs'),path=require('node:path');
const site=path.resolve(__dirname,'../../web/dist/site');
const types={'.css':'text/css','.js':'application/javascript','.svg':'image/svg+xml','.woff2':'font/woff2','.txt':'text/plain','.json':'application/json','.png':'image/png','.webmanifest':'application/manifest+json'};
const enabled=process.env.PORTAL_PACK==='1';
if(enabled&&!fs.existsSync(path.join(site,'index.html')))throw new Error('PORTAL_PACK=1: run python3 Atlas/web/build.py first');
module.exports={
 enabled,
 html:enabled?fs.readFileSync(path.join(site,'index.html'),'utf8'):null,
 // Another page of the pack (login.html, stats.html...), or null in flash mode.
 page:name=>enabled?fs.readFileSync(path.join(site,name),'utf8'):null,
 // True when the request was a pack asset and has been answered.
 serve(pathname,res){
  if(!enabled||!pathname.startsWith('/assets/'))return false;
  const file=path.join(site,path.normalize(pathname).replace(/^[/\\]+/,''));
  if(!file.startsWith(site)||!fs.existsSync(file)){res.statusCode=404;res.end();return true}
  res.setHeader('Content-Type',types[path.extname(file)]||'application/octet-stream');
  res.end(fs.readFileSync(file));return true;
 },
};
