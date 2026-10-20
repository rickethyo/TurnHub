export const TYPES = ['investigator', 'skill', 'condition', 'relic', 'elder', 'episode'];
export const LABELS = {investigator:'Investigators',skill:'Skills',condition:'Conditions',relic:'Relics',elder:'Elder Ones',episode:'Episodes'};
export const slug = value => value.normalize('NFKD').replace(/[\u0300-\u036f]/g,'').toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'');
const urlHost = value => {try{return new URL(value).hostname;}catch{return '';}};
export function blankRecord(type, name='Untitled') {
  return {id:`${type}:${slug(name)}`,type,name,collection:'Custom',sourceUrl:'',aliases:[],imageUrl:'',imageId:'',referenceImage:'',
    rules:'',transformedRules:'',rulesSourceUrl:'',reviewNote:'',verified:false,transformedVerified:false,skillIds:[],levels:type==='skill'?['','','','']:[],preset:false,filterable:false};
}
export function validate(doc) {
  const issues=[];
  const add=(severity,record,message,field='')=>issues.push({severity,id:record?.id||'',name:record?.name||'Database',field,message});
  if(!doc || doc.schema!==1 || !Array.isArray(doc.records) || doc.records.length>5000) return [{severity:'error',id:'',name:'Database',field:'schema',message:'Expected schema 1 and at most 5,000 records.'}];
  if(typeof doc.title!=='string'||!doc.title.trim()||doc.title.length>100) add('error',null,'A database title is required.','title');
  if(typeof doc.version!=='string'||!/^\d+\.\d+\.\d+(?:-[a-z0-9.-]+)?$/.test(doc.version)) add('error',null,'Use a version such as 1.0.0.','version');
  const ids=new Set(),names=new Set();
  for(const r of doc.records) {
    if(!r || typeof r!=='object' || !TYPES.includes(r.type)) {add('error',r,'Unknown record type.','type');continue;}
    if(typeof r.id!=='string'||!new RegExp(`^${r.type}:[a-z0-9][a-z0-9-]{0,100}$`).test(r.id)||ids.has(r.id)) add('error',r,'Record ID must be unique and match its type.','id');
    ids.add(r.id);
    if(typeof r.name!=='string'||!r.name.trim()||r.name.length>(r.type==='skill'?60:r.type==='investigator'?32:r.type==='elder'||r.type==='episode'?40:80)) add('error',r,'Name is missing or exceeds the app limit.','name');
    const nk=r.type+':'+String(r.name).trim().toLowerCase();if(names.has(nk)) add('error',r,'Duplicate name in this content type.','name');names.add(nk);
    for(const f of ['collection','sourceUrl','imageUrl','referenceImage','rules','transformedRules','reviewNote']) {
      if(typeof r[f]!=='string' || /[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(r[f])) add('error',r,`${f} must be text without control characters.`,f);
    }
    if(String(r.collection).length>120||!String(r.collection).trim()) add('error',r,'Choose a collection.','collection');
    for(const f of ['sourceUrl','imageUrl','rulesSourceUrl']) if(r[f]) {try {const u=new URL(r[f]);if(u.protocol!=='https:'||u.username||u.password) throw Error();}catch {add('error',r,'Use a complete HTTPS reference URL.',f);}}
    if((r.rules?.length||0)>2000||(r.transformedRules?.length||0)>2000||(r.reviewNote?.length||0)>2000) add('error',r,'Card rules and notes are limited to 2,000 characters.','rules');
    if(!Array.isArray(r.aliases)||r.aliases.length>20||r.aliases.some(x=>typeof x!=='string'||x.length>100)) add('error',r,'Aliases must be a list of at most 20 short names.','aliases');
    for(const f of ['verified','transformedVerified','preset','filterable']) if(typeof r[f]!=='boolean') add('error',r,`${f} must be true or false.`,f);
    if(!Array.isArray(r.levels)||r.levels.length>4||r.levels.some(x=>typeof x!=='string'||x.length>600)) add('error',r,'Skills support four level descriptions, up to 600 characters each.','levels');
    if(!Array.isArray(r.skillIds)||r.skillIds.length>5||r.skillIds.some(x=>typeof x!=='string')) add('error',r,'Invalid skill reference list.','skillIds');
    if(r.type==='skill'&&r.filterable&&!r.preset) add('error',r,'A common skill filter must also be available as a preset.','preset');
    if(r.type==='investigator'&&Array.isArray(r.skillIds)&&(r.skillIds.length>3||new Set(r.skillIds).size!==r.skillIds.length)) add('error',r,'Choose up to three distinct investigator skills.','skillIds');
    if(r.referenceImage && !/^[a-f0-9-]{36}$/.test(r.referenceImage)) add('error',r,'Invalid uploaded reference image.','referenceImage');
    if(['condition','relic'].includes(r.type) && r.imageUrl && urlHost(r.imageUrl)!=='cf.geekdo-images.com') add('error',r,'Android card artwork currently requires a geekdo image reference. Upload other photographs as review references.','imageUrl');
    if(['condition','relic'].includes(r.type) && r.imageId && !/^[0-9]{1,20}$/.test(r.imageId)) add('error',r,'Card image ID must be numeric.','imageId');
    if(['condition','relic'].includes(r.type) && Boolean(r.imageId)!==Boolean(r.imageUrl)) add('error',r,'Card artwork needs both its image ID and image URL.','imageId');
    if(!r.sourceUrl) add(r.verified?'error':'warning',r,'Add a source reference.','sourceUrl');
    if(!r.verified) add('warning',r,'Not yet checked against the physical reference.','verified');
    if(['condition','relic'].includes(r.type) && !String(r.rules||'').trim()) add(r.verified?'error':'warning',r,'Base rules are missing.','rules');
    if(r.type==='relic'&&!String(r.transformedRules||'').trim()) add(r.transformedVerified?'error':'warning',r,'Transformed rules are missing.','transformedRules');
    if(r.type==='relic'&&!r.transformedVerified) add('warning',r,'Transformed face is not verified.','transformedVerified');
    if(r.type==='skill' && (!Array.isArray(r.levels)||r.levels.length!==4||r.levels.some(x=>typeof x!=='string'||!x.trim()))) add(r.verified?'error':'warning',r,'Complete all four skill levels.','levels');
    if(r.type==='investigator'&&r.skillIds?.length!==3) add(r.verified?'error':'warning',r,'Three skill references are needed for a complete dashboard.','skillIds');
    if(['investigator','condition','relic'].includes(r.type)&&!r.imageUrl&&!r.referenceImage) add('warning',r,'No reference image attached.','imageUrl');
  }
  const map=new Map(doc.records.filter(Boolean).map(r=>[r.id,r]));
  for(const r of doc.records.filter(Boolean)) for(const id of Array.isArray(r.skillIds)?r.skillIds:[]) if(map.get(id)?.type!=='skill') add('error',r,`Unknown skill reference: ${id}`,'skillIds');
  const images=new Map();for(const r of doc.records.filter(Boolean)) if(r.imageId&&r.imageUrl){if(images.has(r.imageId)&&images.get(r.imageId)!==r.imageUrl)add('error',r,'A shared image ID must refer to the same image URL.','imageId');images.set(r.imageId,r.imageUrl);}
  return issues;
}
const q = value => JSON.stringify(String(value)).replace(/\$/g,'\\$').replace(/\\f/g,'\\u000c');
const list = values => `listOf(${values.map(q).join(', ')})`;
export function generateKotlin(doc) {
  const errors=validate(doc).filter(x=>x.severity==='error');if(errors.length) throw Error(errors.map(x=>`${x.name}: ${x.message}`).join('\n'));
  const by=t=>doc.records.filter(r=>r.type===t), skills=new Map(by('skill').map(r=>[r.id,r]));
  const sk = r => `InvestigatorSkill(${q(r.name)}, ${list(r.levels)})`;
  const groups = (type,kind,field) => [...Map.groupBy(by(type),r=>r.collection)].map(([name,rs])=>`        ${kind}(${q(name)}, ${list(rs.map(r=>r.name))})`).join(',\n');
  const map = rows => `mapOf(\n${rows.join(',\n')}\n    )`;
  const cards=[...by('condition'),...by('relic')];
  const images=new Map(cards.filter(r=>r.imageId&&r.imageUrl).map(r=>[r.imageId,r.imageUrl]));
  return `// Generated by TurnHub Database Forge. Edit data/death-may-die/catalogue.json.\n// Database version ${doc.version}; editor verification never bypasses in-game card review.\npackage com.turnhub.android.domain\n\ninternal object DmdContent {\n`+
    `    val boxes: List<InvestigatorBox> = listOf(\n${groups('investigator','InvestigatorBox')}\n    )\n`+
    `    val investigatorArt: Map<String, String> = ${map(by('investigator').filter(r=>r.imageUrl).map(r=>`        ${q(r.name)} to ${q(r.imageUrl)}`))}\n`+
    `    val commonSkills: Map<String, List<String>> = ${map(by('investigator').filter(r=>r.skillIds.filter(id=>skills.get(id)?.filterable&&skills.get(id)?.preset).length===2).map(r=>`        ${q(r.name)} to ${list(r.skillIds.filter(id=>skills.get(id)?.filterable).map(id=>skills.get(id).name))}`))}\n`+
    `    val knownSkills: Map<String, List<InvestigatorSkill>> = ${map(by('investigator').filter(r=>r.skillIds.length===3&&r.skillIds.every(id=>skills.has(id))).map(r=>`        ${q(r.name)} to listOf(${r.skillIds.map(id=>sk(skills.get(id))).join(', ')})`))}\n`+
    `    val skillPresets: List<InvestigatorSkill> = listOf(${by('skill').filter(r=>r.preset).map(sk).join(',\n        ')})\n`+
    `    val skillFilters: List<String> = ${list(by('skill').filter(r=>r.filterable).map(r=>r.name))}\n`+
    `    val cards: List<InvestigatorCardPreset> = listOf(\n${cards.map(r=>`        InvestigatorCardPreset(${q(r.id)}, InvestigatorCardSlot.${r.type==='relic'?'RELIC':'SANITY_CONDITION'}, ${q(r.name)}, ${q(r.collection)}, ${q(r.sourceUrl)}, ${r.imageId?q(r.imageId):'null'}, ${list(r.aliases)})`).join(',\n')}\n    )\n`+
    `    val cardImages: Map<String, String> = ${map([...images].map(([id,url])=>`        ${q(id)} to ${q(url)}`))}\n`+
    `    val cardDrafts: Map<String, InvestigatorRulesDraft> = ${map(cards.filter(r=>r.rules||r.transformedRules).map(r=>`        ${q(r.id)} to InvestigatorRulesDraft(${q(r.rules)}, ${q(r.transformedRules)}, ${q(r.rulesSourceUrl||r.sourceUrl)}, ${q(r.reviewNote)})`))}\n`+
    `    val elderOnes: List<StoryGroup> = listOf(\n${groups('elder','StoryGroup')}\n    )\n`+
    `    val episodes: List<StoryGroup> = listOf(\n${groups('episode','StoryGroup')}\n    )\n}\n`;
}
export function filesForBuild(doc) {
  const kotlin=generateKotlin(doc),issues=validate(doc);
  return {'catalogue.json':JSON.stringify(doc,null,2)+'\n','android/DmdContent.generated.kt':kotlin,
    'validation.json':JSON.stringify({schema:1,records:doc.records.length,errors:issues.filter(i=>i.severity==='error').length,warnings:issues.filter(i=>i.severity==='warning').length,issues},null,2)+'\n',
    'README.md':'# TurnHub Death May Die catalogue\n\nImport catalogue.json with the TurnHub repository tool:\n\n    node tools/dmd-forge/build.mjs --input /path/to/catalogue.json --import\n\nReview the diff and run the Android tests before distribution. Reference photos are authoring materials; they are not automatically bundled into the app. Existing in-game verification and saved-match references remain separate.\n'};
}
