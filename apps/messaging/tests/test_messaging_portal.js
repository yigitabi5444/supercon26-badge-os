// Exercise the shipped page's actual script with a small DOM/network model (no ESP32/browser claimed).
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const app = path.resolve(__dirname,'..');
const vm = require('node:vm');
const crypto = require('node:crypto').webcrypto;
const html = fs.readFileSync(path.join(app,'message_page.html'),'utf8');
const header = fs.readFileSync(path.join(app,'message_page.h'),'utf8');
const generated = '// Generated from message_page.html; see tests/test_messaging_portal.js.\nstatic const char message_page[] =\n' + html.match(/[^\n]*\n|[^\n]+$/g).map(line=>'    '+JSON.stringify(line)+'\n').join('')+'    ;\n';
if(process.argv.includes('--write'))fs.writeFileSync(path.join(app,'message_page.h'),generated);
else assert.equal(header,generated,'embedded page must match source HTML (regenerate with --write)');
const served = fs.readFileSync(path.join(app,'message_page.h'),'utf8').split('\n').filter(line=>line.trim().startsWith('"')).map(line=>JSON.parse(line.trim())).join('');
assert.equal(served,html,'shipped C string must match HTML');
const script = served.match(/<script>([\s\S]*)<\/script>/)[1];
class Element {
 constructor(){this.children=[];this.value='';this.textContent='';this.hidden=false;this.disabled=false;this.checked=false}
 append(...children){this.children.push(...children)}replaceChildren(...children){this.children=children}
 get lastElementChild(){return this.children.at(-1)}scrollIntoView(){}
}
const ids=['capacity','sharing','history','full','relay','refresh','new','back','threads','view','heading','messages','forget','form','compose','body','send','discard','status'];
let messages=[],selected=null,relay=true,fail=false,saveButDrop=false,calls=[],savedWrites=0,device='020000000001';
const session=new Map();
function boot(){
 const dom=Object.fromEntries(ids.map(id=>[id,new Element()]));dom.status.textContent='Loading...';
 const context={crypto,AbortController,setTimeout,clearTimeout,confirm:()=>true,Date,Uint8Array,JSON,
  sessionStorage:{getItem:k=>session.get(k)||null,setItem:(k,v)=>session.set(k,v),removeItem:k=>session.delete(k)},
  document:{getElementById:id=>dom[id],createElement:()=>new Element()},
  fetch:async(path,opt)=>{
   calls.push({path,opt});assert.equal(opt.headers['X-Msg-Key'],'@KEY@');
   if(fail)throw Error('network unavailable');
   if(path==='/api/state')return{ok:true,json:async()=>({device,limit:8,relay,selected,messages:structuredClone(messages)})};
   const fields=opt.body.split('\0');assert.equal(fields.pop(),'');
   if(fields[0]==='send'){
    const [,id,thread,title,body,seconds]=fields;
    const old=messages.find(m=>m.id===id);
    if(old&&(old.thread!==thread||old.title!==title||old.body!==body||old.seconds!==seconds||old.clock!==(seconds==='0'?0:1)||old.sender!==device))return{ok:false,text:async()=>'Message ID already exists with different content or sender.'};
    if(!old){messages.push({id,thread,title,body,seconds,clock:seconds==='0'?0:1,name:'Guest',sender:'020000000001',read:true});savedWrites++}selected=thread;
    if(saveButDrop){saveButDrop=false;throw Error('response lost after commit')}
   }else if(fields[0]==='open'){selected=fields[1];for(const m of messages)if(m.thread===selected)m.read=true}
   else if(fields[0]==='relay')relay=fields[1]==='1';
   else if(fields[0]==='forget'){messages=messages.filter(m=>m.thread!==fields[1]);selected=null}
   else throw Error('unexpected action');
   return{ok:true,text:async()=>'Stored on this badge.'};
  }};
 vm.createContext(context);vm.runInContext(script,context);return{dom,context};
}
const settle=()=>new Promise(r=>setImmediate(r));
async function main(){
 let {dom}=boot();await settle();assert.match(dom.threads.textContent,/No messages/);assert.equal(relay,true);assert.equal(dom.history.hidden,true);assert.equal(dom.form.hidden,false);assert.equal(dom.compose.textContent,'Message');assert.equal(dom.send.textContent,'Send');
 dom.body.value='  Meet up\n<script>alert(1)</script>\nHello & "friends"';await dom.form.onsubmit({preventDefault(){}});
 assert.equal(messages.length,1);const thread=messages[0].thread;assert.equal(thread,messages[0].id);assert.equal(messages[0].title,'Meet up <script>alert(1)</script');assert.equal(messages[0].title.length,32);assert.equal(dom.compose.textContent,'Reply');assert.match(dom.status.textContent,/Saved on this badge/);
 assert.equal(dom.messages.children[0].children[1].textContent,messages[0].body);assert.equal(dom.messages.children[0].children[1].innerHTML,undefined,'message renders as text');
 dom.body.value='A reply';await dom.form.onsubmit({preventDefault(){}});assert.equal(messages[1].thread,thread,'reply keeps thread');
 dom.body.value='A retried reply';saveButDrop=true;await dom.form.onsubmit({preventDefault(){}});
 assert.equal(messages.length,3);const id=messages[2].id;assert.match(dom.status.textContent,/not confirmed/);assert.equal(dom.body.disabled,true);
 await dom.form.onsubmit({preventDefault(){}});assert.equal(messages.length,3);assert.equal(messages[2].id,id);assert.equal(savedWrites,3);
 dom.body.value='Saved while response dropped';saveButDrop=true;await dom.form.onsubmit({preventDefault(){}});assert.equal(messages.length,4);
 ({dom}=boot());await settle();assert.match(dom.status.textContent,/Saved on this badge/);assert.equal(session.size,0,'reconnect resolves already-stored pending ID');
 dom.body.value='Disconnected unsaved draft';fail=true;await dom.form.onsubmit({preventDefault(){}});assert.equal(messages.length,4);assert.equal(session.size,1);
 fail=false;device='020000000002';({dom}=boot());await settle();assert.equal(dom.discard.hidden,true,'another badge must not restore this badge retry draft');device='020000000001';({dom}=boot());await settle();assert.match(dom.status.textContent,/Pending save restored/);await dom.form.onsubmit({preventDefault(){}});assert.equal(messages.length,5);assert.equal(messages[4].thread,thread);
 dom.new.onclick();dom.body.value='Unicode \u2603';let writes=savedWrites;await dom.form.onsubmit({preventDefault(){}});assert.equal(savedWrites,writes);assert.match(dom.status.textContent,/plain characters/);assert(!/Wi-Fi|Retry/.test(dom.status.textContent));
 dom.body.value='No time';
 // Test the real script with an unavailable phone clock.
 let ctx=boot();await settle();vm.runInContext('Date=class extends Date {static now(){return 0}}',ctx.context);ctx.dom.new.onclick();ctx.dom.body.value='No clock';await ctx.dom.form.onsubmit({preventDefault(){}});
 assert.equal(messages.at(-1).clock,0);assert.match(ctx.dom.messages.children.at(-1).children[0].children[0].textContent,/time unknown/);
 ({dom}=boot());await settle();dom.back.onclick();messages[0].read=false;await dom.refresh.onclick();assert(dom.threads.children.some(x=>x.textContent.startsWith('Unread: Meet up')));
 await dom.threads.children.find(x=>x.textContent.includes('Meet up')).onclick();assert.equal(messages[0].read,true);assert.equal(dom.form.hidden,false);assert.equal(dom.compose.textContent,'Reply');assert.equal(dom.history.hidden,true);assert.equal(dom.new.hidden,false);
 dom.relay.checked=false;await dom.relay.onchange();assert.equal(relay,false);assert.match(dom.sharing.textContent,/off/);dom.relay.checked=true;await dom.relay.onchange();assert.equal(relay,true);assert.match(dom.status.textContent,/resumes after Home/);
 await dom.forget.onclick();assert(!messages.some(m=>m.thread===thread));
 // Full state is global and visible; no action can silently evict the eight retained messages.
 while(messages.length<8)messages.push({id:'f'+messages.length,thread:'other'+messages.length,title:'Other thread',body:'Retained',seconds:'0',clock:false,read:false});
 await dom.refresh.onclick();assert.match(dom.capacity.textContent,/8 \/ 8.*FULL/);assert(dom.send.disabled);assert.equal(dom.full.hidden,false);dom.new.onclick();assert.equal(dom.form.hidden,false);
 let fullWrites=savedWrites;dom.body.value='Over limit';await dom.form.onsubmit({preventDefault(){}});assert.equal(savedWrites,fullWrites);assert.equal(messages.length,8);assert.match(dom.status.textContent,/full/);
 assert(!html.includes('innerHTML'));assert(!html.includes("fetch('/cfg"));
 assert(!/id=title\b|Subject/.test(html),'composer has no subject field');
 console.log('PASS actual page script: single-field automatic label, history/reply context, empty, compose, safe text, read/unread, same-thread reply, dropped response, idempotent retry, reload/rejoin, unknown clock, relay and local delete');
 console.log('NOTE DOM/network model only; AP join, QR scanning and browser layout require hardware/manual checks.');
}
async function pendingConfirmationTests(){
 const changes={body:m=>m.body='Other relayed content',title:m=>m.title='Other subject',thread:m=>m.thread=m.thread[0]==='f'?'e'.repeat(32):'f'.repeat(32),seconds:m=>m.seconds=String(Number(m.seconds)+1),clock:m=>m.clock=0,sender:m=>m.sender='030000000003'};
 for(const [field,change] of Object.entries(changes)){
  messages=[];selected=null;session.clear();savedWrites=0;fail=false;device='020000000001';
  let {dom}=boot();await settle();dom.body.value='Keep this intended draft';fail=true;
  await dom.form.onsubmit({preventDefault(){}});fail=false;
  const saved=session.get('badge-message-pending:'+device),p=JSON.parse(saved);
  assert.equal(p.sender,device,'capture actual local device from state before send');assert.equal(p.clock,1);
  const record={...p,name:'Unverified relay name',read:false};change(record);messages.push(record);
  ({dom}=boot());await settle();
  assert.equal(session.get('badge-message-pending:'+device),saved,field+' conflict must retain the exact retry draft');
  assert.equal(dom.body.value,p.body);assert.equal(dom.discard.hidden,false);assert.equal(dom.body.disabled,true);assert.match(dom.status.textContent,/conflict.*Draft kept/i);assert(!/Saved on this badge/.test(dom.status.textContent));
  await dom.refresh.onclick();assert.match(dom.status.textContent,/conflict.*Draft kept/i,'Refresh must not hide conflict');
  dom.relay.checked=true;await dom.relay.onchange();assert.match(dom.status.textContent,/conflict.*Draft kept/i,'relay feedback must not hide conflict');
  await dom.form.onsubmit({preventDefault(){}});assert.equal(session.get('badge-message-pending:'+device),saved,'server rejects mismatched retry without dropping draft');assert.equal(dom.body.value,p.body);
 }
 // Old retry drafts have no captured sender/clock. Keep them rather than infer missing metadata.
 messages=[];selected=null;session.clear();fail=false;let {dom}=boot();await settle();dom.body.value='Legacy draft';fail=true;await dom.form.onsubmit({preventDefault(){}});fail=false;
 const p=JSON.parse(session.get('badge-message-pending:'+device));messages.push({...p,name:'Unverified',read:false});p.title='Legacy subject';messages.at(-1).title=p.title;delete p.sender;delete p.clock;session.set('badge-message-pending:'+device,JSON.stringify(p));
 ({dom}=boot());await settle();assert.equal(session.size,1);assert.equal(dom.body.value,p.body);assert.match(dom.status.textContent,/conflict.*Draft kept/i);
 // The explicit retry still validates all submitted fields on the badge and can confirm an old valid draft.
 await dom.form.onsubmit({preventDefault(){}});assert.equal(session.size,0);assert.equal(messages.at(-1).title,'Legacy subject');assert.match(dom.status.textContent,/Saved on this badge/);
 // Exact matching unknown-time metadata also resolves a lost response, without inventing a timestamp.
 messages=[];selected=null;session.clear();let ctx=boot();await settle();vm.runInContext('Date=class extends Date {static now(){return 0}}',ctx.context);ctx.dom.body.value='Unknown-time retry';saveButDrop=true;await ctx.dom.form.onsubmit({preventDefault(){}});
 ({dom}=boot());await settle();assert.equal(session.size,0);assert.equal(dom.body.value,'');assert.match(dom.status.textContent,/Saved on this badge/);assert.equal(messages[0].clock,0);assert.equal(messages[0].seconds,'0');
 console.log('PASS shipped page: conflicting body/title/thread/time/clock/local sender retain drafts and conflict feedback; legacy drafts retained; exact known/unknown-time retries confirm');
}
async function contextTests(){
 const a='1'.repeat(32),b='2'.repeat(32);
 function reset(){messages=[{id:a,thread:a,title:'First conversation',body:'First message',seconds:'0',clock:0,name:'Guest',sender:'020000000001',read:true},{id:b,thread:b,title:'Second conversation',body:'Second message',seconds:'0',clock:0,name:'Visitor',sender:'030000000003',read:false}];selected=a;device='020000000001';session.clear();fail=false;saveButDrop=false;}
 reset();let ctx=boot();await settle();assert.equal(vm.runInContext('thread',ctx.context),a);
 selected=b;await ctx.dom.refresh.onclick();assert.equal(vm.runInContext('thread',ctx.context),a,'an existing empty composer does not silently follow badge selection');
 ctx.dom.body.value='Keep this unsent reply';await ctx.dom.refresh.onclick();assert.equal(ctx.dom.body.value,'Keep this unsent reply');assert.equal(vm.runInContext('thread',ctx.context),a);
 await ctx.dom.form.onsubmit({preventDefault(){}});assert.equal(messages.at(-1).thread,a,'send retains the visible original conversation');
 selected=b;ctx=boot();await settle();assert.equal(vm.runInContext('thread',ctx.context),b,'explicit page reopen adopts the badge-selected context');
 reset();ctx=boot();await settle();ctx.dom.body.value='Retry in original conversation';fail=true;await ctx.dom.form.onsubmit({preventDefault(){}});fail=false;
 let saved=session.get('badge-message-pending:'+device);selected=b;await ctx.dom.refresh.onclick();assert.equal(session.get('badge-message-pending:'+device),saved);assert.equal(vm.runInContext('thread',ctx.context),a);
 ctx=boot();await settle();assert.equal(vm.runInContext('thread',ctx.context),a,'pending retry outranks a newly selected badge context');assert.equal(ctx.dom.body.value,'Retry in original conversation');
 await ctx.dom.form.onsubmit({preventDefault(){}});assert.equal(messages.at(-1).thread,a);assert.equal(session.size,0);
 reset();ctx=boot();await settle();ctx.dom.body.value='Keep conflicting retry';fail=true;await ctx.dom.form.onsubmit({preventDefault(){}});fail=false;
 saved=session.get('badge-message-pending:'+device);const pending=JSON.parse(saved);messages.push({...pending,body:'Different stored content',name:'Guest',read:false});selected=b;
 ctx=boot();await settle();assert.equal(vm.runInContext('thread',ctx.context),a);assert.equal(session.get('badge-message-pending:'+device),saved);assert.equal(ctx.dom.body.value,'Keep conflicting retry');assert.match(ctx.dom.status.textContent,/conflict.*Draft kept/i);
 await ctx.dom.refresh.onclick();assert.equal(session.get('badge-message-pending:'+device),saved);assert.match(ctx.dom.status.textContent,/conflict.*Draft kept/i);
 reset();ctx=boot();await settle();ctx.dom.body.value='Saved before response dropped';saveButDrop=true;await ctx.dom.form.onsubmit({preventDefault(){}});selected=b;
 ctx=boot();await settle();assert.equal(session.size,0);assert.equal(vm.runInContext('thread',ctx.context),a,'exact retry confirmation retains its original conversation despite badge selection');
 assert(!html.includes('Done'),'phone copy points to Home to resume sharing');
 console.log('PASS shipped page context: badge selection cannot retarget empty/typed/current drafts; explicit reopen adopts selection; pending/conflicting/exact retries retain original thread');
}
async function currentTimeTests(){
 messages=[];selected=null;session.clear();fail=false;saveButDrop=false;device='020000000001';calls=[];
 const ctx=boot();await settle();
 vm.runInContext('Date=class extends Date {static now(){return 1794157500000}}',ctx.context);
 let count=calls.length;await ctx.dom.refresh.onclick();assert.equal(calls.length,count+1,'time piggybacks on the existing state request');
 assert.equal(calls.at(-1).opt.headers['X-Msg-Time'],'1794157500');
 ctx.dom.body.value='Keep the original send time';fail=true;await ctx.dom.form.onsubmit({preventDefault(){}});fail=false;
 const saved=session.get('badge-message-pending:'+device),pending=JSON.parse(saved);assert.equal(pending.seconds,'1794157500');
 vm.runInContext('Date=class extends Date {static now(){return 1794161400000}}',ctx.context);
 count=calls.length;await ctx.dom.refresh.onclick();assert.equal(calls.length,count+1);
 assert.equal(calls.at(-1).opt.headers['X-Msg-Time'],'1794161400','state uses fresh phone time after 65 minutes');
 assert.equal(session.get('badge-message-pending:'+device),saved,'fresh current time never rewrites frozen retry metadata');
 count=calls.length;await ctx.dom.form.onsubmit({preventDefault(){}});assert.equal(calls.length,count+2,'retry uses only its existing action and state request');
 const action=calls.at(-2);assert.equal(action.path,'/api/action');assert.equal(action.opt.headers['X-Msg-Time'],undefined);
 assert.equal(action.opt.body.split('\0')[5],pending.seconds);assert.equal(messages[0].seconds,pending.seconds);assert.equal(messages[0].id,pending.id);
 assert.equal(calls.at(-1).opt.headers['X-Msg-Time'],'1794161400');assert.equal(session.size,0);
 vm.runInContext('Date=class extends Date {static now(){return 0}}',ctx.context);await ctx.dom.refresh.onclick();assert.equal(calls.at(-1).opt.headers['X-Msg-Time'],'0');
 assert(!html.includes('setInterval'),'no polling timer added');
 console.log('PASS shipped page time: fresh existing-state header, no extra requests, frozen historical retry time and unknown phone clock');
}
main().then(pendingConfirmationTests).then(contextTests).then(currentTimeTests).catch(e=>{console.error(e);process.exitCode=1});
