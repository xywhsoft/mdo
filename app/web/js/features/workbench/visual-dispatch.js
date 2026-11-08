import {prepareScreenshot} from './capture-image.js';
import {visualSnapshots,visualPointUnchanged} from './visual-snapshot.js';

const supported=new Set(['click','double_click','right_click','middle_click','hover','scroll','drag','key']);
const regionMatches=(a,b)=>a&&b&&['x','y','width','height'].every(key=>Number.isFinite(a[key])&&Number.isFinite(b[key])&&Math.abs(a[key]-b[key])<=.01);
const frame=/^(?:f0|f-[a-f0-9-]{36})$/i,document=/^fd-[a-f0-9-]{36}$/i;

// Fixed operation after shared C admission. Neither scripts nor native frame
// pins can be supplied by the model; proofs come from a completed image only.
export async function executeVisualAction({browser,params,view,read,guard,send,sendDrag,fail,deadline,action,setSnapshot,
  store=visualSnapshots,render=prepareScreenshot,captureCheckpoint=()=>guard(true)}){
  let key=null;
  try{
    if(!supported.has(params.op))return fail('unsupported_action','This visual path supports pointer gestures, wheel, dragging and focus-bound keys; use observed DOM targets for fields');
    const checked=await guard(true);if(checked.handled)return checked;
    if(checked.state.frame_snapshot_input!==true)return fail('unsupported_capability','This browser lacks native complete-frame visual input fences');
    const dragging=params.op==='drag',keyboard=params.op==='key';
    if(keyboard&&checked.state.frame_snapshot_keyboard!==true)return fail('unsupported_capability','This browser lacks native focus-bound visual keyboard input');
    if(dragging&&(checked.state.frame_snapshot_drag!==true||typeof sendDrag!=='function'))return fail('unsupported_capability','This browser lacks complete-frame native visual drag fences');
    if(checked.state.input_busy||checked.state.capture_busy)return fail('browser_busy','Native input or capture is still completing; inspect its receipt before visual input');
    if(dragging&&(!params.destination||params.destination.image!==params.target.image))
      return fail('visual_image_mismatch','Both drag points must come from the same completed screenshot');
    const saved=store.get(browser,params.target.image),proof=saved.context,targets=dragging?[params.target,params.destination]:[params.target];
    if(params.document!==proof.document||checked.state.navigation!==proof.navigation)return fail('visual_context_changed','The screenshot no longer belongs to this document and navigation');
    if(targets.some(({x,y})=>!Number.isInteger(x)||!Number.isInteger(y)||x<0||y<0||x>=saved.raster.width||y>=saved.raster.height))
      return fail('visual_point_invalid','Choose an integer point inside the delivered image dimensions');
    const area=await read('captureArea',proof.options);if(!area.ok)return{handled:true,result:area};
    const same=other=>other.frame_stamp===proof.frame_stamp&&regionMatches(other.region,proof.region)&&other.device_pixel_ratio===proof.device_pixel_ratio;
    if(!same(area))return fail('visual_context_changed','The document, frame layout, scroll or screenshot crop changed; request a fresh image');
    const documentPoints=targets.map(({x,y})=>({x:proof.region.x+x*proof.region.width/saved.raster.width,y:proof.region.y+y*proof.region.height/saved.raster.height}));
    const marginX=2*proof.region.width/saved.raster.width,marginY=2*proof.region.height/saved.raster.height;
    const masked=boxes=>documentPoints.some(point=>boxes.some(box=>point.x>=box.x-marginX&&point.x<=box.x+box.width+marginX&&point.y>=box.y-marginY&&point.y<=box.y+box.height+marginY));
    if(masked(proof.redactions)||masked(area.redactions))return fail('visual_region_masked','This point is hidden by screenshot privacy masking; use an exact observed DOM target');
    const points=documentPoints.map(point=>({x:point.x-area.scroll.x,y:point.y-area.scroll.y})),point=points[0];
    if(points.some(point=>point.x<0||point.y<0||point.x>=area.viewport.width||point.y>=area.viewport.height))
      return fail('visual_point_outside_viewport','Scroll with a supported operation, then capture a fresh viewport image before clicking this point');
    if(dragging&&Math.hypot(points[1].x-point.x,points[1].y-point.y)<4)
      return fail('drag_distance_too_small','Choose two visible drag points at least four CSS pixels apart');
    const rows=JSON.parse(proof.frame_stamp);
    if(!Array.isArray(rows)||!rows.length||rows.length>32||rows.some(row=>!frame.test(row[0])||!document.test(row[1])||
       row[2]!==null&&!frame.test(row[2])||row[3]!==false||row[4]!==true)||new Set(rows.map(row=>row[0])).size!==rows.length)
      return fail('visual_frame_not_ready','The screenshot needs a complete ready native frame tree before visual input');
    const snapshot=rows.map(row=>({frame:row[0],document:row[1],parent:row[2]||''}));
    const root=snapshot.find(row=>row.frame==='f0');if(!root||root.parent)return fail('visual_frame_not_ready','The screenshot has no original native root');
    const capture=proof.capture;if(!capture)return fail('visual_reference_expired','Request a new screenshot with its capture parameters before visual input');
    const scale=Math.min(1,capture.max_width/(area.region.width*area.device_pixel_ratio),capture.max_height/(area.region.height*area.device_pixel_ratio));
    let settled=false,native,failure;key=action.id+'-visual';
    const pending=view.execute('capture',{agent:true,navigation:proof.navigation,capture_key:key,clip:{...area.region,scale},
      ...(area.native_capture?{capture_context:area.native_capture}:{}),
      format:'png',max_bytes:262144,timeout_ms:Math.max(1,Math.ceil(deadline-performance.now()))})
      .then(value=>{native=value;settled=true;},error=>{failure=error;settled=true;});
    while(!settled){
      // This checkpoint only waits for our outstanding read-only capture. It
      // retains all authority/lifetime/deadline checks; input and every later
      // page read still use the ordinary guard and require capture cleanup.
      await Promise.race([pending,new Promise(resolve=>setTimeout(resolve,40))]);const boundary=await captureCheckpoint();if(boundary.handled)return boundary;
    }
    if(failure)return fail('visual_validation_failed','The current native screenshot could not be validated; no visual input was sent');
    if(area.native_capture&&(native?.native_document!==area.native_capture.native_document||native?.navigation!==area.native_capture.navigation||
       native?.document!==area.document||native?.capture_key!==key))return fail('capture_changed','The validation screenshot reply lost its original native context');
    if(area.native_capture&&native.ok!==true){const code=native.ok===false&&/^[a-z][a-z0-9_]{0,63}$/.test(native.error?.code||'')?native.error.code:'capture_failed';
      return fail(code,'The original native validation screenshot was not confirmed');}
    if(native?.format!==undefined&&native.format!=='png')return fail('capture_source_invalid','Visual validation requires its original lossless screenshot source');
    const after=await read('captureArea',proof.options);if(!after.ok)return{handled:true,result:after};
    if(!same(after)||masked(after.redactions))return fail('visual_context_changed','The scene or privacy coverage changed during visual validation; request a fresh image');
    let unchanged=false;
    await render(native,area,after,capture,{raster:async canvas=>{
      unchanged=true;for(const {x,y} of targets)if(!await visualPointUnchanged(canvas,saved.raster,x,y)){unchanged=false;break;}
    }});
    if(!unchanged)return fail('visual_scene_changed','A selected image neighborhood changed; request a fresh screenshot before visual input');
    let chains=[],focus;
    if(dragging||keyboard){
      const valid=chain=>Array.isArray(chain)&&chain.length>0&&chain.length<=32&&chain.every((pin,index)=>{
        const saved=snapshot.find(row=>row.frame===pin.frame);return saved?.document===pin.document&&(index+1<chain.length?saved.parent===chain[index+1].frame:pin.frame==='f0'&&!saved.parent);
      });
      for(const point of points){
        const routed=await read('visualPoint',{document:proof.document,point,...(keyboard?{op:'key'}:{})});if(!routed.ok)return{handled:true,result:routed};
        if(routed.document!==proof.document||!regionMatches({...routed.point,width:1,height:1},{...point,width:1,height:1})||!valid(routed.frame_chain))
          return fail('visual_context_changed','A drag point no longer belongs to its original frame tree');
        chains.push(routed.frame_chain);
        if(keyboard){
          const parts=routed.target?.ref?.split('|'),native=routed.native_target;
          if(parts?.length!==5||parts[0]!=='r1'||parts[1]!==routed.frame_chain[0].frame||parts[2]!==routed.frame_chain[0].document||
             !/^d-[a-f0-9-]{36}$/i.test(native?.document)||!/^e[1-9][0-9]{0,12}$/.test(native?.ref)||parts[3]!==native.document||parts[4]!==native.ref)
            return fail('frame_response_invalid','The visual key target is not bound to its original isolated frame');
          focus=routed;
        }
      }
    }
    const boundary=await guard(true);if(boundary.handled)return boundary;
    store.consume(browser,params.target.image);setSnapshot(snapshot,root,keyboard?chains[0]:null);
    if(dragging){
      const delivered=await sendDrag({...point,to_x:points[1].x,to_y:points[1].y,source_frame_chain:chains[0],destination_frame_chain:chains[1],frame_snapshot:snapshot});
      if(delivered.handled)return delivered;
      return{handled:true,result:{ok:true,action:action.id,dispatched:true,verification:'native_delivery',input_method:'native_drag',drag_kind:delivered.drag_kind,
        visual_reference:params.target.image,scene_validated:true,observation:await read('observe',{limit:100,max_bytes:4096})}};
    }
    const delivered=await send(keyboard?{kind:'focus',focus_document:focus.native_target.document,focus_ref:focus.native_target.ref}:
      {kind:params.op==='hover'?'move':params.op==='scroll'?'wheel':'click',...point,
      button:params.op==='right_click'?'right':params.op==='middle_click'?'middle':'left',click_count:params.op==='double_click'?2:1,dx:params.dx||0,dy:params.dy||0});
    if(delivered)return delivered;
    if(keyboard){
      const focused=await read('verify',{document:proof.document,target:focus.target,op:'key'});
      if(!focused.ok||!focused.focused)return fail('focus_changed','The image target did not retain keyboard focus; no key was sent');
      const keyed=await send({kind:'key',key:params.key,focus_document:focus.native_target.document,focus_ref:focus.native_target.ref});
      if(keyed)return keyed;
    }
    if(['click','double_click','middle_click','key'].includes(params.op))await new Promise(resolve=>setTimeout(resolve,Math.min(150,Math.max(0,deadline-performance.now()-25))));
    const stopped=await guard(true);if(stopped.handled)return stopped;
    return{handled:true,result:{ok:true,action:action.id,dispatched:true,verification:'native_delivery',visual_reference:params.target.image,
      scene_validated:true,...(keyboard?{input_method:'native_key'}:{}),observation:await read('observe',{limit:100,max_bytes:4096})}};
  }catch(error){
    if(error.browserBoundary)throw error;
    if(['visual_reference_expired','visual_raster_invalid'].includes(error.code))return fail(error.code,error.message);
    throw error;
  }finally{if(key)try{await view.execute('capture_cancel',{capture_key:key});}catch{}}
}
