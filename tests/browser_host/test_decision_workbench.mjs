import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';

const root=new URL('../../app/web/js/',import.meta.url);
const manager=readFileSync(new URL('features/workbench/panel-manager.js',root),'utf8');
const application=readFileSync(new URL('app.js',root),'utf8');
const start=application.indexOf('    onDecisionArrived: (card) => {');
const callback=application.slice(start+'    onDecisionArrived: '.length,application.indexOf('\n  });',start)).trim().replace(/,$/,'');
function arrival({mobile=false,hidden=false,early=false}={}) {
  const events=[],heading={focus(){events.push('focus');}};
  const run=new Function('applicationUi','mobileLayout','shell','document','promptComposition','prompt','composerText','composerAttachments','setSidebar',
    'return '+callback)(early?undefined:{revealConversation(){events.push('reveal');return hidden;}},{matches:mobile},
    {dataset:{sidebar:'closed'}},{querySelector:()=>null,activeElement:null,body:null},{isComposing:()=>false},{},()=>'',[],()=>events.push('sidebar'));
  return{events,run:()=>run({querySelector:()=>heading})};
}
for(const mobile of [false,true])test(`a new decision first reveals a covered conversation on ${mobile?'mobile':'desktop'}`,()=>{
  const f=arrival({mobile,hidden:true});f.run();assert.equal(f.events[0],'reveal');
  assert(f.events.includes('focus'));
});
test('ordinary desktop typing keeps its focus when no panel obscures the decision',()=>{
  const f=arrival();f.run();assert.deepEqual(f.events,['reveal']);
});
test('an early decision before application UI initialization remains safe',()=>{
  const f=arrival({early:true});assert.doesNotThrow(f.run);
});

function fixture({width=1100,open=true,maximized=false,stopped=false,pending=true}={}) {
  const begin=manager.indexOf('  // A decision can arrive');assert(begin>=0,'The panel manager has no decision visibility path');
  const code=manager.slice(begin,manager.indexOf('  const reveal =',begin));
  const toggleStart=manager.indexOf('  async function toggleWorkbench()');
  const toggle=manager.slice(toggleStart,manager.indexOf('  definitions.set(',toggleStart));
  const layout={width:480,open,maximized,active:'page',secondary:'other',order:['page','other']},
    entries=new Map([['page',{id:'page',resource:{id:'native'}}],['other',{id:'other'}]]),events=[];
  const api=new Function('layout','innerWidth','stopped','remember','render','shell','ensure','newTab',code+toggle+
    ';return {run:revealConversation,visible:workbenchVisible,toggle:toggleWorkbench};')(
    layout,width,stopped,()=>events.push('remember'),()=>events.push('render'),
    {querySelector:()=>({hasAttribute:()=>pending})},async()=>{},()=>events.push('new-tab'));
  return{...api,layout,entries,events,pending:value=>{pending=value;}};
}
for(const width of [360,760,1000,1100])test(`overlay workbench at ${width}px yields without discarding tabs`,()=>{
  const f=fixture({width}),before=structuredClone(f.layout),rows=[...f.entries];
  assert.equal(f.run(),true);assert.equal(f.visible(),false);
  assert.deepEqual(f.layout,before);assert.deepEqual([...f.entries],rows);
  assert.deepEqual(f.events,['render']);f.pending(false);assert.equal(f.visible(),true);
});
test('maximized workbench yields on a wide desktop without clearing maximization preference',()=>{
  const f=fixture({width:1600,maximized:true});assert.equal(f.run(),true);
  assert.equal(f.visible(),false);assert.equal(f.layout.open,true);assert.equal(f.layout.maximized,true);assert.equal(f.entries.size,2);
});
for(const state of [{width:1101},{width:1600},{open:false},{stopped:true},{pending:false}])test(`non-covering or retired workbench stays unchanged: ${JSON.stringify(state)}`,()=>{
  const f=fixture(state),before=structuredClone(f.layout);assert.equal(f.run(),false);
  assert.deepEqual(f.layout,before);assert.deepEqual(f.events,[]);
});
test('explicit inspection may reveal tabs; a new decision takes priority again',async()=>{
  const f=fixture();assert.equal(f.visible(),false);const before=structuredClone(f.layout);
  await f.toggle();assert.equal(f.visible(),true);assert.deepEqual(f.layout,before);assert(!f.events.includes('remember'));
  f.run();assert.equal(f.visible(),false);assert.deepEqual(f.layout,before);
});
test('closing during inspection remains closed after the decision finishes',async()=>{
  const f=fixture();await f.toggle();await f.toggle();assert.equal(f.layout.open,false);
  f.pending(false);assert.equal(f.visible(),false);assert.deepEqual(f.layout.order,['page','other']);
});
test('automatic activation while another decision waits cannot cover it',()=>{
  const f=fixture();f.layout.active='other';f.layout.open=true;
  assert.equal(f.visible(),false);f.pending(false);assert.equal(f.visible(),true);assert.equal(f.layout.active,'other');
});
