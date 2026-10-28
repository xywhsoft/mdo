import assert from 'node:assert/strict';
import test from 'node:test';
import {readFileSync,mkdirSync,writeFileSync,rmSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {resolve} from 'node:path';
import {TestElement,testDocument,descendants} from './helpers/extension-dom.mjs';
let moduleSerial=0;
const site=resolve('D:/GIT/home/host/xywhsoft_ai/plugin/mdo/page'),data=source=>'data:text/javascript;base64,'+Buffer.from(source+`\n//# sourceURL=platform-web-module-${++moduleSerial}.mjs\n`).toString('base64'),urls={};
urls['/mdo/extensions-sha256.js']=data(readFileSync(site+'/sha256.js','utf8'));
urls['/mdo/extensions-zip.js']=data(readFileSync(site+'/extensions/zip-directory.js','utf8'));
urls['/mdo/extensions-transfer.js']=data(readFileSync(site+'/extensions/transfer.js','utf8').replaceAll("'/mdo/extensions-sha256.js'",JSON.stringify(urls['/mdo/extensions-sha256.js'])));
urls['/mdo/extensions-ui.js']=data(readFileSync(site+'/extensions/public.js','utf8').replace(/from '(\/mdo\/[^']+)'/g,(_,path)=>'from '+JSON.stringify(urls[path])));
const zip=await import(urls['/mdo/extensions-zip.js']),transfer=await import(urls['/mdo/extensions-transfer.js']),ui=await import(urls['/mdo/extensions-ui.js']);
const storage=()=>{const map=new Map();return{getItem:key=>map.get(key)||null,setItem:(key,value)=>map.set(key,value),removeItem:key=>map.delete(key)};};
const flush=()=>new Promise(resolve=>setImmediate(resolve)),find=(root,text)=>descendants(root).find(n=>n.tagName==='BUTTON'&&n.textContent===text);
function dom(t){const previous={document:globalThis.document,sessionStorage:globalThis.sessionStorage,location:globalThis.location};globalThis.document=testDocument();document.head=new TestElement('head');globalThis.sessionStorage=storage();globalThis.location=new URL('https://ai.xywhsoft.com/mdo/creator');t.after(()=>Object.assign(globalThis,previous));}

test('website ZIP builder retains actual files, hidden dependencies, empty dirs and executable modes; Python interoperates',async()=>{
 const blob=await zip.directoryZip([{path:'.hidden/deep.bin',blob:new Blob([new Uint8Array([0,255,17])])},{path:'bin/peer',blob:new Blob(['native-fixture']),executable:true},{path:'empty/'}],{manifest:{kind:'mcp',name:'Fixture',alias:'zip-fixture',entry:'mcp.json',platforms:['any']},entryText:'{"variants":[]}'});
 const dir=resolve('.build/platform-directory-web');mkdirSync(dir,{recursive:true});const path=dir+'/fixture.zip';writeFileSync(path,new Uint8Array(await blob.arrayBuffer()));
 const result=spawnSync('C:/Program Files/Python312/python.exe',['-c',"import zipfile,sys,json; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; assert z.read('.hidden/deep.bin')==bytes([0,255,17]); assert z.getinfo('empty/').is_dir(); assert z.getinfo('bin/peer').external_attr>>16 & 0o111; assert json.loads(z.read('extension.json'))['kind']=='mcp'; assert z.read('bin/peer')==b'native-fixture'; print('ok')",path],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);assert.equal(result.stdout.trim(),'ok');
});
test('website ZIP builder rejects traversal, devices, case collisions, missing entry and quota',async()=>{
 const manifest={kind:'tools',entry:'main.c'};
 for(const path of ['../main.c','/main.c','a\\main.c','CON.txt','trailing./file','a/../main.c'])await assert.rejects(zip.directoryZip([{path}],{manifest,entryText:'source'}),/路径/);
 await assert.rejects(zip.directoryZip([{path:'A.c',blob:new Blob(['x'])},{path:'a.c',blob:new Blob(['y'])}],{manifest:{entry:'a.c'}}),/冲突/);
 await assert.rejects(zip.directoryZip([],{manifest}),/入口/);
 await assert.rejects(zip.directoryZip([],{manifest,entryText:'source',maxBytes:1}),/容量/);
});
test('raw upload recovers lost chunk response, does not resend sealed bytes, and preserves the exact request ID',async t=>{
 const file=new Blob([new Uint8Array(1048600).fill(75)]),cache=storage(),calls=[];let received=0,state='receiving',lost=true;
 t.mock.method(globalThis,'fetch',async(url,options)=>{calls.push([url,options]);const body=options.headers['Content-Type']==='application/json'?JSON.parse(options.body):null;
  if(options.method==='PUT'){assert(options.body instanceof Blob);received+=options.body.size;if(lost){lost=false;throw Error('response lost');}return Response.json({code:0,data:{received}});}
  if(body?.action==='seal'){state='sealed';return Response.json({code:0,data:{xid:'1'.repeat(32),state}});}
  return Response.json({code:0,data:{xid:'1'.repeat(32),state,received}});
 });
 const settings={kind:'mcp',owner:7,csrf:'memory-only',storage:cache};await transfer.upload(file,settings);const count=calls.filter(([,o])=>o.method==='PUT').length;await transfer.upload(file,settings);assert.equal(count,2);assert.equal(calls.filter(([,o])=>o.method==='PUT').length,2);
 const begins=calls.filter(([,o])=>o.headers['Content-Type']==='application/json'&&JSON.parse(o.body).action==='begin').map(([,o])=>JSON.parse(o.body));assert.equal(begins.length,2);assert.equal(begins[0].request_id,begins[1].request_id);assert(!JSON.stringify(begins).includes('memory-only'));assert(begins[0].sha256.match(/^[a-f0-9]{64}$/));
});
test('upload never trusts invalid offsets or JSON byte containers and honors cancellation',async t=>{
 t.mock.method(globalThis,'fetch',async()=>Response.json({code:0,data:{xid:'2'.repeat(32),state:'receiving',received:-1}}));
 await assert.rejects(transfer.upload(new Blob(['bytes']),{kind:'tools',owner:8,storage:storage()}),/位置/);
 const cancel=new AbortController();cancel.abort();await assert.rejects(transfer.upload(new Blob(['bytes']),{kind:'tools',owner:8,storage:storage(),signal:cancel.signal}),/abort/i);
});
test('submission freezes the whole form and keeps an identical bounded retry after an unknown response',async t=>{
 dom(t);let lost=true,hold,release;const submitted=[];t.mock.method(globalThis,'fetch',async(url,options)=>{const body=options.headers['Content-Type']==='application/json'?JSON.parse(options.body):null;if(body?.action==='begin')return Response.json({code:0,data:{state:'sealed',xid:'3'.repeat(32)}});if(body?.action==='submit'){submitted.push(body);if(hold)await hold;if(lost){lost=false;throw Error('response lost');}return Response.json({code:0,data:{state:'private'}});}throw Error('unexpected '+url);});
 const view=ui.submission({csrf:'memory-only',owner:9,kind:'mcp',limit:100000}),form=view.body.querySelector('form');form.reportValidity=()=>true;for(const control of form.elements)if(control.type==='file')control.files=[];const archive=form.elements.find(n=>n.name==='archive');archive.files=[new Blob(['PK-fixture'])];form.elements.find(n=>n.name==='publish').checked=false;
 hold=new Promise(resolve=>{release=resolve;});find(view.footer,'提交').click();await flush();await flush();assert(form.elements.every(n=>n.disabled));release();await flush();await flush();assert(view.dialog.isConnected);assert(form.elements.every(n=>n.disabled));assert.equal(submitted.length,1);hold=null;find(view.footer,'重试上次提交').click();await flush();await flush();assert.equal(submitted.length,2);assert.deepEqual(submitted[0],submitted[1]);assert(!JSON.stringify(submitted).includes('memory-only'));assert(!view.dialog.isConnected);
});
test('admin uses native layui table toolbar, pagination and nine-kind endpoint; teardown removes listeners',async t=>{
 dom(t);globalThis.location=new URL('https://ai.xywhsoft.com/admin/mdo/extensions/mcp');const previous={layui:globalThis.layui,MutationObserver:globalThis.MutationObserver};let options,observer,removed=false;
 globalThis.layui={use:(_,resolve)=>resolve(),table:{on(){},render:o=>{options=o;},reload(){},destroy(){removed=true;},cache:{}},form:{render(){}},off(){}};
 globalThis.MutationObserver=class{constructor(fn){observer=fn;}observe(){}disconnect(){}};t.after(()=>Object.assign(globalThis,previous));
 const root=new TestElement('main'),grid=new TestElement('table'),feedback=new TestElement('p');feedback.setAttribute('data-feedback','');root.append(feedback,grid);document.body.append(root);
 const source=readFileSync(site+'/extensions/admin.js','utf8').replace(/from '(\/mdo\/[^']+)'/g,(_,path)=>'from '+JSON.stringify(urls[path]));const admin=await import(data(source));await admin.mount(root);assert.deepEqual(options.defaultToolbar,['filter','exports','print']);assert.equal(options.where.kind,'mcp');assert.equal(options.request.pageName,'page');assert.equal(options.url,'/admin/api/mdo/extensions');assert(options.page.layout.includes('skip'));assert.equal(options.parseData({code:0,data:{csrf:'private-memory',items:[{name:'fixture'}],total:1}}).data[0].name,'fixture');root.remove();observer();assert(removed);
});
