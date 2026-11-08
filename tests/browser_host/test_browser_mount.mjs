import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { createBrowserViewLifetime } from '../../app/web/js/features/workbench/browser-ownership.js';

// Execute the applied host wrapper, not a maintained copy of its behavior.
const source=readFileSync(new URL('../../app/web/js/features/workbench/panel-manager.js',import.meta.url),'utf8');
const body=source.slice(source.indexOf('  async function browserView('),source.indexOf('  async function mount(row)'));
function fixture({preview=false}={}) {
  const row={id:'panel',type:preview?'mdo.preview':'mdo.browser',owner:'extension',client:'display',context:{device:'device',project:'project',session:'task'},...(preview?{params:{url:'https://example.test'}}:{resource:{id:'browser'}})};
  const nativeRow=preview?{...row,resource:{id:'browser'}}:row;
  const controller=new AbortController(),mount={abort:controller,browserId:'browser'},mounts=new Map([['panel',mount]]),entries=new Map([['panel',row]]),views=new Map(),closed=[],reconciled=[],titles=[],remounted=[];
  let resolve,options,currentTarget='local';
  const factory=new Function('mountBrowserView','bound','bridge','clientId','layout','workbenchVisible','navigation','update','layoutApi','addContext','browserViews','close','popupManager','isRemoteTarget','targetState','createBrowserViewLifetime','entries','mounts','remount',
    `let stopped=false;const displayTarget='local';${body};return {browserView,stop(){stopped=true;}};`);
  const layout={open:true,active:'panel'};
  const host=factory(async(root,value)=>{options=value;return new Promise(done=>{resolve=done;});},value=>({context:value.context}),{},'display',layout,()=>layout.open,{get:()=>({view:'chat'})},
    async(id,patch,current=()=>true)=>{if(current())titles.push({id,patch});},{call(){throw Error('No external navigation');}},()=>{},views,
    async(id,current=()=>true)=>{if(current())closed.push(id);},{reconcile:id=>reconciled.push(id)},()=>currentTarget!=='local',()=>({selected:{id:currentTarget}}),createBrowserViewLifetime,entries,mounts,id=>remounted.push(id));
  const execute=async()=>({state:'original'}),view={id:'browser',execute};
  const pending=host.browserView({hidden:false},nativeRow,controller.signal,mount);
  return{...host,row,nativeRow,controller,mount,mounts,entries,views,view,pending,closed,reconciled,titles,layout,remounted,options:()=>options,ready:()=>resolve(view),target:value=>{currentTarget=value;}};
}

for(const cause of ['mount','target','scope','resource','stop'])test(`late native mount cannot publish after ${cause} changes`,async()=>{
  const f=fixture();
  if(cause==='mount')f.mounts.set('panel',{abort:new AbortController()});
  if(cause==='target')f.target('another-device');if(cause==='scope')f.entries.set('panel',{...f.row,context:{...f.row.context,session:'foreign'}});
  if(cause==='resource')f.entries.set('panel',{...f.row,resource:{id:'replacement'}});if(cause==='stop')f.stop();
  f.ready();await f.pending;assert.equal(f.views.size,0);assert.equal(f.reconciled.length,0);assert(!f.options().alive());
  assert.deepEqual(f.remounted,[]);
});

test('late native mount preserves a newer view of the same task',async()=>{
  const f=fixture(),replacement={row:f.row,execute:async()=>({})};f.views.set('browser',replacement);f.ready();await f.pending;
  assert.equal(f.views.get('browser'),replacement);assert.equal(f.reconciled.length,0);
});

for(const timing of ['initializing','bound'])test(`a task dispatcher arriving during ${timing} mount requests a fresh scoped pane`,async()=>{
  const f=fixture();if(timing==='bound')assert(f.options().onBind(f.view));
  const dispatcher={row:{id:'browser',client:f.row.client,owner:f.row.owner,context:f.row.context},execute:async()=>({ready:'original-task'})};
  f.views.set('browser',dispatcher);assert(!f.options().alive());
  f.ready();await f.pending;
  assert.equal(f.views.get('browser'),dispatcher);assert.deepEqual(f.reconciled,[]);assert.deepEqual(f.remounted,['panel']);
  assert.equal((await dispatcher.execute('state')).ready,'original-task');assert(!f.options().canHide());
  await f.options().onEnd('browser');assert.deepEqual(f.closed,[]);
  const stale=f.options();f.controller.abort();stale.onDispose();
  const next={abort:new AbortController()};f.mounts.set('panel',next);
  const mounted=f.browserView({hidden:false},f.nativeRow,next.abort.signal,next);
  assert(f.options().onBind(f.view));assert.equal(f.views.get('browser'),dispatcher);
  f.ready();await mounted;
  assert(f.options().alive());assert.equal(f.views.get('browser').row.id,'panel');assert.deepEqual(f.reconciled,['browser']);assert.deepEqual(f.remounted,['panel']);
  await stale.onEnd('browser');assert.deepEqual(f.closed,[]);assert(!stale.canHide());
});

for(const cause of ['abort','mount','record','resource','scope','target','stop'])test(`stale end callback cannot close its ${cause} replacement`,async()=>{
  const f=fixture();f.ready();await f.pending;const original=f.views.get('browser');let expected=original;
  if(cause==='abort')f.controller.abort();if(cause==='mount')f.mounts.set('panel',{abort:new AbortController()});
  if(cause==='record'){expected={row:f.row,execute:async()=>({})};f.views.set('browser',expected);}
  if(cause==='resource')f.entries.set('panel',{...f.row,resource:{id:'replacement'}});
  if(cause==='scope')f.entries.set('panel',{...f.row,context:{...f.row.context,session:'foreign'}});
  if(cause==='target')f.target('another-device');if(cause==='stop')f.stop();await f.options().onEnd('browser');
  assert.equal(f.views.get('browser'),expected);assert.equal(f.closed.length,0);
});

test('ending one valid browser is idempotent and never accepts another native ID',async()=>{
  const f=fixture();f.ready();await f.pending;
  await f.options().onEnd('foreign');assert(f.views.has('browser'));assert.equal(f.closed.length,0);
  await f.options().onEnd('browser');await f.options().onEnd('browser');assert.equal(f.views.size,0);assert.deepEqual(f.closed,['panel']);assert(!f.options().alive());
});

test('preview and metadata refresh keep the original mounted owner',async()=>{
  for(const preview of [false,true]){
    const f=fixture({preview});f.entries.set('panel',{...f.row,title:'Refreshed',revision:8});f.ready();await f.pending;
    assert(f.views.has('browser'));assert(f.options().alive());assert.deepEqual(f.reconciled,['browser']);await f.options().onEnd('browser');assert.deepEqual(f.closed,['panel']);
  }
});

test('unmount revokes UI callbacks without revoking the shared native dispatcher',async()=>{
  const f=fixture();f.ready();await f.pending;const registered=f.views.get('browser');assert.deepEqual(Object.keys(registered).sort(),['execute','row']);
  f.controller.abort();assert(!f.options().alive());assert.equal(f.views.get('browser'),registered);assert.equal((await registered.execute('state')).state,'original');
  await f.options().updateTitle('stale');assert.equal(f.titles.length,0);assert.equal(f.closed.length,0);
});

test('a native result cannot register a different browser identity',async()=>{
  const f=fixture();f.view.id='foreign';f.ready();await f.pending;
  assert.equal(f.views.size,0);assert.equal(f.reconciled.length,0);await f.options().onEnd('foreign');assert.equal(f.closed.length,0);
});

function updating(){
  const text=source.slice(source.indexOf('  const update = async'),source.indexOf('  const handle ='));
  const original={id:'panel',revision:1},saved={id:'panel',revision:2,title:'Original result'},entries=new Map([['panel',original]]),effects=[];let resolve,requests=0;
  const factory=new Function('entries','bound','remember','render',`let stopped=false;${text};return{update,stop(){stopped=true;}};`);
  return{...factory(entries,row=>({call(method,params){assert.equal(row,original);assert.equal(method,'panel.update');assert.equal(params.revision,1);requests++;return new Promise(done=>{resolve=done;});}}),()=>effects.push('remember'),()=>effects.push('render')),
    entries,original,saved,effects,requests:()=>requests,resolve:()=>resolve(saved)};
}
for(const cause of ['replacement','owner','stop','removal'])test(`actual panel update ignores its late result after ${cause}`,async()=>{
  const f=updating();let current=true;const pending=f.update('panel',{title:'Original result'},()=>current);assert.equal(f.requests(),1);let expected=f.original;
  if(cause==='replacement'){expected={id:'panel',revision:2,title:'New owner'};f.entries.set('panel',expected);}
  if(cause==='owner')current=false;if(cause==='stop')f.stop();if(cause==='removal'){f.entries.delete('panel');expected=undefined;}
  f.resolve();assert.equal(await pending,undefined);assert.equal(f.entries.get('panel'),expected);assert.equal(f.effects.length,0);
});
test('actual panel update preserves its saved result for a current owner',async()=>{
  const f=updating(),pending=f.update('panel',{title:'Original result'},()=>true);f.resolve();assert.equal(await pending,f.saved);assert.equal(f.entries.get('panel'),f.saved);assert.deepEqual(f.effects,['remember','render']);
});

test('binding retires the old UI before native creation, with one final reconciliation',async()=>{
  const f=fixture();assert(f.options().onBind(f.view));
  assert.equal(f.views.size,0,'initializing native view is not a ready task dispatcher');assert.equal(f.reconciled.length,0);assert(f.options().alive());
  f.ready();await f.pending;assert.equal(f.views.get('browser').execute,f.view.execute);assert.deepEqual(f.reconciled,['browser']);
});
test('shared UI claims retire an old pane while preserving the existing ready task dispatcher',async()=>{
  const row={id:'first',resource:{id:'browser'},owner:'extension',client:'display',context:{project:'project',session:'task'}},
    other={...row,id:'second'},mounts=new Map(),entries=new Map([['first',row],['second',other]]),views=new Map(),reconciled=[];
  const create=value=>{const abort=new AbortController(),mount={};mounts.set(value.id,mount);return createBrowserViewLifetime({row:value,signal:abort.signal,mount,mounts,entries,views,
    alive:()=>true,close:async()=>{},reconcile:id=>reconciled.push(id)});};
  const a=create(row),first={id:'browser',execute:async()=>({ready:'first'})};assert(a.bind(first));assert(a.register(first));const ready=views.get('browser');
  const b=create(other),second={id:'browser',execute:async()=>({ready:'second'})};assert(b.bind(second));assert(!a.current());assert(b.current());
  assert.equal(views.get('browser'),ready);assert.equal((await ready.execute('state')).ready,'first');assert.deepEqual(reconciled,['browser']);
  assert(!a.canHide());assert(b.canHide());a.release();assert(b.current());assert(b.register(second));assert.equal(views.get('browser').execute,second.execute);
  b.release();assert(!b.current());assert(b.canRemount());
});
for(const cause of ['normal','record','mount','scope','target','stop'])test(`cleanup may hide only its original ${cause} owner`,async()=>{
  const f=fixture();assert(f.options().onBind(f.view));f.ready();await f.pending;
  f.mounts.delete('panel');f.controller.abort();
  if(cause==='record')f.views.set('browser',{row:f.row,execute:async()=>({})});
  if(cause==='mount')f.mounts.set('panel',{browserId:'browser'});
  if(cause==='scope')f.entries.set('panel',{...f.row,context:{session:'another-task'}});
  if(cause==='target')f.target('other-device');if(cause==='stop')f.stop();
  assert.equal(Boolean(f.options().canHide()),['normal','stop'].includes(cause));
});
test('only the preferred still-scoped retired pane can request a remount',async()=>{
  const f=fixture();f.ready();await f.pending;f.views.set('browser',{row:f.row,execute:async()=>({})});assert(!f.options().alive());
  f.layout.active='other';f.options().onRetired();assert.equal(f.remounted.length,0);
  f.layout.secondary='panel';f.mounts.set('other',{browserId:'browser'});f.options().onRetired();assert.equal(f.remounted.length,0);
  f.mounts.set('other',{browserId:'different-browser'});f.options().onRetired();assert.deepEqual(f.remounted,['panel']);
  f.remounted.length=0;f.views.set('browser',{row:{...f.row,context:{session:'foreign'}},execute:async()=>({})});f.options().onRetired();assert.equal(f.remounted.length,0);
});
