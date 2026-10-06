// The portal the smoke checks render: the SD portal pack (WEB_PORTAL.md),
// built here with `python3 Atlas/web/build.py` into dist/site/ when missing,
// whose /assets/ files are served the way Atlas serves them from the card.
// (Atlas's flash keeps only a small install page since 2026-10-02.)
const fs=require('node:fs'),path=require('node:path');
const site=path.resolve(__dirname,'../../web/dist/site');
const types={'.css':'text/css','.js':'application/javascript','.svg':'image/svg+xml','.woff2':'font/woff2','.txt':'text/plain','.json':'application/json','.png':'image/png','.webmanifest':'application/manifest+json'};
const enabled=true;
if(!fs.existsSync(path.join(site,'index.html')))require('node:child_process').execFileSync(process.env.PYTHON||'python3',[path.resolve(__dirname,'../../web/build.py')],{stdio:'inherit'});
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
