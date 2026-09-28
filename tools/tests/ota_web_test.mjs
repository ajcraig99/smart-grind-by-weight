// Exercise the production update status rendering, not a copy of its logic.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const page=readFileSync(new URL('../../src/network/device_page.h',import.meta.url),'utf8');
const script=page.split('<script>')[1].split('</script>')[0];
new vm.Script(script);
const render=script.slice(script.indexOf('let otaRequestPending='),script.indexOf('async function refreshStatus()'));
const versions=script.slice(script.indexOf('function versionParts('),script.indexOf('function showReleaseLink('));
const elements=new Map(),messages=[];
const get=id=>{if(!elements.has(id))elements.set(id,{style:{},textContent:'',disabled:false});return elements.get(id)};
const context=vm.createContext({$:get,state:{releaseChecked:true},performance:{now:()=>5000},toast:m=>messages.push(m)});
vm.runInContext(versions+render,context);
function status(ota,version='1.5.8'){
 context.status={ota:{active:false,preparing:false,progress:0,...ota},firmware:{version}};
 vm.runInContext('renderOtaStatus(status)',context);
}
status({failed:true});
assert.match(get('releaseStatus').textContent,/Update failed/);
assert.match(get('otaMessage').textContent,/previous firmware is retained/);
assert.equal(get('installUpdate').disabled,false);
// Polling during the ready/start gap must not enable a second request.
vm.runInContext('otaRequestPending=true',context);
status({ready:true});assert.equal(get('installUpdate').disabled,true);
vm.runInContext("otaRequestPending=false;githubInstall={tag:'v1.5.9',startedAt:0}",context);
status({active:true,progress:45});
assert.equal(get('installUpdate').disabled,true);
assert.match(get('releaseStatus').textContent,/45%/);
assert.equal(messages.length,0,'acceptance or progress is not success');
// Recovery reboot with the old version must not count as success.
status({});assert.match(get('releaseStatus').textContent,/Update failed/);
assert.equal(messages.length,0);
vm.runInContext("githubInstall={tag:'v1.5.9',startedAt:0}",context);
status({},'1.5.9');
assert.equal(messages.length,1);
assert.match(messages[0],/confirmed/);
assert.equal(context.state.releaseChecked,false,'refresh release comparison after reboot');

// Prepare consumes the on-device permission and returns a single-use token;
// the upload must carry it.
{
 const install=script.slice(script.indexOf('async function installFirmwareBlob('),script.indexOf("$('otaButton').onclick"));
 const calls=[],toasts=[];
 const ctx=vm.createContext({$:get,toast:(m,e)=>toasts.push({m,e}),confirm:()=>true,refreshStatus(){},
  setTimeout:fn=>{queueMicrotask(fn);return 1},FormData:class{append(){}},encodeURIComponent,
  async api(url,options){calls.push(url);if(url==='/api/v1/ota/prepare')return{preparing:true,token:'a1b2'};if(url==='/api/v1/status')return{ota:{ready:true}};return 'ok'}});
 vm.runInContext(install,ctx);
 const blob={slice:()=>({arrayBuffer:async()=>new Uint8Array([0xe9]).buffer})};
 ctx.blob=blob;
 assert.equal(await vm.runInContext("installFirmwareBlob(blob,'firmware.bin')",ctx),true);
 assert.deepEqual(calls.filter(u=>u.startsWith('/api/v1/ota')),['/api/v1/ota/prepare','/api/v1/ota?token=a1b2']);
 // A refused prepare (no permission on the grinder) surfaces its reason and uploads nothing.
 calls.length=0;
 ctx.api=async url=>{calls.push(url);throw new Error('Allow the update on the grinder first: Menu > Firmware Update > Allow Update')};
 assert.equal(await vm.runInContext("installFirmwareBlob(blob,'firmware.bin')",ctx),false);
 assert.deepEqual(calls,['/api/v1/ota/prepare']);
 assert.match(toasts.at(-1).m,/Allow the update on the grinder/);
 assert.equal(toasts.at(-1).e,true);
}
// A build without the release channel must not query or offer GitHub releases.
{
 const check=script.slice(script.indexOf('function showReleaseLink('),script.indexOf("$('checkUpdate').onclick"));
 let fetches=0;
 const ctx=vm.createContext({$:get,fetch:async()=>{fetches++;throw new Error('no network')},
  state:{deviceStatus:{ota:{release_updates:false}},releaseChecked:false}});
 vm.runInContext(versions+check,ctx);
 await vm.runInContext('checkForUpdate()',ctx);
 assert.equal(fetches,0);
 assert.match(get('releaseStatus').textContent,/off in this firmware build/);
 assert.equal(get('checkUpdate').style.display,'none');
 assert.equal(get('installUpdate').style.display,'none');
 ctx.state.deviceStatus={ota:{release_updates:true,release_repository:'owner/fork'},device:{hardware_revision:'v1'},firmware:{version:'1.0.0'}};
 const urls=[];ctx.fetch=async url=>{urls.push(url);throw new Error('offline')};
 await vm.runInContext('checkForUpdate()',ctx);
 assert.deepEqual(urls,['https://api.github.com/repos/owner/fork/releases/latest']);
}
// Remote start off: the start control is disabled with a hint; stop stays available.
{
 const renderLive=script.slice(script.indexOf('function stableFlow('),script.indexOf('async function syncCompletedTrace('));
 const nodes=new Map(),node=id=>{if(!nodes.has(id))nodes.set(id,{style:{},textContent:'',disabled:false,hidden:false,classList:{toggle(){}},setAttribute(){}});return nodes.get(id)};
 const ctx=vm.createContext({$:node,liveSamples:[],liveFlows:[],displayFlow:0,lastPhase:'IDLE',resultSyncToken:0,commandPending:false,
  state:{page:'history',settings:null},document:{querySelectorAll:()=>[]},drawLive(){},syncCompletedTrace(){}});
 vm.runInContext(renderLive,ctx);
 const frame=(phase,active,remote)=>({grind:{phase,active,mode:'weight',profile:0,progress:0,target_weight:18,target_time_ms:0},
  scale:{weight:0,flow:0},...(remote===undefined?{}:{control:{remote_start:remote}})});
 ctx.s=frame('IDLE',false,false);vm.runInContext('render(s)',ctx);
 assert.equal(node('grindButton').disabled,true);assert.equal(node('remoteStartHint').hidden,false);
 ctx.s=frame('IDLE',false,true);vm.runInContext('render(s)',ctx);
 assert.equal(node('grindButton').disabled,false);assert.equal(node('remoteStartHint').hidden,true);
 ctx.s=frame('GRINDING',true,false);vm.runInContext('render(s)',ctx);
 assert.equal(node('grindButton').disabled,false,'stop must stay available');assert.equal(node('remoteStartHint').hidden,true);
 ctx.s=frame('IDLE',false,undefined);vm.runInContext('render(s)',ctx);
 assert.equal(node('grindButton').disabled,false,'older firmware without the flag keeps working');
}
console.log('OTA UI status tests passed.');
