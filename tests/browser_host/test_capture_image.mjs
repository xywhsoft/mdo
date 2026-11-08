import assert from "node:assert/strict";
import { test } from "node:test";
import { prepareScreenshot } from "../../app/web/js/features/workbench/capture-image.js";
import { readFile } from 'node:fs/promises';
import { webcrypto } from 'node:crypto';
import vm from 'node:vm';

test("invalid and oversized image envelopes are rejected before bitmap decoding",async()=>{
  let decodes=0;const previous=globalThis.createImageBitmap;globalThis.createImageBitmap=()=>{decodes++;throw Error("Must not decode oversized images");};
  try{
    await assert.rejects(prepareScreenshot({data:"!"},{},{},{format:"png"}),/invalid byte envelope/);
    const png=new Uint8Array(33);png.set([137,80,78,71,13,10,26,10]);
    const view=new DataView(png.buffer);view.setUint32(16,65535);view.setUint32(20,65535);
    await assert.rejects(prepareScreenshot({data:Buffer.from(png).toString("base64")},{},{},{format:"png"}),/raster budget/);
    assert.equal(decodes,0);
  }finally{globalThis.createImageBitmap=previous;}
});

test('visual raster inspection runs after private masks and releases its bitmap on failure',async()=>{
  const previous={document:globalThis.document,bitmap:globalThis.createImageBitmap};let closed=0,inspected=0;
  const png=new Uint8Array(33);png.set([137,80,78,71,13,10,26,10]);new DataView(png.buffer).setUint32(16,100);new DataView(png.buffer).setUint32(20,50);
  const encoded=Buffer.from(png).toString('base64');
  globalThis.createImageBitmap=async()=>({close(){closed++;}});
  globalThis.document={createElement(){const calls=[],context={drawImage(){calls.push('image');},fillRect(){calls.push('mask');}};
    return{width:0,height:0,getContext:()=>context,toDataURL:()=>`data:image/png;base64,${encoded}`,calls};}};
  const area={region:{x:0,y:0,width:100,height:50},redactions:[{x:10,y:10,width:20,height:10}],device_pixel_ratio:1};
  try{
    const image=await prepareScreenshot({data:encoded,scale:1},area,area,{}, {raster:async canvas=>{
      inspected++;assert.deepEqual(canvas.calls,['image','mask','mask']);assert.equal(canvas.width,100);assert.equal(canvas.height,50);
    }});
    assert.equal(image.redacted_regions,1);assert.equal(inspected,1);assert.equal(closed,1);assert(!('hashes'in image));
    await assert.rejects(prepareScreenshot({data:encoded,scale:1},area,area,{}, {raster:async()=>{throw Error('Synthetic raster rejection');}}),/Synthetic raster rejection/);
    assert.equal(closed,2);
  }finally{globalThis.document=previous.document;globalThis.createImageBitmap=previous.bitmap;}
});

test('rendered private controls remain masked when inert, aria-hidden or CSS overrides hidden',async()=>{
  const source=(await readFile(new URL('../../app/web/js/features/workbench/automation-runtime.js',import.meta.url),'utf8')).replace('export function browserRuntime(','function browserRuntime(');
  const document={defaultView:{getComputedStyle:()=>({display:'block',visibility:'visible',opacity:'1'})}},controls=[];
  for(let index=0;index<3;index++)controls.push({ownerDocument:document,isConnected:true,localName:'input',type:'text',
    closest:()=>({semanticHidden:true}),getAttribute:()=>null,hasAttribute:()=>false,
    getClientRects:()=>[{}],getBoundingClientRect:()=>({x:20,y:20+index*40,width:100,height:24})});
  document.createTreeWalker=()=>{let index=0;return{nextNode:()=>controls[index++]||null};};
  const context=vm.createContext({document,crypto:{getRandomValues:webcrypto.getRandomValues.bind(webcrypto)},performance,
    visualViewport:{scale:1},innerWidth:800,innerHeight:600,scrollX:0,scrollY:0,devicePixelRatio:1});
  const runtime=vm.runInContext('('+source+')',context)(),result=runtime.captureArea({mode:'viewport'});
  assert(result.ok);assert.equal(result.redactions.length,3);
  for(let index=0;index<3;index++)assert.equal(result.redactions[index].y,20+index*40);
});

test('local capture masks bind exact iframe nodes without entering child documents',async()=>{
  const source=(await readFile(new URL('../../app/web/js/features/workbench/automation-runtime.js',import.meta.url),'utf8')).replace('export function browserRuntime(','function browserRuntime(');
  const document={defaultView:{getComputedStyle:()=>({display:'block',visibility:'visible',opacity:'1'})}},frames=[];let childReads=0;
  for(let index=0;index<2;index++)frames.push({ownerDocument:document,isConnected:true,localName:'iframe',
    getAttribute:()=>null,hasAttribute:()=>false,getClientRects:()=>[{}],getBoundingClientRect:()=>({x:20+index*340,y:30,width:300,height:150}),
    get contentDocument(){childReads++;throw Error('Local capture must not inspect a child document');}});
  document.createTreeWalker=()=>{let index=0;return{nextNode:()=>frames[index++]||null};};
  const context=vm.createContext({document,crypto:{getRandomValues:webcrypto.getRandomValues.bind(webcrypto)},performance,
    visualViewport:{scale:1},innerWidth:800,innerHeight:600,scrollX:0,scrollY:0,devicePixelRatio:1});
  const factory=vm.runInContext('('+source+')',context),runtime=factory({local_frame:true}),area=runtime.captureArea({mode:'viewport'});
  assert(area.ok);assert.equal(childReads,0);assert.equal(area.visited,2);assert.equal(area.redactions.length,2);
  const first=runtime.pinCaptureFrame(frames[0]),second=runtime.pinCaptureFrame(frames[1]);
  assert(first.ok&&second.ok);assert.equal(first.document,area.document);assert.notEqual(first.ref,second.ref);
  assert.equal(area.redactions[0].frame_ref,first.ref);assert.equal(area.redactions[1].frame_ref,second.ref);
  assert.equal(runtime.pinCaptureFrame({isConnected:true,localName:'iframe',ownerDocument:{}}).error.code,'frame_owner_unavailable');
  assert.equal(factory().pinCaptureFrame(frames[0]).error.code,'frame_owner_unavailable');
  const bounded=runtime.captureArea({mode:'viewport',_max_nodes:1});assert(!bounded.ok);assert.equal(bounded.error.code,'capture_preparation_failed');
});

test('native viewport capture admits an initial fit and masks opaque media without weakening ordinary capture',async()=>{
  const source=(await readFile(new URL('../../app/web/js/features/workbench/automation-runtime.js',import.meta.url),'utf8')).replace('export function browserRuntime(','function browserRuntime(');
  const document={defaultView:{getComputedStyle:()=>({display:'block',visibility:'visible',opacity:'1'})}},nodes=[];
  for(const tag of ['video','object','embed','input'])nodes.push({ownerDocument:document,isConnected:true,localName:tag,type:'text',
    getAttribute:()=>null,hasAttribute:()=>false,getClientRects:()=>[{}],getBoundingClientRect:()=>({x:10,y:20+nodes.length*30,width:80,height:20})});
  document.createTreeWalker=()=>{let index=0;return{nextNode:()=>nodes[index++]||null};};
  const context=vm.createContext({document,crypto:{getRandomValues:webcrypto.getRandomValues.bind(webcrypto)},performance,
    visualViewport:{width:320,height:240,scale:.5,offsetLeft:0,offsetTop:0},innerWidth:320,innerHeight:240,scrollX:0,scrollY:0,devicePixelRatio:2});
  const runtime=vm.runInContext('('+source+')',context)({local_frame:true}),options={mode:'viewport',_native_viewport:true,_opaque_media:true};
  const out=runtime.captureArea(options);assert(out.ok);assert.equal(out.redactions.length,4);
  assert.equal(runtime.captureArea({mode:'viewport'}).error.code,'unsupported_geometry');
  context.visualViewport.scale=1;assert.equal(runtime.captureArea({mode:'viewport'}).redactions.length,1);
  context.visualViewport.offsetTop=1;assert.equal(runtime.captureArea(options).error.code,'unsupported_geometry');
  context.visualViewport.offsetTop=0;context.visualViewport.width=160;assert.equal(runtime.captureArea(options).error.code,'unsupported_geometry');
});

test('lossless native pixels are masked before optional public JPEG encoding and final byte limits',async()=>{
  const previous={document:globalThis.document,bitmap:globalThis.createImageBitmap},calls=[];
  const png=new Uint8Array(2048);png.set([137,80,78,71,13,10,26,10]);new DataView(png.buffer).setUint32(16,100);new DataView(png.buffer).setUint32(20,50);
  const raw=Buffer.from(png).toString('base64'),encoded=Buffer.from([255,216,255,217]).toString('base64');
  globalThis.createImageBitmap=async blob=>{assert.equal(blob.type,'image/png');calls.push('decode-png');return{close(){calls.push('close');}};};
  globalThis.document={createElement(){return{width:0,height:0,getContext:()=>({drawImage(){calls.push('draw');},fillRect(){calls.push('mask');}}),
    toDataURL:mime=>{assert.equal(mime,'image/jpeg');calls.push('encode-jpeg');return`data:${mime};base64,${encoded}`;}};}};
  const area={region:{x:0,y:0,width:100,height:50},redactions:[{x:10,y:10,width:20,height:10}]};
  try{
    const out=await prepareScreenshot({format:'png',data:raw,scale:1},area,area,{format:'jpeg',max_bytes:1024});
    assert.deepEqual(calls,['decode-png','draw','mask','mask','encode-jpeg','close']);
    assert.equal(out.image.media_type,'image/jpeg');assert(out.image.bytes<=1024);assert.notEqual(out.image.base64,raw);
    await assert.rejects(prepareScreenshot({format:'webp',data:raw},area,area,{format:'jpeg'}),/Unsupported screenshot codec/);
    assert.equal(calls.filter(row=>row==='decode-png').length,1);
  }finally{globalThis.document=previous.document;globalThis.createImageBitmap=previous.bitmap;}
});
