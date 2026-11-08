import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import {fixture,createDocumentDriver,doc,fd,ref} from './fixtures/document-driver.mjs';

let source=readFileSync(new URL('../../app/web/js/features/workbench/automation-dispatch.js',import.meta.url),'utf8');
source=source.replace('from "./automation-runtime.js"',`from ${JSON.stringify(new URL('../../app/web/js/features/workbench/automation-runtime.js',import.meta.url).href)}`);
for(const name of ['capture-image','visual-snapshot','visual-dispatch','upload-dispatch','download-dispatch','download-save','script-dispatch'])
  source=source.replace(`from "./${name}.js"`,`from ${JSON.stringify(new URL(`../../app/web/js/features/workbench/${name}.js`,import.meta.url).href)}`);
const {executeBrowserAutomation}=await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

// Couple the production driver and shared dispatcher to a synthetic native
// protocol. These receipts do not certify Android framework input/interception.
function coupled(){
  const f=fixture(),inputs=[],captures=[],captureCancellations=[];let downloads=false,chooser=false,focused=false,text='';
  f.state.hook=({method,options,result})=>{
    if(method==='verify'){result.value.focused=focused;result.value.verified=options.op!=='fill'||text===options.text;}
    return result;
  };
  const execute=async(command,params)=>{
    if(command==='state')return{...await f.execute(command,params),automation:f.state.available,
      browser_use:{available:f.state.available,reason:'isolated_world_unsupported'},frame_target_keyboard:true,
      wheel_input:f.state.wheel===true,
      hover_input:f.state.hover===true,
      capture_capability:f.state.capture===true,
      capture_busy:f.state.captureBusy===true,capture_cleanup_failed:f.state.captureCleanupFailed===true,
      dialog_cleanup_failed:f.state.dialogCleanupFailed===true,
      popup_cleanup_failed:f.state.popupCleanupFailed===true,
      download_capability:true,downloads_armed:downloads,chooser_capability:true,chooser_upload_capability:true,chooser_armed:chooser};
    if(command==='downloads'){if(params.action==='arm')downloads=true;return{ok:true,armed:downloads,downloads:[]};}
    if(command==='chooser'){if(params.action==='arm')chooser=true;return{ok:true,armed:chooser,choosers:[]};}
    if(command==='capture'){
      captures.push(params);if(typeof f.state.captureReply==='function')return f.state.captureReply(params);
      return{ok:true,data:f.state.captureData,scale:1,capture_key:params.capture_key,document:params.capture_context.document,
        native_document:params.capture_context.native_document,navigation:params.capture_context.navigation};
    }
    if(command==='capture_cancel'){captureCancellations.push(params);return{cancelled:true};}
    if(command==='input'){
      assert(downloads&&chooser,'Native input preceded interception acknowledgement');
      assert.equal(params.input_document,f.state.expectedNativeDocument||doc);assert.equal(params.input_ref,'e7');assert.equal(params.navigation,7);
      assert.deepEqual(params.frame_chain,f.state.expectedChain||[{frame:'f0',document:fd}]);
      if(['focus','key','text'].includes(params.kind)){assert.equal(params.focus_document,doc);assert.equal(params.focus_ref,'e7');}
      inputs.push(params);if(typeof f.state.inputReply==='function')return f.state.inputReply(params,inputs);
      if(params.kind==='focus')focused=true;if(params.kind==='text')text=params.text;
      return{accepted:true,target_focused:focused,trusted_input:params.kind!=='focus'};
    }
    return f.execute(command,params);
  };
  const view={execute},pageDriver=createDocumentDriver({execute});
  const run=(command,params,other={})=>executeBrowserAutomation({command,params,view,pageDriver,browser:view,
    action:{id:'synthetic-main-document'},api:{async call(method){assert.equal(method,'ui.result');return{state:'pending'};}},...other});
  return{...f,inputs,captures,captureCancellations,run};
}

test('shared observation and waits consume the qualified main-document protocol',async()=>{
  const f=coupled(),observed=await f.run('observe',{});assert(observed.result.ok);assert.equal(observed.result.elements[0].ref,ref);
  assert.equal(observed.result.frame_scope,'main_document');
  const waited=await f.run('wait_for',{document:doc,until:'visible',target:{ref}});assert(waited.result.matched);
  assert.equal(f.inputs.length,0);
});

test('shared click, fill and key carry the same exact native document and isolated target',async()=>{
  for(const params of [{op:'click'},{op:'fill',text:'中文🙂'},{op:'key',key:'Enter'}]){
    const f=coupled(),out=await f.run('act',{document:doc,target:{ref},...params});assert(out.result.ok,JSON.stringify(out.result));
    assert.deepEqual(f.inputs.map(row=>row.kind),params.op==='click'?['click']:params.op==='fill'?['focus','key','text']:['focus','key']);
    assert.equal(f.state.calls.filter(row=>row.command==='evaluate'||row.command==='click'||row.command==='type').length,0);
  }
});

test('unsupported Android input kinds and unavailable automation stop before native work',async()=>{
  for(const op of ['hover','scroll']){
    const f=coupled(),out=await f.run('act',{op,document:doc,target:{ref},dy:100});
    assert.equal(out.result.error.code,'unsupported_capability');assert.equal(out.result.error.effect_may_have_occurred,false);
    assert.equal(f.inputs.length,0);assert.equal(f.state.calls.filter(row=>row.command==='automation').length,0);
  }
  const f=coupled();f.state.available=false;const out=await f.run('observe',{});
  assert.equal(out.result.error.code,'unsupported_capability');assert.equal(f.inputs.length,0);
  assert.equal(f.state.calls.filter(row=>row.command==='automation').length,0);
});

test('context cancellation during preparation never dispatches the partially prepared target',async()=>{
  const f=coupled();let alive=true;f.state.hook=({method,result})=>{if(method==='prepare')alive=false;return result;};
  const out=await f.run('act',{document:doc,target:{ref},op:'click'},{alive:()=>alive});
  assert.equal(out.result.error.code,'context_changed');assert.equal(out.result.error.effect_may_have_occurred,false);
  assert.equal(f.inputs.length,0);
});
test('Android viewport and target scrolling pin a root hit and send one native wheel without a focus click',async()=>{
  for(const target of [undefined,{ref}]){
    const f=coupled();f.state.wheel=true;const out=await f.run('act',{op:'scroll',document:doc,target,dx:97,dy:131});
    assert(out.result.ok,JSON.stringify(out.result));assert.deepEqual(f.inputs.map(row=>row.kind),['wheel']);
    assert.equal(f.inputs[0].dx,97);assert.equal(f.inputs[0].dy,131);
    assert(!Object.hasOwn(f.inputs[0],'focus_ref'));assert.equal(f.state.calls.filter(row=>row.method==='scrollPoint').length,1);
  }
  const f=coupled();f.state.wheel=true;
  f.state.hook=({method,result})=>method==='scrollPoint'?{...result,value:{ok:false,error:{code:'android_frames_unavailable'}}}:result;
  const out=await f.run('act',{op:'scroll',document:doc,dx:0,dy:100});assert.equal(out.result.error.code,'android_frames_unavailable');assert.equal(f.inputs.length,0);
});

test('Android hover sends one pinned native move with no focus, click or touch',async()=>{
  const f=coupled();f.state.hover=true;
  const out=await f.run('act',{op:'hover',document:doc,target:{ref}});assert(out.result.ok,JSON.stringify(out.result));
  assert.deepEqual(f.inputs.map(row=>row.kind),['move']);assert.equal(f.inputs[0].x,20);assert.equal(f.inputs[0].y,30);
  assert(!Object.hasOwn(f.inputs[0],'focus_ref'));
  const preparations=f.state.calls.filter(row=>row.method==='prepare');assert(preparations.length>=2);
  assert(preparations.every(row=>row.options.op==='hover'));
});

test('Android scroll dispatch retains a child document and complete chain instead of substituting the root',async()=>{
  const f=coupled(),local='d-00000000-0000-4000-8000-000000000002',child='f-00000000-0000-4000-8000-000000000002',childDoc='fd-00000000-0000-4000-8000-000000000002';
  f.state.wheel=true;f.state.expectedNativeDocument=local;f.state.expectedChain=[{frame:child,document:childDoc},{frame:'f0',document:fd}];
  const pageDriver={async read(method,options,control){if(method!=='scrollPoint')return f.driver.read(method,options,control);
    return{ok:true,document:doc,point:options.point,hit_ref:`r1|${child}|${childDoc}|${local}|e7`,native_target:{document:local,ref:'e7'},frame_chain:f.state.expectedChain};}};
  const out=await f.run('act',{op:'scroll',document:doc,dx:11,dy:70},{pageDriver});
  assert(out.result.ok,JSON.stringify(out.result));assert.equal(f.inputs.length,1);assert.equal(f.inputs[0].kind,'wheel');
  assert(!Object.hasOwn(f.inputs[0],'focus_ref'));assert.deepEqual(f.inputs[0].frame_chain,f.state.expectedChain);
});

test('Android scroll rejects incomplete, inconsistent or changed private pins before sending any wheel',async()=>{
  for(const mode of ['root-document','missing-root','duplicate','wrong-ref','moved-point','nan-point']){
    const f=coupled();f.state.wheel=true;
    const pageDriver={async read(method,options,control){const out=await f.driver.read(method,options,control);if(method!=='scrollPoint'||!out.ok)return out;
      if(mode==='root-document')out.document='d-00000000-0000-4000-8000-000000000009';
      if(mode==='missing-root')out.frame_chain[0].frame='f-00000000-0000-4000-8000-000000000002';
      if(mode==='duplicate')out.frame_chain.unshift({...out.frame_chain[0]});
      if(mode==='wrong-ref')out.hit_ref+='forged';
      if(mode==='moved-point')out.point={x:options.point.x+1,y:options.point.y};
      if(mode==='nan-point')out.point={x:NaN,y:options.point.y};return out;}};
    const out=await f.run('act',{op:'scroll',document:doc,dx:0,dy:70},{pageDriver});
    assert.equal(out.result.error.code,'frame_response_invalid',mode);assert.equal(out.result.error.effect_may_have_occurred,false);assert.equal(f.inputs.length,0);
  }
});

test('Android hover refuses target drift and cancelled preparation before native movement',async()=>{
  for(const mode of ['moved','cancelled']){
    const f=coupled();f.state.hover=true;let count=0,alive=true;
    f.state.hook=({method,result})=>{
      if(method==='prepare'&&++count===3){
        if(mode==='moved'){result.value.point.x+=10;result.value.geometry[4]+=10;}else alive=false;
      }
      return result;
    };
    const out=await f.run('act',{op:'hover',document:doc,target:{ref}},{alive:()=>alive});
    assert.equal(out.result.error.code,mode==='moved'?'target_changed':'context_changed');
    assert.equal(out.result.error.effect_may_have_occurred,false);assert.equal(f.inputs.length,0);
  }
});

const nativeRefusal=(code,overrides={})=>({accepted:false,trusted_input:false,target_focused:false,
  effect_may_have_occurred:false,cleanup_confirmed:true,delivered_steps:0,error:{code},...overrides});

test('native guard refusal retains its cause and confirmed zero effects without replay',async()=>{
  for(const op of ['click','scroll']){
    const f=coupled();f.state.wheel=true;
    f.state.inputReply=()=>nativeRefusal('input_target_changed',{error:{code:'input_target_changed',message:'page-provided exception must not escape'}});
    const out=await f.run('act',{op,document:doc,target:{ref},dy:100});
    assert.equal(out.result.error.code,'input_target_changed');assert.equal(out.result.error.effect_may_have_occurred,false);
    assert.deepEqual(out.result.error.native_input,{kind:op==='scroll'?'wheel':'click',effect_may_have_occurred:false,cleanup_confirmed:true,delivered_steps:0});
    assert(!JSON.stringify(out).includes('page-provided'));assert.equal(f.inputs.length,1);
    f.state.inputReply=null;
    const next=await f.run('act',{op,document:doc,target:{ref},dy:100});assert(next.result.ok);
    assert.equal(f.inputs.length,2,'Only the independently requested next action may send more input');
  }
});

test('a later native refusal cannot erase effects from earlier focus or selection',async()=>{
  for(const refusedKind of ['key','text']){
    const f=coupled(),hook=f.state.hook;
    f.state.hook=args=>{const result=hook(args);if(args.method==='verify')result.value.focused=true;return result;};
    f.state.inputReply=params=>params.kind===refusedKind?nativeRefusal('input_focus_lost'):
      {accepted:true,target_focused:params.kind==='focus',trusted_input:params.kind!=='focus'};
    const out=await f.run('act',{op:'fill',document:doc,target:{ref},text:'must not be replayed'});
    assert.equal(out.result.error.code,'input_focus_lost');assert.equal(out.result.error.effect_may_have_occurred,true);
    assert.equal(out.result.error.native_input.kind,refusedKind);assert.equal(out.result.error.native_input.effect_may_have_occurred,false);
    assert.deepEqual(f.inputs.map(row=>row.kind),refusedKind==='key'?['focus','key']:['focus','key','text']);
  }
});

test('partial native input preserves its cause, cleanup and delivered-step receipt',async()=>{
  const f=coupled();f.state.inputReply=()=>nativeRefusal('input_cleanup_failed',{
    effect_may_have_occurred:true,cleanup_confirmed:false,delivered_steps:1});
  const out=await f.run('act',{op:'click',document:doc,target:{ref}});
  assert.equal(out.result.error.code,'input_cleanup_failed');assert.equal(out.result.error.effect_may_have_occurred,true);
  assert.deepEqual(out.result.error.native_input,{kind:'click',effect_may_have_occurred:true,cleanup_confirmed:false,delivered_steps:1});
  assert.equal(f.inputs.length,1);
});

test('malformed or untrusted native replies retain conservative input uncertainty',async()=>{
  const valid=nativeRefusal('input_target_changed');
  for(const reply of [null,{}, {...valid,accepted:true}, {...valid,effect_may_have_occurred:undefined},
      {...valid,cleanup_confirmed:undefined},{...valid,delivered_steps:1},{...valid,delivered_steps:-1},
      {...valid,delivered_steps:1.5},{...valid,delivered_steps:2147483648},
      {...valid,error:{code:'unbounded\npage text'}},{...valid,error:{code:'a'.repeat(65)}}]){
    const f=coupled();f.state.inputReply=()=>reply;
    const out=await f.run('act',{op:'click',document:doc,target:{ref}});
    assert.equal(out.result.error.code,'action_uncertain');assert.equal(out.result.error.effect_may_have_occurred,true);
    assert(!Object.hasOwn(out.result.error,'native_input'));assert.equal(f.inputs.length,1);
  }
});

test('unconfirmed cleanup and lost transport cannot claim zero effects or replay input',async()=>{
  const f=coupled();f.state.inputReply=()=>nativeRefusal('input_cleanup_failed',{cleanup_confirmed:false});
  const out=await f.run('act',{op:'click',document:doc,target:{ref}});
  assert.equal(out.result.error.code,'input_cleanup_failed');assert.equal(out.result.error.effect_may_have_occurred,true);
  assert.equal(out.result.error.native_input.cleanup_confirmed,false);assert.equal(f.inputs.length,1);
  const lost=coupled();lost.state.inputReply=()=>{throw Error('Native acknowledgement lost');};
  const uncertain=await lost.run('act',{op:'click',document:doc,target:{ref}});
  assert.equal(uncertain.result.error.code,'action_uncertain');assert.equal(uncertain.result.error.effect_may_have_occurred,true);
  assert.equal(lost.inputs.length,1);
});

async function captureFixture(run){
  const previous={document:globalThis.document,bitmap:globalThis.createImageBitmap},png=new Uint8Array(33),state={masks:[],decodes:0,closed:0};
  png.set([137,80,78,71,13,10,26,10]);new DataView(png.buffer).setUint32(16,320);new DataView(png.buffer).setUint32(20,240);
  state.data=Buffer.from(png).toString('base64');
  globalThis.createImageBitmap=async()=>{state.decodes++;return{close(){state.closed++;}};};
  globalThis.document={createElement(){return{width:0,height:0,getContext:()=>({drawImage(){},fillRect(...box){state.masks.push(box);}}),
    toDataURL:()=>`data:image/png;base64,${state.data}`};}};
  try{await run(state);}finally{globalThis.document=previous.document;globalThis.createImageBitmap=previous.bitmap;}
}

test('shared Android capture carries the original context and masks opaque children before publishing',async()=>{
  await captureFixture(async pixels=>{
    const f=coupled();f.state.capture=true;f.state.captureData=pixels.data;
    const out=await f.run('capture',{document:doc});assert(out.result.ok,JSON.stringify(out));
    assert.equal(f.captures.length,1);assert.equal(f.inputs.length,0);assert.equal(f.captures[0].navigation,7);
    assert.deepEqual(f.captures[0].capture_context,{adapter:'android-main-document-v1',native_document:fd,navigation:7,document:doc,
      scroll:{x:0,y:0},viewport:{width:320,height:240},device_pixel_ratio:1});
    assert.equal(pixels.masks.length,4);assert.equal(out.result.redacted_regions,2);assert.equal(pixels.decodes,1);assert.equal(pixels.closed,1);
    assert.equal(out.result.frame_scope,'main_document');assert(out.result.limitations.includes('child_frames_masked'));
    assert(!Object.hasOwn(out.result,'visual_reference'));assert(!JSON.stringify(out.result).includes('native_capture'));
  });
});

test('changed Android native screenshot identities reject before any bitmap decode',async()=>{
  await captureFixture(async pixels=>{
    for(const field of ['native_document','navigation','document','capture_key']){
      const f=coupled();f.state.capture=true;
      f.state.captureReply=params=>({data:pixels.data,scale:1,native_document:fd,navigation:7,document:doc,capture_key:params.capture_key,[field]:'replaced'});
      const out=await f.run('capture',{document:doc});assert.equal(out.result.error.code,'capture_changed',field);
      assert.equal(f.captures.length,1);assert.equal(pixels.decodes,0);assert.equal(f.inputs.length,0);
    }
  });
});

test('Android opaque frame changes and cancellation discard native pixels without replay',async()=>{
  await captureFixture(async pixels=>{
    const f=coupled();f.state.capture=true;
    f.state.captureReply=params=>{
      f.state.hook=({method,result})=>{if(method==='captureArea')result.value.redactions[1].frame_ref='e9';return result;};
      return{ok:true,data:pixels.data,scale:1,native_document:fd,navigation:7,document:doc,capture_key:params.capture_key};
    };
    const changed=await f.run('capture',{document:doc});assert.equal(changed.result.error.code,'capture_changed');assert.equal(f.captures.length,1);
    const cancelled=coupled();cancelled.state.capture=true;let alive=true,complete;
    cancelled.state.captureReply=()=>{alive=false;return new Promise(resolve=>{complete=resolve;});};
    const out=await cancelled.run('capture',{document:doc},{alive:()=>alive});assert.equal(out.result.error.code,'context_changed');
    assert.equal(cancelled.captures.length,1);assert.deepEqual(cancelled.captureCancellations,[{capture_key:'synthetic-main-document'}]);
    complete({data:pixels.data});await new Promise(resolve=>setTimeout(resolve,1));
    assert.equal(pixels.decodes,0);assert.equal(cancelled.captures.length,1);
  });
});

test('native Android capture failures preserve bounded causes without decoding or exposing exceptions',async()=>{
  await captureFixture(async pixels=>{
    for(const variant of ['capture_timed_out','capture_cleanup_failed','capture_byte_limit','malformed']){
      const f=coupled();f.state.capture=true;
      f.state.captureReply=params=>({ok:false,native_document:fd,navigation:7,document:doc,capture_key:params.capture_key,
        error:{code:variant==='malformed'?'bad\npage exception':variant,message:'Private native exception'}});
      const out=await f.run('capture',{document:doc});assert.equal(out.result.error.code,variant==='malformed'?'capture_failed':variant);
      assert.equal(out.result.error.effect_may_have_occurred,false);assert(!JSON.stringify(out).includes('Private native'));
      assert.equal(f.captures.length,1);assert.equal(pixels.decodes,0);
    }
  });
});

test('native screenshot reservations and cleanup faults refuse new input before target preparation',async()=>{
  for(const field of ['captureBusy','captureCleanupFailed']){
    const f=coupled();f.state[field]=true;
    const out=await f.run('act',{op:'click',document:doc,target:{ref}});
    assert.equal(out.result.error.code,field==='captureBusy'?'browser_busy':'capture_cleanup_failed');
    assert.equal(out.result.error.effect_may_have_occurred,false);assert.equal(f.inputs.length,0);
    assert(!f.state.calls.some(row=>row.command==='automation'));assert.equal(f.captures.length,0);
  }
});

test('uncertain native dialog cleanup refuses effects before document preparation',async()=>{
  for(const command of ['act','capture','navigate_to']){
    const f=coupled();f.state.dialogCleanupFailed=true;f.state.capture=true;
    const out=await f.run(command,{op:'click',document:doc,target:{ref},action:'goto',url:'https://fixture.invalid/'});
    assert.equal(out.result.ok,false);assert.equal(out.result.error.code,'dialog_cleanup_failed');
    assert.equal(out.result.error.effect_may_have_occurred,false);assert.equal(f.inputs.length,0);assert.equal(f.captures.length,0);
    assert(!f.state.calls.some(row=>row.command==='automation'));
  }
});

test('dialog cleanup faults still allow read-only observation without native input',async()=>{
  const f=coupled();f.state.dialogCleanupFailed=true;
  const out=await f.run('observe',{});assert.equal(out.result.ok,true);
  assert.equal(f.inputs.length,0);assert.equal(f.captures.length,0);
});

test('uncertain popup transport refuses further effects before preparing the document',async()=>{
  for(const command of ['act','capture','navigate_to']){
    const f=coupled();f.state.popupCleanupFailed=true;f.state.capture=true;
    const out=await f.run(command,{op:'click',document:doc,target:{ref},action:'goto',url:'https://fixture.invalid/'});
    assert.equal(out.result.ok,false);assert.equal(out.result.error.code,'popup_cleanup_failed');
    assert.equal(out.result.error.effect_may_have_occurred,false);assert.equal(f.inputs.length,0);assert.equal(f.captures.length,0);
    assert(!f.state.calls.some(row=>row.command==='automation'));
  }
  const f=coupled();f.state.popupCleanupFailed=true;
  assert.equal((await f.run('observe',{})).result.ok,true);
  assert.equal(f.inputs.length,0);assert.equal(f.captures.length,0);
});
