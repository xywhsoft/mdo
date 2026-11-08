import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';

let source=await readFile(new URL('../../app/web/js/features/workbench/automation-dispatch.js',import.meta.url),'utf8');
for(const [name,path] of [['executeDownloadSave, downloadSaveState','download-save.js'],['visualSnapshots, hashVisualRaster','visual-snapshot.js'],['executeVisualAction','visual-dispatch.js']]){
  source=source.replace(`import { ${name} } from "./${path}";`,`import {${name}} from ${JSON.stringify(new URL('../../app/web/js/features/workbench/'+path,import.meta.url).href)};`);
}
source=source.replace('import { browserRuntime } from "./automation-runtime.js";','function browserRuntime(){}')
  .replace('import { prepareScreenshot } from "./capture-image.js";',`async function prepareScreenshot(native,area,after,params,inspect={}){
    globalThis.__captureSettlingRenders++;const {width,height}=area.region;
    if(inspect.raster)await inspect.raster({width,height,getContext:()=>({getImageData:(x,y,w,h)=>({data:new Uint8ClampedArray(w*h*4)})})});
    return{image:{width,height,bytes:12},redaction:'fixture'}}`)
  .replace('import { executeFileUpload } from "./upload-dispatch.js";','async function executeFileUpload(){throw Error("Unexpected upload")}')
  .replace('import { executeBrowserDownload } from "./download-dispatch.js";','async function executeBrowserDownload(){throw Error("Unexpected download")}');
const {executeBrowserAutomation}=await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const doc='d-00000000-0000-4000-8000-000000000001';
const area=(width=100,change={})=>({ok:true,document:doc,device_pixel_ratio:1,region:{x:0,y:0,width,height:100},redactions:[],
  frame_stamp:JSON.stringify([['f0',change.frame||'fd-original',null,false,true,change.mask||'readable',doc,{x:0,y:0},{width,height:100},null]]),...change});

function fixture(areas,mode=''){
  const state={reads:[],captures:[],navigation:'1',receipt:'pending',manual:false};globalThis.__captureSettlingRenders=0;
  const params={timeout_ms:2000};
  const args={command:'capture',params,browser:'capture-settling-'+mode,action:{id:'settle-'+mode},
    api:{async call(){return{state:state.receipt};}},pageDriver:{async read(method,options){assert.equal(method,'captureArea');state.reads.push({...options});
      const value=areas[Math.min(state.reads.length-1,areas.length-1)];
      if(state.reads.length===2){if(mode==='cancel')state.receipt='cancelled';if(mode==='manual')state.manual=true;if(mode==='navigation')state.navigation='2';}
      return value;
    }},view:{async execute(command,options){
      if(command==='state')return{loading:false,automation:true,capture_capability:true,navigation:state.navigation,manual:state.manual};
      if(command==='capture'){state.captures.push(options);return{data:'private-invalidated-native-capture',
        ...(options.capture_context?{ok:true,document:options.capture_context.document,native_document:options.capture_context.native_document,
          navigation:options.capture_context.navigation,capture_key:options.capture_key}:{}),
        ...(state.sourceFormat!==undefined?{format:state.sourceFormat}:{}),...state.captureReply};}
      throw Error('Unexpected native operation '+command);
    }}};
  return{args,state,run:()=>executeBrowserAutomation(args)};
}

test('one viewport resize retries the read-only capture and publishes only its stable raster',async()=>{
  const f=fixture([area(),area(120),area(120),area(120)]),out=await f.run();
  assert(out.result.ok);assert.equal(out.result.image.width,120);assert.equal(f.state.captures.length,2);assert.equal(globalThis.__captureSettlingRenders,1);
  assert(f.state.captures.every(row=>row.capture_key==='settle-'&&row.navigation==='1'));
  assert(f.state.reads.slice(1).every(row=>row.document===doc));assert(!JSON.stringify(out).includes('private'));
});

test('continuously resizing viewports stop after three captures without publishing partial pixels',async()=>{
  const f=fixture([area(),area(120),area(120),area(140),area(140),area(160)]),out=await f.run();
  assert.equal(out.result.error.code,'capture_changed');assert.equal(out.result.error.effect_may_have_occurred,false);
  assert.equal(f.state.captures.length,3);assert.equal(globalThis.__captureSettlingRenders,0);assert.equal(out.result.image,undefined);
});

test('document, privacy, scroll and DPR changes never become viewport-settling retries',async()=>{
  for(const mode of ['document','privacy','scroll','dpr']){
    const after=area(120,mode==='document'?{frame:'fd-replaced'}:mode==='privacy'?{mask:'new-private-mask'}:mode==='dpr'?{device_pixel_ratio:2}:{});
    if(mode==='scroll'){const rows=JSON.parse(after.frame_stamp);rows[0][7].y=10;after.frame_stamp=JSON.stringify(rows);}
    const f=fixture([area(),after],mode),out=await f.run();assert.equal(out.result.error.code,'capture_changed',mode);
    assert.equal(f.state.captures.length,1);assert.equal(globalThis.__captureSettlingRenders,0);assert(!JSON.stringify(out).includes('new-private-mask'));
  }
});

test('cancel, takeover, navigation and deadline interrupts stop a settling capture before its next attempt',async()=>{
  for(const mode of ['cancel','manual','navigation','deadline']){
    const f=fixture([area(),area(120)],mode);if(mode==='deadline')f.args.params.timeout_ms=40;
    const out=await f.run();assert.equal(out.result.error.code,{cancel:'cancelled',manual:'manual_control',navigation:'stale_document',deadline:'timed_out'}[mode]);
    assert.equal(f.state.captures.length,1);assert.equal(f.state.reads.length,2);assert.equal(globalThis.__captureSettlingRenders,0);
  }
});

test('a settled retry cannot rebind the original screenshot to a different document',async()=>{
  const f=fixture([area(),area(120),area(120,{document:'d-replaced'})]),out=await f.run();
  assert.equal(out.result.error.code,'stale_document');assert.equal(f.state.captures.length,1);assert.equal(globalThis.__captureSettlingRenders,0);
});

test('public screenshot codecs retain a separate bounded lossless native source',async()=>{
  for(const format of ['png','jpeg']){
    const f=fixture([area(),area()],format);f.args.params.format=format;f.args.params.max_bytes=8192;
    const out=await f.run();assert(out.result.ok);assert.equal(f.state.captures.length,1);
    assert.equal(f.state.captures[0].format,'png');assert.equal(f.state.captures[0].max_bytes,262144);
    assert.equal(f.args.params.format,format);assert.equal(f.args.params.max_bytes,8192);
  }
});

test('only a complete native frame adapter can issue Android visual references',async()=>{
  for(const adapter of ['android-native-frames-v1','android-main-document-v1']){
    const first=area(),context={adapter,native_document:'fd-00000000-0000-4000-8000-000000000001',document:doc,navigation:1,
      viewport:{width:100,height:100},scroll:{x:0,y:0},device_pixel_ratio:1,frame_stamp:first.frame_stamp};
    first.native_capture=context;first.frame_scope=adapter==='android-native-frames-v1'?'native-frame-tree':'main-document';
    first.limitations=['android-visible-viewport-only'];
    const f=fixture([first,first],adapter);f.state.navigation=1;
    const out=await f.run();assert(out.result.ok);assert.deepEqual(f.state.captures[0].capture_context,context);
    assert.equal(Boolean(out.result.visual_reference),adapter==='android-native-frames-v1');
    assert.equal(out.result.frame_scope,first.frame_scope);assert(!JSON.stringify(out.result).includes('native_capture'));
  }
});

test('Android native screenshot errors and mismatched echoes cannot reach the shared raster renderer',async()=>{
  for(const mode of ['failed','navigation','document','capture_key']){
    const first=area();first.native_capture={adapter:'android-native-frames-v1',native_document:'fd-00000000-0000-4000-8000-000000000001',document:doc,navigation:1};
    const f=fixture([first,first],mode);f.state.navigation=1;
    f.state.captureReply=mode==='failed'?{ok:false,error:{code:'capture_changed'}}:{[mode]:'changed'};
    const out=await f.run();assert.equal(out.result.error.code,'capture_changed');assert.equal(globalThis.__captureSettlingRenders,0);
    assert(!Object.hasOwn(out.result,'image'));assert.equal(out.result.error.effect_may_have_occurred,false);
  }
});

test('a backend cannot silently substitute a lossy or unknown screenshot source',async()=>{
  for(const format of ['jpeg','webp','',123]){
    const f=fixture([area(),area()]);f.state.sourceFormat=format;
    const out=await f.run();assert.equal(out.result.error.code,'capture_source_invalid');
    assert.equal(out.result.error.effect_may_have_occurred,false);assert.equal(globalThis.__captureSettlingRenders,0);
    assert.equal(f.state.captures.length,1);assert.equal(f.state.reads.length,1);
  }
});
