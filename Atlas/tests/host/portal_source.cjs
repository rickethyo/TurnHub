// Build the same source bundle embedded by PlatformIO; always refresh to avoid stale smoke assets.
const fs=require('node:fs'),path=require('node:path');
const site=path.resolve(__dirname,'../../web/dist/site');
const types={'.css':'text/css','.js':'application/javascript','.svg':'image/svg+xml','.woff2':'font/woff2','.txt':'text/plain','.json':'application/json','.png':'image/png','.webmanifest':'application/manifest+json'};
const enabled=true;
require('node:child_process').execFileSync(process.env.PYTHON||'python3',[path.resolve(__dirname,'../../web/build.py')],{stdio:'inherit'});
module.exports={
 enabled,
 html:enabled?fs.readFileSync(path.join(site,'index.html'),'utf8'):null,
 // Another embedded page (login.html, update.html...).
 page:name=>enabled?fs.readFileSync(path.join(site,name),'utf8'):null,
 // True when the request was an embedded asset and has been answered.
 serve(pathname,res){
  if(!enabled||!pathname.startsWith('/assets/'))return false;
  const file=path.join(site,path.normalize(pathname).replace(/^[/\\]+/,''));
  if(!file.startsWith(site)||!fs.existsSync(file)){res.statusCode=404;res.end();return true}
  res.setHeader('Content-Type',types[path.extname(file)]||'application/octet-stream');
  res.end(fs.readFileSync(file));return true;
 },
};
