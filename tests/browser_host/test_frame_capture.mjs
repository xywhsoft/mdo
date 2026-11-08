import assert from 'node:assert/strict';
import {test} from 'node:test';
import {prepareFrameCapture} from '../../app/web/js/features/workbench/frame-capture.js';

const guid=n=>`00000000-0000-4000-8000-${String(n).padStart(12,'0')}`;
const child='f-'+guid(2),leaf='f-'+guid(3),doc=n=>'d-'+guid(n);
const rect=(x,y,width,height)=>({x,y,width,height});
function fixture(){
  const rows=[{id:'f0',document:'fd-'+guid(1),parent:null,loading:false,isolated:true},
    {id:child,document:'fd-'+guid(2),parent:'f0',loading:false,isolated:true},
    {id:leaf,document:'fd-'+guid(3),parent:child,loading:false,isolated:true}];
  const area=(n,region,view,scroll,redactions)=>({ok:true,document:doc(n),region,viewport:view,scroll,redactions,visited:1,device_pixel_ratio:1});
  const areas={f0:area(1,rect(0,100,800,600),{width:800,height:600},{x:0,y:100},[rect(10,110,20,10),{...rect(100.5,300.25,300,200),frame_ref:'e9'}]),
    [child]:area(2,rect(0,10,290,190),{width:290,height:190},{x:0,y:10},[rect(5,15,20,20),{...rect(50.25,40.5,100,80),frame_ref:'e8'}]),
    [leaf]:area(3,rect(3,5,96,76),{width:96,height:76},{x:3,y:5},[rect(90,70,9,11),rect(25,20,10,8)])};
  const owner=(n,ref,box,content,parent)=>({ok:true,owner:{document:doc(n),ref},painted:true,box,content,scroll:areas[parent].scroll,viewport:areas[parent].viewport});
  const owners={[child]:owner(1,'e9',rect(100.5,200.25,300,200),rect(105.75,205.5,289.5,189.5),'f0'),
    [leaf]:owner(2,'e8',rect(50.25,30.5,100,80),rect(52.5,32.75,95.5,75.5),child)};
  const state={describes:0,calls:[],beforeDescribe:null,beforeOwner:null,beforeArea:null};
  const args={options:{document:doc(1),mode:'viewport'},
    describe:async()=>{state.describes++;await state.beforeDescribe?.();return{ok:true,document:doc(1),frames:structuredClone(rows)};},
    locate:async()=>({ok:true,route:{frame:leaf,frame_document:rows[2].document,document:doc(3),ref:'e1'}}),
    evaluate:async(route,method,options)=>{
      assert.equal(method,'captureArea');state.calls.push({frame:route.frame,...options});
      const custom=await state.beforeArea?.(route,options);if(custom)return custom;
      return{...structuredClone(areas[route.frame]),...(options.mode==='element'?{region:rect(20,15,30,20)}:{})};
    },
    native:async(command,params)=>{
      assert.equal(command,'frames');assert.equal(params.action,'capture_owner');await state.beforeOwner?.(params);
      const row=rows.find(row=>row.id===params.frame),parent=rows.find(item=>item.id===row.parent);
      return{ok:true,frame:row.id,frame_document:row.document,parent:row.parent,parent_document:parent.document,value:structuredClone(owners[row.id])};
    }};
  return{rows,areas,owners,state,args,run:options=>prepareFrameCapture({...args,options:{...args.options,...options}})};
}

test('nested masks use exact owner identities and clip in fractional document coordinates',async()=>{
  const f=fixture(),out=await f.run();assert(out.ok);assert.deepEqual(out.region,rect(0,100,800,600));
  assert.deepEqual(out.frame_masks,{readable:3,opaque:0});assert.equal(out.redactions.length,4);
  assert(out.redactions.some(box=>JSON.stringify(box)===JSON.stringify(rect(110.75,310.5,20,20))));
  assert(out.redactions.some(box=>JSON.stringify(box)===JSON.stringify(rect(245.25,403.25,8.5,10.5))));
  assert(out.redactions.every(box=>box.frame_ref===undefined));
  assert.deepEqual(f.state.calls.map(row=>row._max_nodes),[6000,5999,5998]);assert(out.frame_stamp.includes(doc(3)));
});

test('nested element capture crops through every ancestor while retaining private masks',async()=>{
  const f=fixture(),out=await f.run({mode:'element',target:{ref:'qualified'}});assert(out.ok);
  assert.deepEqual(out.region,rect(175,348,31,21));assert.deepEqual(out.redactions,[rect(180.25,353.25,10,8)]);
  assert.equal(f.state.calls.at(-1).document,doc(3));assert.equal(f.state.calls.at(-1).target.ref,'e1');
  assert.deepEqual(out.scroll,{x:0,y:100});assert.deepEqual(out.viewport,{width:800,height:600});
});

test('same rectangles cannot authorize a missing or wrong-document owner binding',async()=>{
  for(const mode of ['reference','document','geometry']){
    const f=fixture();if(mode==='reference')f.owners[child].owner.ref='e99';
    if(mode==='document')f.owners[child].owner.document=doc(9);
    if(mode==='geometry')f.owners[child].box.x++;
    await assert.rejects(f.run(),error=>error.code===(mode==='document'?'stale_frame':'capture_changed'));
    assert.equal(f.state.calls.length,1);
  }
});

test('unsupported transforms retain the exact opaque parent mask and stop descendant scans',async()=>{
  const f=fixture();f.owners[child]={ok:false,owner:{document:doc(1),ref:'e9'},error:{code:'unsupported_geometry'}};
  const out=await f.run();assert(out.ok);assert.deepEqual(out.frame_masks,{readable:1,opaque:2});
  assert(out.redactions.some(box=>JSON.stringify(box)===JSON.stringify(rect(100.5,300.25,300,200))));assert.equal(f.state.calls.length,1);
  await assert.rejects(f.run({mode:'element',target:{ref:'qualified'}}),error=>error.code==='frame_capture_unsupported');
});

test('Android capture context carries opaque masks without claiming unreadable descendants',async()=>{
  const f=fixture();f.args.nativeCapture={navigation:3};
  f.owners[child]={ok:false,owner:{document:doc(1),ref:'e9'},error:{code:'unsupported_geometry'}};
  const out=await f.run(),stamp=JSON.parse(out.native_capture.frame_stamp);assert(out.ok);
  assert.equal(stamp[1][5],'opaque_geometry');assert.equal(stamp[2][5],'opaque_ancestor');
  assert.equal(stamp[1][6],null);assert.equal(stamp[2][9],null);
  assert.deepEqual(stamp[1][9],{owner:{document:doc(1),ref:'e9'}});
  assert(out.redactions.some(box=>JSON.stringify(box)===JSON.stringify(rect(100.5,300.25,300,200))));
  assert.equal(out.native_capture.navigation,3);assert.equal(out.native_capture.frame_stamp,out.frame_stamp);
});

test('unreadable or excessive child scans stay opaque without bypassing cancellation',async()=>{
  for(const kind of ['budget','unreadable']){
    const f=fixture();f.state.beforeArea=route=>{if(route.frame===child){if(kind==='budget')return{ok:false,error:{code:'capture_preparation_failed'}};throw Object.assign(Error('Frame vanished'),{code:'frame_evaluation_failed'});}};
    const out=await f.run();assert(out.ok);assert.equal(out.frame_masks.readable,1);assert.equal(out.redactions.length,2);assert.equal(f.state.calls.length,2);
  }
  const f=fixture(),sentinel=Object.assign(Error('Original action cancelled'),{code:'frame_evaluation_failed',browserBoundary:{handled:true}});
  f.state.beforeArea=route=>{if(route.frame===child)throw sentinel;};await assert.rejects(f.run(),error=>error===sentinel);
});

test('changed scroll, frame tree, scale or unknown budgets cannot expose child pixels',async()=>{
  const moved=fixture();moved.owners[child].scroll={x:0,y:101};await assert.rejects(moved.run(),error=>error.code==='capture_changed');
  const replaced=fixture();replaced.state.beforeDescribe=()=>{if(replaced.state.describes===2)replaced.rows[2].document='fd-'+guid(9);};
  await assert.rejects(replaced.run(),error=>error.code==='stale_frame');
  const scaled=fixture();scaled.areas[child].device_pixel_ratio=2;const hidden=await scaled.run();assert.equal(hidden.frame_masks.readable,1);assert.equal(hidden.redactions.length,2);
  const invalid=fixture();invalid.areas[child].visited=6000;await assert.rejects(invalid.run(),error=>error.code==='frame_response_invalid');
});

test('combined mask counts have one bounded result rather than per-frame expansion',async()=>{
  const f=fixture();f.areas.f0.redactions.unshift(...Array.from({length:254},()=>rect(10,110,20,10)));
  await assert.rejects(f.run(),error=>error.code==='capture_mask_limit');
});
