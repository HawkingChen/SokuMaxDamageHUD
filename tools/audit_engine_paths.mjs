// Optional source-level audit of the externally downloaded cleista engine.
// Node 22; GPL engine and local game archives stay outside the repository.
// Usage: node tools/audit_engine_paths.mjs ENGINE_DIR PATTERN_DIR INVENTORY_JSON OUTPUT_DIR
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';

const [engineDirectory, patternDirectory, inventoryFile, outputDirectory] = process.argv.slice(2);
if (!outputDirectory) throw new Error('Expected ENGINE_DIR PATTERN_DIR INVENTORY_JSON OUTPUT_DIR');
const testVariants=process.argv.includes("--variants");
const engineRoot = path.resolve(engineDirectory), output = path.resolve(outputDirectory);
fs.mkdirSync(output, {recursive:true});
const engineFile = path.join(engineRoot, 'chunk-OBY5JK72.js');
let source = fs.readFileSync(engineFile, 'utf8');
const hash = createHash('sha256').update(source).digest('hex');
if (hash !== '93f49ebae8f9ebb06f2a08622379cf14d0ccc33cf349e073428a8d89d6770734')
  throw new Error('Engine changed: review adapters and exports before using another build');
// Node's bundled Acorn is used only by this optional research tool. Refuse if
// unavailable rather than rewriting JavaScript with an approximate regexp.
const parser = {}, bundled = process.binding('natives')['internal/deps/acorn/acorn/dist/acorn'];
if (!bundled) throw new Error('This audit requires Node 22 with its bundled Acorn parser');
vm.runInNewContext(bundled, {exports:parser,module:{exports:parser}});
const tree = parser.parse(source, {ecmaVersion:'latest',sourceType:'module'});
const edits = [], sites = [];
function walk(node, functionName) {
  if (!node || typeof node !== 'object') return;
  if (node.type === 'FunctionDeclaration') functionName = node.id.name;
  if (node.type === 'AssignmentExpression' && node.operator === '=' &&
      node.left.type === 'MemberExpression' && !node.left.computed &&
      node.left.property.name === 'hp' && node.left.object.type === 'Identifier') {
    const actor = source.slice(node.left.object.start,node.left.object.end);
    const attack = ['kBi','KZ'].includes(functionName) ? 't' :
      ['Ke','PBn','Jrt'].includes(functionName) ? 'i' : 'null';
    sites.push({site:functionName,offset:node.start,expression:source.slice(node.start,node.end)});
    edits.push({start:node.start,end:node.right.start,text:`__sokuMaxAuditHP(${actor},`},
      {start:node.end,end:node.end,text:`,${JSON.stringify(functionName)},n,${attack})`});
  }
  for (const [key,value] of Object.entries(node)) {
    if (key === 'start' || key === 'end') continue;
    if (Array.isArray(value)) value.forEach(child=>walk(child,functionName));
    else if (value && typeof value === 'object') walk(value,functionName);
  }
}
walk(tree,null);
if (sites.length !== 42) throw new Error('HP-write inventory changed');
for (const edit of edits.sort((a,b)=>b.start-a.start))
  source = source.slice(0,edit.start)+edit.text+source.slice(edit.end);
source = source.replace('i.actionId=I4(t),l_i(n,i)',
  'i.actionId=I4(t),globalThis.__sokuMaxAuditAction?.(n,i,t,-1),l_i(n,i)')
  .replace('Y(n,P,c.actionId),P}',
    'Y(n,P,c.actionId),globalThis.__sokuMaxAuditBirth?.(n,P,p),P}')
  .replace('i.actionId=I4(t),D(n,i,a)',
    'i.actionId=I4(t),globalThis.__sokuMaxAuditAction?.(n,i,t,a),D(n,i,a)')
  .replace('a.objects.update(n,i),i.tail',
    'globalThis.__sokuMaxAuditBeginUpdate?.(n,i),a.objects.update(n,i),globalThis.__sokuMaxAuditEndUpdate?.(n,i),i.tail');
source += `
function __sokuMaxAuditHP(actor,value,site,context,attack) {
  const before=actor.hp;actor.hp=value;
  if(actor.id===0||actor.id===1)globalThis.__sokuMaxAuditHP?.({actor,before,after:value,site,context,attack,
    boxAction:attack?k(attack,0x1c8):-1});
  return value;
}
export {Lpt as auditTick,GCi as auditInput};
`;
// Keep relative imports beside the original downloaded engine.
const adapted = path.join(engineRoot, 'sokumax-audit-engine.js');
fs.writeFileSync(adapted,source);
fs.writeFileSync(path.join(output,'hp-write-sites.json'),JSON.stringify({hash,sites},null,2));
const engine = await import(pathToFileURL(adapted));
const assets = await import(pathToFileURL(path.join(engineRoot,'chunk-3FKW4SWO.js')));
const names = 'reimu marisa sakuya alice patchouli youmu remilia yuyuko yukari suika udonge aya komachi iku tenshi sanae chirno meirin utsuho suwako'.split(' ');
const movePatterns = new Map(), cardTables={common:null,byCharacter:new Map()};
for (const [id,name] of [...names.entries(),[-1,'common']]) {
  const p=path.join(patternDirectory,`data/character/${name}/${name}.pat`);
  movePatterns.set(id,assets.g(new Uint8Array(fs.readFileSync(p)),{path:p}));
  const csv=path.join(patternDirectory,`data/csv/${name}/spellcard.cv1`);
  if (fs.existsSync(csv)) {
    const bytes=new Uint8Array(fs.readFileSync(csv));assets.d(bytes,139,113,149);
    const table=assets.p(bytes);
    if(id===-1)cardTables.common=table;else cardTables.byCharacter.set(id,table);
  }
}
const readPattern = name => assets.g(new Uint8Array(fs.readFileSync(path.join(patternDirectory,name))),{path:name});
const data = assets.n({movePatterns,cardTables,
  effectPattern:readPattern('data/effect/effect.pat'),
  weatherPattern:readPattern('data/weather/effect/effect.pat')});
const inventory=JSON.parse(fs.readFileSync(inventoryFile));
const trace=fs.openSync(path.join(output,'simulator-native.tsv'),'w');
const results=fs.openSync(path.join(output,'cases.jsonl'),'w');
let current=null;
const updating=[];
globalThis.__sokuMaxAuditBeginUpdate=(ctx,actor)=>{
  updating.push(actor);
  if(ctx.state.players[actor.owner].characterId===5&&actor.actionId===899&&actor.sequenceId===0&&engine.x(ctx.state.players[actor.owner],2258)>0)
    emit(`R ${actorId(actor)} ${actor.owner+1} ${ctx.state.frame}`);
};
globalThis.__sokuMaxAuditEndUpdate=(ctx,actor)=>{
  if(ctx.state.players[actor.owner].characterId===5&&actor.actionId===899)
    emit(`X ${actor.owner+1} ${ctx.state.frame} ${engine.x(ctx.state.players[actor.owner],2258)}`);
  updating.pop();
};
const continues=(character,from,to)=>character===19&&((from===305&&to===306)||(from===552&&to===550)||(from===553&&to===551));
const actorId = a => a.id===0||a.id===1 ? a.id+1 : 100+a.owner*4294967296+a.handle;
const emit = text => {if(current)fs.writeSync(trace,text+'\n');};
globalThis.__sokuMaxAuditAction=(ctx,a,action,sequence)=>{
  if(current&&a.id===0&&action>=300&&action<690&&action!==current.action&&
      !continues(a.characterId,current.action,action)&&!(action>=650&&action<670&&action-50===current.action))
    current.nextAttackStarted=action;
  if(a.id===0||a.id===1)emit(`A ${a.id+1} ${action} ${ctx.state.frame} ${sequence} ${a.characterId===5?engine.x(a,2258):0}`);
};
globalThis.__sokuMaxAuditBirth=(ctx,a,creator)=>{
  const caller=updating.at(-1);
  if((creator.id===0||creator.id===1)&&caller&&caller.owner===a.owner)creator=caller;
  emit(
  `B ${actorId(a)} ${a.owner+1} ${actorId(creator)} ${a.parent?100+a.parent.manager*4294967296+a.parent.handle:0} ${a.actionId} ${ctx.state.frame}`);
};
globalThis.__sokuMaxAuditHP=({actor,before,after,site,context,attack,boxAction})=>{
  if(!current||before===after)return;
  current.writes.push({frame:context?.state?.frame,side:actor.id,before,after,site});
  if(before>after&&attack&&['kBi','Ke','PBn','Jrt'].includes(site))
    emit(`H ${actorId(attack)} ${(attack.owner??attack.id)+1} ${(attack.ally??attack.id)+1} ${actor.id+1} ${attack.actionId} ${before-after} ${context.state.frame} ${boxAction??-1}`);
};
const totals={cases:0,damageObserved:0,engineErrors:0};
for(const character of inventory.characters) {
  let base=engine.La({character1:character.characterId,character2:0,stageId:0,seed:1},data);
  for(let frame=0;frame<500;frame++)base=engine.Db(base,0,0,data);
  const seen=new Set();
  for(const move of character.moves) {
    const key=testVariants?move.slug:move.action;
    if(!['normal','skill','spell'].includes(move.kind)||seen.has(key))continue;
    seen.add(key);
    for(const distance of [60,300]) {
      const state=structuredClone(base), player=state.players[0], victim=state.players[1];
      player.positionX=500;victim.positionX=500+distance;
      const air=move.notation?.toLowerCase().startsWith('j');
      if(air){player.positionY=120;player.airborne=true;}
      engine.nb(player);
      if(move.card>=100&&move.card<200)engine.mb(player,move.card-100,4);
      player.hand.cards=Array.from({length:5},()=>({id:move.card??200,cost:1}));
      engine.C(player,engine.da,5);
      const diagnostics=[];
      const options={diagnostics:{report(d){if(diagnostics.length<30)diagnostics.push(d);}}};
      current={character:character.character,action:move.action,notation:move.notation,distance,air,
        forcedAction:true,skillLevel:4,chargeVariantTested:testVariants&&move.notation?.includes("["),nativeVerified:false,writes:[],diagnostics};
      emit(`C ${character.character} ${move.action} ${distance}`);
      const charged=testVariants?/\[([ABC])\]/.exec(move.notation??''):null;
      const heldWord=charged?({A:16,B:32,C:64}[charged[1]]):0;
      try {
        if(heldWord)engine.auditInput(player,heldWord,null,data,engine.Eb(state,data,options));
        engine.sa(engine.Eb(state,data,options),player,move.action);
        current.entryAction=player.actionId;
        current.entryRejected=player.actionId!==move.action&&!continues(character.characterId,move.action,player.actionId);
        for(let frame=0;frame<900;frame++) {
          const word=!current.entryRejected&&frame<60?heldWord:0;
          if(heldWord)engine.auditInput(player,word,null,data,engine.Eb(state,data,options));
          engine.auditTick(state,data,true,[word,0],options);state.frame++;
          if(current.nextAttackStarted)break;
        }
        current.opponentDamage=current.writes.filter(w=>w.side===1&&w.before>w.after)
          .reduce((sum,w)=>sum+w.before-w.after,0);
        current.selfDamage=current.writes.filter(w=>w.side===0&&w.before>w.after)
          .reduce((sum,w)=>sum+w.before-w.after,0);
        current.status=current.entryRejected?'entry-rejected':current.opponentDamage||current.selfDamage?'damage-observed':'no-damage-observed';
        if(current.opponentDamage||current.selfDamage)totals.damageObserved++;
      }catch(error){current.status='engine-error';current.error=error.message;totals.engineErrors++;}
      fs.writeSync(results,JSON.stringify(current)+'\n');current=null;updating.length=0;totals.cases++;
    }
  }
  console.log(character.character,JSON.stringify(totals));
}
fs.closeSync(trace);fs.closeSync(results);
fs.writeFileSync(path.join(output,'summary.json'),JSON.stringify({engineHash:hash,totals,
  limitations:['Forced action entry, not command-input validation',testVariants?'Charged variants: fixed 60-frame button hold, not exhaustive release timing':'Unique action IDs only; charged variants not independently exercised',
    'One seed, two distances, max skill level, no weather or guard','Character updates and collision steps; not full practice UI or native execution',
    'Charged scenarios stop when a separate attack starts; follow-ups have their own cases',
    'Rejected forced entries are not exercised moves',
    'Observed HP loss is not an authoritative native damage total','No-damage and diagnostics require review; they are not passes']},null,2));
