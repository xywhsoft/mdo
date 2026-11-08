import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';
import {executeFileUpload} from '../../app/web/js/features/workbench/upload-dispatch.js';

const url=source=>`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`;
const reader=(await readFile(new URL('../../app/web/js/features/workbench/frame-reader.js',import.meta.url),'utf8'))
  .replace("import {browserRuntime} from './automation-runtime.js';",'function browserRuntime(){}');
const driver=(await readFile(new URL('../../app/web/js/features/workbench/frame-driver.js',import.meta.url),'utf8'))
  .replace("import {browserRuntime} from './automation-runtime.js';",'function browserRuntime(){}')
  .replace("import {prepareFrameCapture} from './frame-capture.js';",`import {prepareFrameCapture} from ${JSON.stringify(url(await readFile(new URL('../../app/web/js/features/workbench/frame-capture.js',import.meta.url),'utf8')))};`)
  .replace("import {createFrameReader} from './frame-reader.js';",`import {createFrameReader} from ${JSON.stringify(url(reader))};`);
const {createFrameDriver}=await import(url(driver));
const guid=n=>`00000000-0000-4000-8000-${String(n).padStart(12,'0')}`;
const doc='d-'+guid(1),local='d-'+guid(2),child='f-'+guid(2),fd='fd-'+guid(2),rootFd='fd-'+guid(1);

function fixture(nativeCapture=null,nativeFileInputs=false){
  const rows=[{id:'f0',document:rootFd,parent:null,isolated:true,loading:false,url:'https://root.test'},
    {id:child,document:fd,parent:'f0',isolated:true,loading:false,url:'https://child.test'}];
  const state={calls:[],effect:0,parentFocused:true,parentInert:false,onRead:null,checkpoint:null,pickers:[{id:'chooser-fixture',supported:true,frame:child,frame_document:fd}]};
  const api=createFrameDriver({nativeCapture,nativeFileInputs,checkpoint:async()=>{await state.checkpoint?.();},execute:async(command,params)=>{
    state.calls.push({...params,command});
    if(command==='chooser'){
      if(params.action==='list')return{ok:true,choosers:structuredClone(state.pickers)};
      assert.equal(params.action,'target');assert.equal(params.document,local);
      const value={ok:true,document:local,element:{ref:'e7',hidden:true,multiple:true,directory:false}};
      return await state.onRead?.({method:'chooserTarget',options:params,value,rows})||value;
    }
    assert.equal(command,'frames');
    if(params.action==='file_input'){
      const value={ok:true,frame:params.frame,frame_document:params.document,value:{ok:true,document:params.local_document,
        element:{ref:params.ref,hidden:true,multiple:true,directory:false}}};
      return await state.onRead?.({method:'file_input',options:params,value,rows})||value;
    }
    if(params.action==='list')return{ok:true,complete:true,frames:structuredClone(rows)};
    if(params.action==='upload_policy'){
      await state.onRead?.({method:'upload_policy',rows});
      return{ok:true,frame:child,frame_document:fd,parent:'f0',parent_document:rootFd,value:state.parentInert?{ok:false,error:{code:'element_disabled'}}:{ok:true}};
    }
    const box=state.ownerBox||{x:100,y:200,width:300,height:200};
    if(params.action==='capture_owner')return{ok:true,frame:child,frame_document:fd,parent:'f0',parent_document:rootFd,
      value:{ok:true,owner:{document:doc,ref:'e9'},painted:true,content:{...box},box:{...box},scroll:{x:0,y:0},viewport:{width:800,height:600}}};
    if(params.action==='owner'){
      await state.onRead?.({method:'owner',rows});
      return{ok:true,frame:child,frame_document:fd,parent:'f0',parent_document:rootFd,
        value:{ok:true,focused:state.parentFocused,point:{x:box.x+params.point.x,y:box.y+params.point.y},geometry:[box.x,box.y,box.width,box.height,box.x,box.y,box.width,box.height]}};
    }
    const tail=params.script.split('({local_frame:true}).').at(-1),method=tail.slice(0,tail.indexOf('(')),options=JSON.parse(tail.slice(tail.indexOf('(')+1,-1));
    const document=params.frame==='f0'?doc:local,element={ref:'e1',frame:'f0',name:'Child',role:'textbox',editable:true};
    let value={ok:true,document,url:'https://fixture.test',ready:'complete',viewport:{width:800,height:600}};
    if(method==='observe')value={...value,elements:[],frames:[{id:'f0',dom:true}],text:'Fixture',visited:1,truncated:false};
    if(method==='locate')value=params.frame===child?{ok:true,document,element,visited:1}:{ok:false,error:{code:'element_not_found'},visited:1};
    if(method==='prepare')value={ok:true,document,element,point:{x:20,y:10},geometry:[10,0,20,20,20,10]};
    if(method==='verify')value={ok:true,document,focused:true,verified:true};
    if(method==='visualPoint')value={ok:true,document,frame_ref:params.frame==='f0'&&options.point.x>=box.x&&options.point.x<box.x+box.width&&options.point.y>=box.y&&options.point.y<box.y+box.height?'e9':null,...(options.op==='key'?{focus_ref:'e7'}:{})};
    if(method==='scrollPoint'){
      assert.equal(options.document,document);assert.equal(options._route_child,true);
      value={ok:true,document,point:{...options.point},frame_ref:params.frame==='f0'&&options.point.x>=box.x&&options.point.x<box.x+box.width&&options.point.y>=box.y&&options.point.y<box.y+box.height?'e9':null,hit_ref:'e7'};
    }
    if(method==='optionList')value={ok:true,document,ref:'e1',frame:'f0',options:[{index:0,label:'First'}]};
    if(method==='uploadInfo')value={ok:true,document,element:{...element,role:'fileinput',hidden:true,multiple:true,directory:false}};
    if(method==='selectOptions'){state.effect++;value={ok:true,document,dispatched:true,verified:true};}
    if(method==='captureArea')value={ok:true,document,visited:1,scroll:{x:0,y:0},viewport:{width:params.frame==='f0'?800:300,height:params.frame==='f0'?600:200},device_pixel_ratio:1,
      redactions:params.frame==='f0'?[{x:1,y:1,width:10,height:10},{x:100,y:200,width:300,height:200,frame_ref:'e9'}]:[{x:30,y:30,width:10,height:10}],
      region:options.mode==='element'?{x:20,y:20,width:50,height:40}:{x:0,y:0,width:params.frame==='f0'?800:300,height:params.frame==='f0'?600:200}};
    const custom=await state.onRead?.({method,options,value,rows});
    return custom||{ok:true,frame:params.frame,frame_document:params.document,value};
  }});
  return{state,api};
}

test('prepare maps a qualified target and pins the complete document chain',async()=>{
  const {api}=fixture(),out=await api.read('prepare',{document:doc,target:{name:'Child'},op:'fill',scroll:true});
  assert(out.ok);assert.equal(out.document,doc);assert.equal(out.element.frame,child);assert(out.element.ref.startsWith('r1|'));
  assert.deepEqual(out.point,{x:120,y:210});assert.equal(out.geometry.length,16);
  assert.deepEqual(out.frame_chain,[{frame:child,document:fd},{frame:'f0',document:rootFd}]);
  assert.deepEqual(out.native_target,{document:local,ref:'e1'});
});

test('Android frame capture freezes its native navigation and retains the complete shared privacy stamp',async()=>{
  const context={navigation:7},f=fixture(context);context.navigation=999;
  const out=await f.api.read('captureArea',{document:doc,mode:'viewport'});
  assert(out.ok);assert.deepEqual(out.native_capture,{adapter:'android-native-frames-v1',native_document:rootFd,document:doc,navigation:7,
    viewport:out.viewport,scroll:out.scroll,device_pixel_ratio:out.device_pixel_ratio,frame_stamp:out.frame_stamp});
  const stamp=JSON.parse(out.frame_stamp);assert.equal(stamp.length,2);assert.equal(stamp[0][2],null);assert.equal(stamp[1][6],local);
  assert.equal(stamp[1][9].owner.ref,'e9');assert.equal(out.frame_scope,'native-frame-tree');
  assert.deepEqual(out.limitations,['android-visible-viewport-only']);assert.equal(out.redactions.length,2);
  const windows=await fixture().api.read('captureArea',{document:doc,mode:'viewport'});assert(windows.ok);
  assert(!Object.hasOwn(windows,'native_capture'));assert.equal(windows.frame_stamp,out.frame_stamp);
  for(const navigation of [0,-1,1.5,'7',NaN,Infinity,Number.MAX_SAFE_INTEGER+1])
    assert.throws(()=>fixture({navigation}),error=>error.code==='capture_context_invalid');
});

test('visual points bind the exact root or child owner without DOM selector guesses',async()=>{
  const {api,state}=fixture();const root=await api.read('visualPoint',{document:doc,point:{x:40,y:30}});
  assert(root.ok);assert.deepEqual(root.frame_chain,[{frame:'f0',document:rootFd}]);
  const nested=await api.read('visualPoint',{document:doc,point:{x:120,y:210}});
  assert(nested.ok);assert.deepEqual(nested.point,{x:120,y:210});
  assert.deepEqual(nested.frame_chain,[{frame:child,document:fd},{frame:'f0',document:rootFd}]);
  assert(!state.calls.some(row=>row.action==='evaluate'&&row.script.includes('.locate(')));
});

test('visual key routing binds a private target to the exact root or child document',async()=>{
  for(const point of [{x:40,y:30},{x:120,y:210}]){
    const {api,state}=fixture(),out=await api.read('visualPoint',{document:doc,point,op:'key'});
    const nested=point.x===120;assert(out.ok);assert.equal(out.target.ref,`r1|${nested?child:'f0'}|${nested?fd:rootFd}|${nested?local:doc}|e7`);
    assert.deepEqual(out.native_target,{document:nested?local:doc,ref:'e7'});
    assert(!state.calls.some(row=>row.script?.includes('.locate(')));
  }
});

test('scroll points bind root and native child hits to exact local documents and complete chains',async()=>{
  for(const point of [{x:40,y:30},{x:120,y:210}]){
    const {api,state}=fixture(),out=await api.read('scrollPoint',{document:doc,point});const nested=point.x===120;
    assert(out.ok,JSON.stringify(out));assert.equal(out.document,doc);assert.deepEqual(out.point,point);
    assert.equal(out.hit_ref,`r1|${nested?child:'f0'}|${nested?fd:rootFd}|${nested?local:doc}|e7`);
    assert.deepEqual(out.native_target,{document:nested?local:doc,ref:'e7'});
    assert.deepEqual(out.frame_chain,nested?[{frame:child,document:fd},{frame:'f0',document:rootFd}]:[{frame:'f0',document:rootFd}]);
    assert(!state.calls.some(row=>/\.(?:visualPoint|locate|focus|prepare)\(/.test(row.script||'')));
    const reads=state.calls.filter(row=>row.script?.includes('.scrollPoint('));assert.equal(reads.length,nested?2:1);
  }
});

test('scroll routing refuses drift, changed documents, malformed hits and cancellation before input',async()=>{
  for(const mode of ['point','document','missing-ref','bad-frame-ref','navigation']){
    const {api,state}=fixture();state.onRead=({method,value,rows})=>{
      if(method!=='scrollPoint')return;
      if(mode==='point')value.point.x++;
      if(mode==='document')value.document='d-'+guid(9);
      if(mode==='missing-ref')delete value.hit_ref;
      if(mode==='bad-frame-ref')value.frame_ref='caller-picked';
      if(mode==='navigation')rows[0].document='fd-'+guid(9);
    };
    const out=await api.read('scrollPoint',{document:doc,point:mode==='missing-ref'?{x:40,y:30}:{x:120,y:210}});
    assert.equal(out.ok,false,mode);assert.equal(out.error.effect_may_have_occurred,false);assert.equal(state.effect,0);
  }
  const {api,state}=fixture(),cancel=Error('Original caller cancelled');let retired=false;
  state.onRead=({method})=>{if(method==='scrollPoint')retired=true;};
  await assert.rejects(api.read('scrollPoint',{document:doc,point:{x:120,y:210}},{checkpoint(){if(retired)throw cancel;}}),error=>error===cancel);
});

test('native point routing admits the visible content of a partially clipped iframe',async()=>{
  for(const method of ['scrollPoint','visualPoint']){
    const {api,state}=fixture();state.ownerBox={x:-10,y:-20,width:300,height:200};
    const point={x:5,y:10},out=await api.read(method,{document:doc,point});
    assert(out.ok,JSON.stringify(out));assert.deepEqual(out.point,point);assert.equal(out.frame_chain[0].frame,child);
  }
});

test('visual key routing rejects missing or malformed local focus references',async()=>{
  for(const ref of [undefined,'','r1|foreign','e0','e1;throw 1']){
    const {api,state}=fixture();state.onRead=async({method,value})=>{if(method==='visualPoint'){value.focus_ref=ref;return{ok:true,frame:'f0',frame_document:rootFd,value};}};
    const out=await api.read('visualPoint',{document:doc,point:{x:40,y:30},op:'key'});assert(!out.ok);assert.equal(out.error.code,'frame_response_invalid');
  }
});

test('visual routing refuses unknown owners, replaced children and cancelled reads',async()=>{
  for(const mode of ['owner','navigation','cancel']){
    const {api,state}=fixture();state.onRead=({method,value,rows})=>{
      if(mode==='owner'&&method==='visualPoint'&&value.frame_ref)value.frame_ref='e99';
      if(mode==='navigation'&&method==='owner')rows[1].document='fd-'+guid(9);
      if(mode==='cancel'&&method==='visualPoint')state.checkpoint=()=>{throw Error('Cancelled visual route');};
    };
    if(mode==='cancel')await assert.rejects(api.read('visualPoint',{document:doc,point:{x:120,y:210}}),/Cancelled/);
    else{const result=await api.read('visualPoint',{document:doc,point:{x:120,y:210}});assert.equal(result.ok,false,mode);assert.equal(result.error.effect_may_have_occurred,false);}
  }
});

test('parent focus participates in keyboard verification without discarding field state',async()=>{
  const {api,state}=fixture();let out=await api.read('verify',{document:doc,target:{name:'Child'},op:'fill',text:'synthetic'});
  assert(out.ok&&out.focused&&out.verified);state.parentFocused=false;
  out=await api.read('verify',{document:doc,target:{name:'Child'},op:'fill',text:'synthetic'});
  assert(out.ok&&out.verified);assert.equal(out.focused,false);
});

test('option pages use public references while hidden file targets retain native-local pins',async()=>{
  const {api,state}=fixture(),options=await api.read('optionList',{document:doc,target:{name:'Child'}});
  assert(options.ok);assert(options.ref.startsWith('r1|'));assert.equal(options.frame,child);
  const file=await api.read('uploadInfo',{document:doc,target:{name:'Child'}});assert(file.ok&&file.element.hidden);
  assert.deepEqual(file.native_target,{document:local,ref:'e1',frame_chain:[{frame:child,document:fd},{frame:'f0',document:rootFd}]});
  assert(state.calls.some(call=>call.script?.includes('"_upload":true')));
});

test('the exact picker pins its real input in its owning local frame world',async()=>{
  const {api,state}=fixture(),out=await api.read('chooserInfo',{document:doc,chooser:'chooser-fixture'});
  assert(out.ok);assert.equal(out.document,doc);assert.equal(out.element.frame,child);assert(out.element.ref.endsWith('|'+local+'|e7'));
  const expected=[{frame:child,document:fd},{frame:'f0',document:rootFd}];
  assert.deepEqual(out.native_target,{document:local,ref:'e7',frame_chain:expected});
  assert.deepEqual(state.calls.find(row=>row.command==='chooser'&&row.action==='target').frame_chain,expected);
});

test('Android verifies direct and picker file inputs through exact native objects before source delivery',async()=>{
  for(const method of ['uploadInfo','chooserInfo']){
    const f=fixture(null,true),out=await f.api.read(method,{document:doc,target:{name:'Child'},chooser:'chooser-fixture',timeout_ms:3000});assert(out.ok,method);
    const proof=f.state.calls.find(row=>row.action==='file_input');assert(proof);assert.equal(proof.agent,true);
    assert.equal(proof.frame,child);assert.equal(proof.document,fd);assert.equal(proof.local_document,local);
    assert.equal(proof.ref,method==='uploadInfo'?'e1':'e7');assert.deepEqual(proof.frame_chain,out.native_target.frame_chain);
    assert(Number.isInteger(proof.timeout_ms)&&proof.timeout_ms>0&&proof.timeout_ms<=3000);
    assert.equal(f.state.effect,0);assert(!Object.hasOwn(out,'backendNodeId'));
  }
  const f=fixture();assert((await f.api.read('uploadInfo',{document:doc,target:{name:'Child'}})).ok);
  assert(!f.state.calls.some(row=>row.action==='file_input'));
  assert.throws(()=>fixture(null,'true'),error=>error.code==='file_input_context_invalid');
});

test('changed native file metadata, document, reference, ancestry or cancellation withholds the upload target',async()=>{
  for(const mode of ['frame','frame_document','document','ref','flag','missing','failure','parent','cancel']){
    const f=fixture(null,true);let stopped=false;const boundary=Object.assign(Error('Original operation cancelled'),{browserBoundary:{handled:true}});
    if(mode==='cancel')f.state.checkpoint=()=>{if(stopped)throw boundary;};
    f.state.onRead=({method,value,rows})=>{
      if(method!=='file_input')return;
      if(mode==='cancel'){stopped=true;return value;}
      if(mode==='failure')return{ok:false,error:{code:'file_input_unavailable'}};
      if(mode==='parent')rows[0].document='fd-'+guid(9);
      if(mode==='frame'||mode==='frame_document')value[mode]='changed';
      if(mode==='document')value.value.document='changed';if(mode==='ref')value.value.element.ref='e99';
      if(mode==='flag')value.value.element.multiple=false;if(mode==='missing')delete value.value.element.hidden;
      return value;
    };
    const pending=f.api.read('uploadInfo',{document:doc,target:{name:'Child'}});
    if(mode==='cancel')await assert.rejects(pending,error=>error===boundary);
    else{const out=await pending;assert.equal(out.error.code,mode==='failure'?'file_input_unavailable':'upload_target_changed',mode);
      assert.equal(out.error.effect_may_have_occurred,false);assert(!out.native_target);}
    assert.equal(f.state.effect,0);
  }
});

test('a failed Android native target proof stops the actual upload dispatcher before any source SDK access',async()=>{
  const f=fixture(null,true);let sources=0,uploads=0,dispatched=0;
  f.state.onRead=({method})=>method==='file_input'?{ok:false,error:{code:'file_input_unavailable'}}:null;
  const out=await executeFileUpload({params:{action:'set',document:doc,target:{name:'Child'},files:[{path:'work/input.txt'}]},action:{id:'file-proof-before-source'},
    deadline:performance.now()+3000,guard:async()=>({state:{file_input_capability:true,upload_capability:true,navigation:7}}),
    read:(...args)=>f.api.read(...args),api:{async call(){sources++;throw Error('Source access must follow native proof');}},
    view:{async execute(){uploads++;throw Error('No native upload may start');}},setDispatched:()=>dispatched++,
    fail:(code,message)=>({handled:true,result:{ok:false,error:{code,message,effect_may_have_occurred:false}}})});
  assert.equal(out.result.error.code,'file_input_unavailable');assert.equal(sources+uploads+dispatched,0);
});

test('a missing, stale or wrong-root picker never reaches native target resolution',async()=>{
  for(const mode of ['missing','stale','wrong-root']){
    const {api,state}=fixture();
    if(mode==='missing')state.pickers=[];if(mode==='stale')state.pickers[0].frame_document='fd-'+guid(9);
    const out=await api.read('chooserInfo',{document:mode==='wrong-root'?'d-'+guid(9):doc,chooser:'chooser-fixture'});
    assert.equal(out.error.code,mode==='missing'?'chooser_changed':mode==='stale'?'stale_frame':'stale_document');
    assert(!out.error.effect_may_have_occurred);assert(!state.calls.some(row=>row.command==='chooser'&&row.action==='target'));
  }
});

test('picker binding is rejected when its ancestor changes during native resolution',async()=>{
  const {api,state}=fixture();state.onRead=({method,rows})=>{if(method==='chooserTarget')rows[0].document='fd-'+guid(9);};
  const out=await api.read('chooserInfo',{document:doc,chooser:'chooser-fixture'});
  assert.equal(out.error.code,'target_changed');assert(!out.error.effect_may_have_occurred);
});

test('inert iframe parents block hidden direct and picker input before target delivery',async()=>{
  for(const method of ['uploadInfo','chooserInfo']){
    const {api,state}=fixture();state.parentInert=true;
    const out=await api.read(method,{document:doc,target:{name:'Child'},chooser:'chooser-fixture'});
    assert.equal(out.error.code,'element_disabled');assert(!out.error.effect_may_have_occurred);
    assert(!state.calls.some(row=>row.command==='chooser'&&row.action==='target'));
  }
});

test('upload policy revalidates ancestor documents before returning native file pins',async()=>{
  const {api,state}=fixture();state.onRead=({method,rows})=>{if(method==='upload_policy')rows[0].document='fd-'+guid(9);};
  const out=await api.read('uploadInfo',{document:doc,target:{name:'Child'}});
  assert.equal(out.error.code,'stale_frame');assert(!out.error.effect_may_have_occurred);
});

test('a sent mutation retains uncertainty on native error and is never replayed',async()=>{
  const {api,state}=fixture();state.onRead=({method})=>method==='selectOptions'?{ok:false,error:{code:'frame_interrupted',evaluation_may_have_occurred:true}}:null;
  const out=await api.read('selectOptions',{document:doc,target:{name:'Child'},choices:[{index:0}]});
  assert.equal(out.error.code,'frame_interrupted');assert.equal(out.error.effect_may_have_occurred,true);assert.equal(state.effect,1);
});

test('a native pre-evaluation rejection accurately reports no mutation',async()=>{
  const {api,state}=fixture();state.onRead=({method})=>method==='selectOptions'?{ok:false,error:{code:'stale_frame',evaluation_may_have_occurred:false}}:null;
  const out=await api.read('selectOptions',{document:doc,target:{name:'Child'}});
  assert.equal(out.error.code,'stale_frame');assert.equal(out.error.effect_may_have_occurred,false);
});

test('lost mutation delivery is uncertain while caller cancellation preserves its checkpoint',async()=>{
  const first=fixture();first.state.onRead=({method})=>{if(method==='selectOptions')throw Error('Synthetic lost reply');};
  const out=await first.api.read('selectOptions',{document:doc,target:{name:'Child'}});assert(out.error.effect_may_have_occurred);assert.equal(first.state.effect,1);
  const second=fixture(),sentinel=Object.assign(Error('cancelled'),{browserBoundary:{handled:true,result:{ok:false}}});
  second.state.checkpoint=()=>{if(second.state.effect)throw sentinel;};
  await assert.rejects(second.api.read('selectOptions',{document:doc,target:{name:'Child'}}),error=>error===sentinel);assert.equal(second.state.effect,1);
});

test('changed parent identity rejects mapped coordinates before any input exists',async()=>{
  const {api,state}=fixture();state.onRead=({method,rows})=>{if(method==='owner')rows[0].document='fd-'+guid(9);};
  const out=await api.read('prepare',{document:doc,target:{name:'Child'}});
  assert.equal(out.error.code,'stale_frame');assert.equal(out.error.effect_may_have_occurred,false);assert.equal(state.effect,0);
});

test('a native failure packet still checks authoritative cancellation before returning',async()=>{
  const {api,state}=fixture(),boundary=Object.assign(Error('Original action cancelled'),{browserBoundary:{handled:true,result:{ok:false}}});let cancelled=false;
  state.onRead=({method})=>{if(method==='selectOptions'){cancelled=true;return{ok:false,error:{code:'frame_interrupted',evaluation_may_have_occurred:true}};}};
  await assert.rejects(api.read('selectOptions',{document:doc,target:{name:'Child'}},{checkpoint:()=>{if(cancelled)throw boundary;}}),error=>error===boundary);
  assert.equal(state.effect,1);
});

test('the driver accepts only fixed methods and composes exact frame capture masks',async()=>{
  const {api,state}=fixture();assert.equal((await api.read('evaluate',{script:'forged'})).error.code,'frame_method_invalid');assert.equal(state.calls.length,0);
  assert.equal((await api.read('clearFiles',{})).error.code,'frame_method_invalid');assert.equal(state.effect,0);
  const element=await api.read('captureArea',{document:doc,target:{name:'Child'},mode:'element'});assert(element.ok);assert.deepEqual(element.region,{x:120,y:220,width:50,height:40});assert.equal(element.redactions.length,1);
  const root=await api.read('captureArea',{document:doc,mode:'viewport'});assert(root.ok);assert.equal(root.redactions.length,2);assert.equal(root.frame_masks.readable,2);
});


test('original navigation proof constrains every native subread and never enters page arguments',async()=>{
  const f=fixture(),navigation_ref='nv-'+guid(88),out=await f.api.read('observe',{limit:10},{navigation_ref});
  assert.equal(out.ok,true);assert(f.state.calls.length>3);
  for(const row of f.state.calls){assert.equal(row.command,'frames');assert.equal(row.navigation_ref,navigation_ref);
    if(row.script)assert(!row.script.includes(navigation_ref)&&!row.script.includes('navigation_ref'));}
  f.state.calls.length=0;assert((await f.api.read('observe')).ok);assert(f.state.calls.every(row=>row.navigation_ref===undefined));
});
test('malformed navigation references and non-observation constrained methods execute nothing',async()=>{
  for(const navigation_ref of ['',null,4,{},'raw-loader','nv-'+guid(88)]){
    const f=fixture(),out=await f.api.read(navigation_ref==='nv-'+guid(88)?'selectOptions':'observe',{}, {navigation_ref});
    assert.equal(out.error.code,'navigation_unproven');assert.equal(out.error.effect_may_have_occurred,false);assert.equal(f.state.calls.length,0);
  }
});
