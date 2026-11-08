import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';

// Execute the applied manager's real mount/error path. Only DOM/broker delivery
// is controlled; no copy of its error classification or action guards is kept.
const source=readFileSync(new URL('../../app/web/js/features/workbench/panel-manager.js',import.meta.url),'utf8');
const failure=source.slice(source.indexOf('  const fail ='),source.indexOf('  const save ='));
const mounting=source.slice(source.indexOf('  async function mount(row)'),source.indexOf('  const remount ='));
function fixture(error) {
  const node=(tag,className,text)=>({tag,className,textContent:text,children:[],hidden:false,
    setAttribute(){},append(...items){this.children.push(...items);},replaceChildren(...items){this.children=items;}});
  const button=(label,run)=>Object.assign(node('button','',label),{click:run});
  const row={id:'panel',type:'mdo.browser',owner:'extension',client:'display',resource:{id:'original'},context:{session:'task'}};
  const entries=new Map([[row.id,row]]),mounts=new Map(),content=node('main'),reports=[],closed=[],retried=[];
  const build=new Function('make','button','mounts','layout','workbenchVisible','content','definitions','bound','displayTarget','browserView',
    'clientId','remount','close','entries','ResizeObserver',failure+mounting+';return mount;');
  const mount=build(node,button,mounts,{open:true,active:row.id},()=>true,content,new Map([[row.type,{renderer:'browser'}]]),
    ()=>({call:async(method,params)=>{reports.push({method,params});}}),'local',async()=>{throw error;},'display',id=>retried.push(id),
    async(id,current)=>{if(current())closed.push(id);},entries,class{observe(){}});
  return{row,entries,mounts,reports,closed,retried,start:()=>mount(row),root:()=>content.children[0]};
}

for(const code of ['resource_missing','browser_missing'])test(`${code} offers an ended-page recovery instead of repeating a permanent read`,async()=>{
  const f=fixture(Object.assign(Error('The requested workbench resource is unavailable'),{status:404,code}));await f.start();
  assert.equal(f.root().children[0].textContent,'页面已结束');
  assert(f.root().children.some(n=>n.textContent?.includes('不会自动重新打开')));
  assert(!f.root().children.some(n=>n.textContent==='重新读取'));
  const close=f.root().children.find(n=>n.textContent==='关闭标签页');assert(close);
  assert.deepEqual(f.closed,[]);await close.click();assert.deepEqual(f.closed,['panel']);assert.deepEqual(f.retried,[]);
  assert.equal(f.reports[0].method,'panel.report');assert.equal(f.reports[0].params.value.status,'error');
});

for(const error of [Object.assign(Error('Disconnected'),{status:503,code:'unavailable'}),
 Object.assign(Error('Another owner'),{status:403,code:'owner_mismatch'}),
 Object.assign(Error('Other missing endpoint'),{status:404,code:'route_missing'})])test(`unproven loss keeps ordinary retry: ${error.code}`,async()=>{
  const f=fixture(error);await f.start();assert.equal(f.root().children[0].textContent,error.message);
  const retry=f.root().children.find(n=>n.textContent==='重新读取');assert(retry);retry.click();assert.deepEqual(f.retried,['panel']);
  assert(!f.root().children.some(n=>n.textContent==='关闭标签页'));
});

for(const change of ['abort','mount','entry'])test(`an obsolete ended-page close cannot touch a ${change} replacement`,async()=>{
  const f=fixture(Object.assign(Error('Gone'),{status:404,code:'resource_missing'}));await f.start();
  const close=f.root().children.find(n=>n.textContent==='关闭标签页');assert(close);
  if(change==='abort')f.mounts.get('panel').abort.abort();
  if(change==='mount')f.mounts.set('panel',{abort:new AbortController()});
  if(change==='entry')f.entries.set('panel',{...f.row,resource:{id:'replacement'}});
  await close.click();assert.deepEqual(f.closed,[]);assert.deepEqual(f.retried,[]);
});
