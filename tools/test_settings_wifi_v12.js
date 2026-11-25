// Exercise the shipped Settings script with native-select-like DOM and controlled fetches.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert');
const html=fs.readFileSync(process.argv[2],'utf8'),nodes={},requests=[],timers=[];
for(const m of html.matchAll(/id=(?:"([^"]+)"|'([^']+)'|([^\s>]+))/g))nodes[m[1]||m[2]||m[3]]={value:'',checked:false,textContent:'',options:[],add(o){this.options.push(o)}};
nodes.mt.checked=nodes.wc.checked=true;
Object.defineProperty(nodes.networks,'innerHTML',{set(){throw Error('SSID inserted as HTML')}});
let result={networks:[{ssid:'Saved <&" network'},{ssid:'Visible'},{ssid:'İ'.repeat(16)}],busy:false,error:false,available:true,age:0,retry_after:0,omitted:0},reject=false,hang=false;
const sandbox={document:{getElementById:id=>{assert(nodes[id],id);return nodes[id]}},TextEncoder,Option:function(text,value){this.text=text;this.value=value},AbortController,setTimeout:(fn,delay)=>{const t={fn,delay};timers.push(t);return t},clearTimeout:t=>{const i=timers.indexOf(t);if(i>=0)timers.splice(i,1)},
fetch:async(url,args)=>{requests.push({url,args});if(url==='/badge-font')return{ok:false};if(url==='/cfg')return{ok:true,json:async()=>({ssid:'Saved <&" network',pass:'unchanged-password',img:false,id:'0000'})};
 if(url.startsWith('/networks')){if(hang)return new Promise((resolve,reject)=>args.signal.addEventListener('abort',()=>reject(Error('timeout'))));if(reject)throw Error('network');return{ok:true,status:200,json:async()=>result}}return{ok:true,text:async()=>'Saved.'}}};
vm.createContext(sandbox);vm.runInContext(html.split('<script>')[1].split('</script>')[0],sandbox);
const tick=()=>new Promise(r=>setImmediate(r));
(async()=>{await tick();assert.equal(nodes.ssid.value,'Saved <&" network');assert.equal(nodes.networks.value,nodes.ssid.value);assert.equal(nodes.pass.value,'unchanged-password');assert.equal(timers.length,0);
 assert(requests.some(r=>r.url==='/networks'&&r.args.method==='GET'));assert(!requests.some(r=>r.url==='/networks/scan'));
 nodes.networks.value='Visible';nodes.networks.onchange();assert.equal(nodes.ssid.value,'Visible');assert.equal(nodes.pass.value,'unchanged-password');
 nodes.ssid.value='Hidden manual';result={...result,networks:[],age:300,error:true};await sandbox.loadNetworks(false);
 assert.equal(nodes.ssid.value,'Hidden manual');assert.equal(nodes.networks.value,'Hidden manual');assert(nodes.netstatus.textContent.includes('stale'));assert.equal(nodes.pass.value,'unchanged-password');
 reject=true;await sandbox.loadNetworks(true);assert.equal(nodes.ssid.value,'Hidden manual');assert(!nodes.rescan.disabled);reject=false;
 hang=true;const stalled=sandbox.loadNetworks(true);const deadline=timers.find(t=>t.delay===4000);assert(deadline);deadline.fn();await stalled;hang=false;assert(!nodes.rescan.disabled&&nodes.ssid.value==='Hidden manual'&&nodes.pass.value==='unchanged-password');
 result={...result,error:false,busy:true};await sandbox.loadNetworks(true);let polls=0;while(timers.length){const t=timers.shift();assert.equal(t.delay,500);t.fn();await tick();assert(++polls<=15)}assert.equal(polls,15);assert(!nodes.rescan.disabled);
 result={...result,busy:false,error:false,networks:[]};await sandbox.loadNetworks(false);assert(nodes.netstatus.textContent.includes('No visible'));
 result={...result,available:false,error:true};await sandbox.loadNetworks(false);assert(nodes.rescan.disabled&&nodes.netstatus.textContent.includes('Reopen Settings'));result.available=true;
 nodes.ssid.value='İ'.repeat(16);nodes.save.onclick();await tick();const saved=requests.filter(r=>r.url==='/save').at(-1);assert(saved.args.body.includes('ssid\0'+'İ'.repeat(16)+'\0pass\0unchanged-password\0'));
 const before=requests.length;nodes.ssid.value='İ'.repeat(17);nodes.save.onclick();await tick();assert.equal(requests.length,before);assert(nodes.st.textContent.includes('32 bytes'));
 assert(html.includes('<select id=networks')&&html.includes('<input id=ssid'));
 console.log('PASS: shipped native select, saved/hidden draft and password retention, DOM-safe escaped names, empty/stale/fetch failure, bounded explicit scan status checks/no idle polls, full32-byte save and overlong rejection');
})().catch(e=>{console.error(e);process.exitCode=1});
