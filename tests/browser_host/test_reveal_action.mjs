import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';

// Execute the actual insertion used by the ordinary panel executor. The real
// public C/JS/WebView2 recovery gate separately checks these DOM/native bindings.
const manager=readFileSync(new URL('../../app/web/js/features/workbench/panel-manager.js',import.meta.url),'utf8');
const source=manager.slice(manager.indexOf('// Inserted into the existing workbench executor.'),manager.indexOf('      let view = browserViews.get(id);'));
assert.ok(source.includes('The browser action viewport could not be shown'));
const AsyncFunction=Object.getPrototypeOf(async function(){}).constructor;
const run=new AsyncFunction('command','params','id','api','action','clientId','browserViews','mounts','layout','document','sync','reveal','stopped','performance','setTimeout',source);
function fixture(change={}){
  const state={visible:false,error:'The website connection was interrupted',automation:true,manual:false,process_failed:false,
    controller_bounds:{width:800,height:600},...change.state};
  const root={isConnected:true,hidden:false,querySelector(){return{getBoundingClientRect:()=>({width:800,height:600,...change.surface})};},...change.root};
  const mounted={root,status:{status:'error',message:'The website connection was interrupted',...change.status}};
  const layout={open:true,active:'panel',...change.layout};
  const document={hidden:false,querySelectorAll:()=>change.overlay?[{getClientRects:()=>[{}]}]:[],...change.document};
  let now=0,calls=0;
  return{state,execute:command=>run(command,{timeout_ms:10},'browser',{bind(){return{async call(method){assert.equal(method,'panel.open');return{id:'panel'};}};}},
    {context:{}},'client',new Map([['browser',{row:{id:'panel'},async execute(command){calls++;assert.equal(command,'state');return state;}}]]),
    new Map([['panel',mounted]]),layout,document,async()=>{},()=>{},false,{now:()=>now},callback=>{now+=50;callback();}),calls:()=>calls};
}

test('explicit navigation can recover a page error hidden by its own error UI',async()=>{
  const f=fixture();await f.execute('navigate_to');assert.equal(f.calls(),1);
});
test('a visible native action retains its ordinary viewport requirement',async()=>{
  const f=fixture({state:{visible:true,error:''},status:{status:'ready',message:''}});await f.execute('act');assert.equal(f.calls(),1);
});
test('hidden page-error recovery does not admit input or capture',async()=>{
  for(const command of ['act','capture'])await assert.rejects(fixture().execute(command),/viewport could not be shown/);
});
test('error navigation still requires the original shown pane, healthy controller and unobstructed surface',async()=>{
  const cases=[{state:{manual:true}},{state:{process_failed:true}},{state:{creating:true}},{state:{close_requested:true}},
    {state:{closed_by_page:true}},{state:{automation:false}},{state:{error:''}},{state:{error:17}},
    {state:{dialog_open:true}},{state:{popup_open:true}},
    {state:{controller_bounds:{width:0,height:600}}},{state:{controller_bounds:{width:800,height:NaN}}},
    {state:{controller_bounds:undefined}},{root:{isConnected:false}},{root:{hidden:true}},
    {layout:{open:false}},{layout:{active:'other-panel'}},{document:{hidden:true}},{overlay:true},
    {surface:{width:0}},{surface:{height:Infinity}},{status:{status:'loading'}},
    {status:{message:'The main native bridge is unavailable'}}];
  for(const change of cases)await assert.rejects(fixture(change).execute('navigate_to'),/viewport could not be shown/);
});
