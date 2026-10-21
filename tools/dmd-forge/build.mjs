import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {generateKotlin,validate} from './catalogue.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const args=process.argv.slice(2),allowed=new Set(['--input','--check','--import','--validate-only']);
for(let i=0;i<args.length;i++) {if(!allowed.has(args[i])) throw Error(`Unknown argument ${args[i]}`);if(args[i]==='--input')i++;}
const canonical=path.join(root,'data/death-may-die/catalogue.json');
const index=args.indexOf('--input'),input=index<0?canonical:path.resolve(args[index+1]);
const doc=JSON.parse(fs.readFileSync(input,'utf8')),issues=validate(doc),errors=issues.filter(i=>i.severity==='error');
if(errors.length){console.error(errors);process.exit(1);}
if(args.includes('--validate-only')){console.log(`Valid catalogue: ${doc.records.length} records; ${issues.length} review warnings.`);process.exit(0);}
const text=generateKotlin(doc),output=path.join(root,'Android/app/src/main/java/com/turnhub/android/domain/DmdContent.generated.kt');
if(args.includes('--check')){if(!fs.existsSync(output)||fs.readFileSync(output,'utf8').replace(/\r/g,'')!==text){console.error('Generated DMD catalogue is stale. Run node tools/dmd-forge/build.mjs.');process.exit(1);}}
else {if(args.includes('--import'))fs.writeFileSync(canonical,JSON.stringify(doc,null,2)+'\n');else if(input!==canonical)throw Error('Use --import to replace the source catalogue, or --validate-only to inspect an external file.');fs.writeFileSync(output,text);}
console.log(`DMD catalogue ${args.includes('--check')?'matches':'generated'}: ${doc.records.length} records, ${issues.length} review warnings.`);
