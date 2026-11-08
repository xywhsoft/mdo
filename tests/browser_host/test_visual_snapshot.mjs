import assert from 'node:assert/strict';
import {test} from 'node:test';
import {createVisualSnapshotStore,hashVisualRaster,visualTileMatches} from '../../app/web/js/features/workbench/visual-snapshot.js';

const raster=(width=256,height=128)=>{const data=new Uint8ClampedArray(width*height*4);data.fill(42);return{
  width,height,data,getContext:()=>({getImageData:()=>({data})})};};
const reference=(()=>{let number=0;return()=>`vs-${(++number).toString(16).padStart(48,'0')}`;})();
const context={document:'document-fixture',navigation:'1',frame_stamp:'[]',region:{x:10,y:20,width:256,height:128},
  options:{mode:'viewport',document:'document-fixture'},redactions:[]};

test('visual tile hashes detect local/neighbor changes without retaining pixel arrays',async()=>{
  const first=raster(),second=raster(),before=await hashVisualRaster(first),same=await hashVisualRaster(second);
  assert(visualTileMatches(before,same,20.5,30.5));assert(!JSON.stringify(before).includes('data'));
  second.data[(30*256+220)*4]=99;const distant=await hashVisualRaster(second);
  assert(visualTileMatches(before,distant,20,30));assert(!visualTileMatches(before,distant,220,30));
  second.data[(30*256+65)*4]=100;const adjacent=await hashVisualRaster(second);
  assert(!visualTileMatches(before,adjacent,63,30),'A point near the tile edge ignored its padded neighborhood');
  assert(!visualTileMatches(before,adjacent,-1,0));assert(!visualTileMatches(before,adjacent,256,0));
  assert(!visualTileMatches(before,adjacent,NaN,0));assert(!visualTileMatches(before,{...adjacent,width:255},10,10));
});

test('raster bounds are checked before readback and malformed data is refused',async()=>{
  let reads=0;const invalid={width:4096,height:4096,getContext(){reads++;throw Error('Must not allocate');}};
  await assert.rejects(hashVisualRaster(invalid),{code:'visual_raster_invalid'});assert.equal(reads,0);
  await assert.rejects(hashVisualRaster({width:1,height:1,getContext:()=>({getImageData:()=>({data:[]})})}),{code:'visual_raster_invalid'});
  assert(!visualTileMatches({width:1,height:1},{width:1,height:1},0,0));
});

test('references are browser-scoped, latest-image-only, immutable and consumed once',async()=>{
  const store=createVisualSnapshotStore({random:reference}),pixels=await hashVisualRaster(raster());
  const original=structuredClone(context),a=store.issue('a',{...original,base64:'must-not-retain'},pixels);
  assert.equal(a.coordinates,'image_pixels');assert(!('hashes'in a));assert.equal(a.width,256);
  assert.throws(()=>store.get('b',a.id),{code:'visual_reference_expired'});
  original.region.x=999;pixels.hashes[0]='0'.repeat(64);
  const kept=store.get('a',a.id);assert.equal(kept.context.region.x,10);assert(!('base64'in kept.context));
  assert.throws(()=>{kept.context.region.x=999;},TypeError);
  const next=store.issue('a',context,pixels);assert.throws(()=>store.get('a',a.id),{code:'visual_reference_expired'});
  assert.equal(store.consume('a',next.id).context.document,context.document);
  assert.throws(()=>store.consume('a',next.id),{code:'visual_reference_expired'});assert.deepEqual(store.usage(),{entries:0,bytes:0});
});

test('leases expire monotonically and entry/byte quotas evict bounded oldest metadata',async()=>{
  let now=0;const pixels=await hashVisualRaster(raster()),store=createVisualSnapshotStore({now:()=>now,random:reference,ttl:10,limit:2});
  const a=store.issue('a',context,pixels),b=store.issue('b',context,pixels);assert.equal(store.usage().entries,2);
  const one=store.usage().bytes/2;store.issue('c',context,pixels);
  assert.throws(()=>store.get('a',a.id),{code:'visual_reference_expired'});assert(store.get('b',b.id));
  now=10;assert.deepEqual(store.usage(),{entries:0,bytes:0});
  const bounded=createVisualSnapshotStore({random:reference,limit:32,byteLimit:Math.ceil(one*1.5)});
  bounded.issue('a',context,pixels);bounded.issue('b',context,pixels);assert.equal(bounded.usage().entries,1);assert(bounded.usage().bytes<=one*1.5);
  bounded.release('b');assert.deepEqual(bounded.usage(),{entries:0,bytes:0});
});

test('failed reference admission preserves the previous completed screenshot lease',async()=>{
  let secure=true;const store=createVisualSnapshotStore({random:()=>secure?reference():'unsafe'}),pixels=await hashVisualRaster(raster());
  const a=store.issue('a',context,pixels);secure=false;
  assert.throws(()=>store.issue('a',context,pixels),{code:'visual_identity_unavailable'});assert(store.get('a',a.id));
  assert.throws(()=>store.issue('a',{...context,frame_stamp:'x'.repeat(65537)},pixels),{code:'visual_snapshot_invalid'});
  assert.throws(()=>store.issue('a',context,{...pixels,hashes:[]}),{code:'visual_snapshot_invalid'});assert(store.get('a',a.id));
});
