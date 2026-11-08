import {browserRuntime} from './automation-runtime.js';
import {createFrameReader} from './frame-reader.js';
import {prepareFrameCapture} from './frame-capture.js';

const permitted=new Set(['observe','locate','prepare','verify','visualPoint','scrollPoint','optionList','selectOptions','uploadInfo','chooserInfo','captureArea']);
const mutations=new Set(['selectOptions']);
const issue=(code,message)=>Object.assign(new Error(message),{code});
const pointValid=point=>point&&[point.x,point.y].every(number=>Number.isFinite(number)&&number>=0&&number<=32768);

// The final dispatcher uses fixed methods only. Script and process/context IDs
// cannot be supplied by a model. Route metadata is for native adapters alone.
export function createFrameDriver({execute,checkpoint=async()=>{},nativeCapture=null,nativeFileInputs=false}){
  if(typeof nativeFileInputs!=='boolean')throw issue('file_input_context_invalid','Native file input inspection requires an explicit host boolean');
  if(nativeCapture&&(!Number.isSafeInteger(nativeCapture.navigation)||nativeCapture.navigation<1))throw issue('capture_context_invalid','The Android capture owner has no exact navigation');
  const captureOwner=nativeCapture?Object.freeze({navigation:nativeCapture.navigation}):null;
  async function read(method,options={},control={}){
    let possibleEffect=false,interruption=null;
    const deadline=performance.now()+Math.min(60000,Math.max(1,Number(options.timeout_ms)||10000));
    const check=async(nativeFailure=false)=>{
      try{await checkpoint();await control.checkpoint?.({native_failure:nativeFailure===true});}
      catch(error){interruption=error;throw error;}
      if(performance.now()>=deadline)throw issue('timed_out','The frame operation deadline expired');
    };
    const beforeEvaluation=async()=>{
      // Inventory/bootstrap is native-only. Every page expression, through
      // either reader or direct method, waits for the original action's gate.
      if(!control.beforeEvaluate)return;
      try{await control.beforeEvaluate();}catch(error){interruption=error;throw error;}
      await check();
    };
    const native=async(command,params)=>{
      await check();if(command==='frames'&&params.action==='evaluate')await beforeEvaluation();
      let result;
      try{result=await execute(command,{...params,...(control.navigation_ref?{navigation_ref:control.navigation_ref}:{})});return result;}
      finally{await check(result?.ok===false&&typeof result.error?.code==='string');}
    };
    const reader=createFrameReader({execute:(command,params)=>native(command,params),checkpoint:check});
    const fromReader=async(name,args)=>{const result=await reader[name]({...args,timeout_ms:Math.max(1,deadline-performance.now())});if(interruption)throw interruption;return result;};
    async function evaluate(route,name,args){
      await check();await beforeEvaluation();
      const request={action:'evaluate',agent:true,frame:route.frame,document:route.frame_document,
        script:`(${browserRuntime.toString()})({local_frame:true}).${name}(${JSON.stringify(args)})`};
      // Read helpers can be retried by their caller. Mutations retain uncertainty
      // from the instant their request is sent, including lost/error replies.
      if(mutations.has(name)){control.onDispatch?.();possibleEffect=true;}
      let result;
      try{
        result=await execute('frames',request);
        if(!result?.ok&&result?.error?.evaluation_may_have_occurred===false)possibleEffect=false;
      }finally{await check(result?.ok===false&&typeof result.error?.code==='string');}
      if(!result?.ok){
        throw issue(result?.error?.code||'frame_unavailable','The pinned frame operation was not confirmed');
      }
      if(result.frame!==route.frame||result.frame_document!==route.frame_document||typeof result.value?.ok!=='boolean')
        throw issue('frame_response_invalid','The operation reply does not match its pinned frame');
      if(mutations.has(name)&&result.value.error?.effect_may_have_occurred===false)possibleEffect=false;
      return result.value;
    }
    async function pinChain(route){
      const tree=await native('frames',{action:'list',agent:true});
      if(!tree?.ok||tree.complete!==true||!Array.isArray(tree.frames)||tree.frames.length>32)
        throw issue(tree?.error?.code||'frame_not_ready','The current frame ancestry is not complete');
      const rows=new Map(tree.frames.map(row=>[row.id,row])),chain=[],seen=new Set();let row=rows.get(route.frame);
      if(row?.document!==route.frame_document)throw issue('stale_frame','The target frame changed before operation delivery');
      while(row&&chain.length<32){
        if(seen.has(row.id)||row.loading||!row.isolated)throw issue('stale_frame','The frame ancestry is no longer ready');
        seen.add(row.id);chain.push({frame:row.id,document:row.document});
        if(row.id==='f0'){if(row.parent!==null)throw issue('frame_response_invalid','The original root has an invalid parent');return chain;}
        row=rows.get(row.parent);
      }
      throw issue('stale_frame','The original frame ancestry is no longer present');
    }
    async function mapPoint(route,local){
      if(!pointValid(local.point)||!Array.isArray(local.geometry)||local.geometry.length>16||!local.geometry.every(Number.isFinite))
        throw issue('frame_response_invalid','The local target returned invalid CSS geometry');
      const chain=await pinChain(route);let point={...local.point},geometry=[...local.geometry],focused=true;
      for(let index=0;index<chain.length-1;index++){
        const current=chain[index],parent=chain[index+1];
        const owner=await native('frames',{action:'owner',agent:true,frame:current.frame,document:current.document,point});
        if(!owner?.ok)throw issue(owner?.error?.code||'frame_unavailable','The pinned iframe owner changed');
        if(owner.frame!==current.frame||owner.frame_document!==current.document||owner.parent!==parent.frame||owner.parent_document!==parent.document)
          throw issue('stale_frame','The iframe owner no longer belongs to the original parent');
        if(!owner.value?.ok)throw issue(owner.value?.error?.code||'frame_owner_unavailable','The iframe owner is not actionable');
        if(!pointValid(owner.value.point)||!Array.isArray(owner.value.geometry)||owner.value.geometry.length!==8||!owner.value.geometry.every(Number.isFinite))
          throw issue('frame_response_invalid','The parent returned invalid CSS geometry');
        point={...owner.value.point};geometry.push(...owner.value.geometry,point.x,point.y);focused&&=owner.value.focused===true;
      }
      const fresh=await pinChain(route);
      if(JSON.stringify(fresh)!==JSON.stringify(chain))throw issue('stale_frame','The frame ancestry changed during coordinate mapping');
      return{point,geometry,frame_chain:chain,parents_focused:focused};
    }
    async function uploadParents(route,chain){
      for(let index=0;index<chain.length-1;index++){
        const child=chain[index],parent=chain[index+1],owner=await native('frames',{action:'upload_policy',agent:true,frame:child.frame,document:child.document});
        if(!owner?.ok)throw issue(owner?.error?.code||'frame_unavailable','The upload frame owner could not be checked');
        if(owner.frame!==child.frame||owner.frame_document!==child.document||owner.parent!==parent.frame||owner.parent_document!==parent.document)
          throw issue('stale_frame','The upload frame owner changed during policy checking');
        if(!owner.value?.ok)throw issue(owner.value?.error?.code||'frame_owner_unavailable','A parent iframe is not enabled for upload');
      }
      if(JSON.stringify(await pinChain(route))!==JSON.stringify(chain))throw issue('stale_frame','The upload ancestry changed during policy checking');
    }
    try{
      if(control.navigation_ref!==undefined&&(typeof control.navigation_ref!=='string'||!/^nv-[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/i.test(control.navigation_ref)||method!=='observe'))
        throw issue('navigation_unproven','Use the original native navigation reference for its observation only');
      if(!permitted.has(method))throw issue('frame_method_invalid','Only fixed Browser Use operations are allowed');
      if(method==='observe'||method==='locate')return await fromReader(method,options);
      if(method==='visualPoint'||method==='scrollPoint'){
        const scroll=method==='scrollPoint';
        if(!pointValid(options.point))throw issue('visual_point_invalid','Choose a bounded viewport point');
        const meta=await fromReader('describe',{document:options.document});if(!meta.ok)return meta;
        const rows=new Map(meta.frames.map(row=>[row.id,row]));let row=rows.get('f0'),point={...options.point};
        for(let depth=0;row&&depth<32;depth++){
          if(row.loading||!row.isolated)throw issue('frame_not_ready','The selected point frame is not ready');
          const route={frame:row.id,frame_document:row.document};
          let document=row.id==='f0'?meta.document:null;
          if(scroll&&!document){const local=await evaluate(route,'describe',{});
            if(!local.ok||!/^d-[a-f0-9-]{36}$/i.test(local.document))throw issue('frame_response_invalid','The scroll frame has no original document identity');document=local.document;}
          const hit=await evaluate(route,method,{point,...(scroll?{document,_route_child:true}:options.op==='key'?{op:'key'}:{}),...(!scroll&&row.id==='f0'?{document:meta.document}:{})});
          if(!hit.ok)return hit;
          if(!/^d-[a-f0-9-]{36}$/i.test(hit.document)||(scroll||row.id==='f0')&&hit.document!==document)
            throw issue('stale_document','The visual point document changed');
          if(scroll&&(!pointValid(hit.point)||hit.point.x!==point.x||hit.point.y!==point.y))
            throw issue('frame_response_invalid','The scroll reply changed its original local point');
          if(hit.frame_ref===null){
            const mapped=await mapPoint(route,{point,geometry:[]});
            if(Math.hypot(mapped.point.x-options.point.x,mapped.point.y-options.point.y)>.5)
              throw issue('visual_context_changed','An iframe moved during point routing');
            const focus={};
            if(scroll){
              if(!/^e[1-9][0-9]{0,12}$/.test(hit.hit_ref))throw issue('frame_response_invalid','The scroll point has no original local hit identity');
              focus.hit_ref=`r1|${route.frame}|${route.frame_document}|${hit.document}|${hit.hit_ref}`;
              focus.native_target={document:hit.document,ref:hit.hit_ref};
            }
            if(options.op==='key'){
              if(!/^e[1-9][0-9]{0,12}$/.test(hit.focus_ref))throw issue('frame_response_invalid','The image point has no original keyboard target');
              focus.target={ref:`r1|${route.frame}|${route.frame_document}|${hit.document}|${hit.focus_ref}`};
              focus.native_target={document:hit.document,ref:hit.focus_ref};
            }
            return{ok:true,document:meta.document,point:mapped.point,frame_chain:mapped.frame_chain,...focus};
          }
          if(!/^e[1-9][0-9]{0,12}$/.test(hit.frame_ref))throw issue('frame_response_invalid','The visual point iframe identity is invalid');
          let selected=null;
          for(const candidate of rows.values())if(candidate.parent===row.id){
            const owner=await native('frames',{action:'capture_owner',agent:true,frame:candidate.id,document:candidate.document});
            if(!owner?.ok||owner.frame!==candidate.id||owner.frame_document!==candidate.document||owner.parent!==row.id||owner.parent_document!==row.document)
              throw issue('stale_frame','A visual point iframe owner changed');
            if(owner.value?.owner?.ref!==hit.frame_ref||owner.value.owner.document!==hit.document)continue;
            if(selected)throw issue('frame_response_invalid','The visual point owner has multiple native frames');
            const box=owner.value.content;
            if(!owner.value.ok||owner.value.painted!==true||!box||![box.x,box.y].every(n=>Number.isFinite(n)&&Math.abs(n)<=32768)||![box.width,box.height].every(n=>Number.isFinite(n)&&n>0&&n<=32768))
              throw issue('frame_owner_unavailable','The visual point iframe is hidden or has unsupported geometry');
            const local={x:point.x-box.x,y:point.y-box.y};
            if(local.x<0||local.y<0||local.x>=box.width||local.y>=box.height)
              throw issue('visual_point_outside_viewport','The point is on an iframe border or outside its content');
            selected={row:candidate,point:local};
          }
          if(!selected)throw issue('frame_not_ready','The visible iframe has no original ready native frame');
          row=selected.row;point=selected.point;
        }
        throw issue('frame_limit','The visual point exceeds its native frame depth bound');
      }
      async function inspectFileInput(route,local,chain){
        if(!nativeFileInputs)return;
        const result=await native('frames',{action:'file_input',agent:true,frame:route.frame,document:route.frame_document,
          local_document:route.document,ref:route.ref,frame_chain:chain,timeout_ms:Math.max(1,Math.floor(deadline-performance.now()))});
        if(!result?.ok)throw issue(result?.error?.code||'file_input_unavailable','The native file input binding was not confirmed');
        const value=result.value,element=value?.element;
        if(result.frame!==route.frame||result.frame_document!==route.frame_document||value?.ok!==true||value.document!==route.document||element?.ref!==route.ref||
          ['multiple','directory','hidden'].some(key=>typeof element?.[key]!=='boolean'||element[key]!==local.element[key])||
          JSON.stringify(await pinChain(route))!==JSON.stringify(chain))
          throw issue('upload_target_changed','The native file input no longer matches its original local target and frame chain');
      }
      if(method==='chooserInfo'){
        const page=await fromReader('describe',{document:options.document});
        if(!page.ok)return page;
        if(!options.document||page.document!==options.document)throw issue('stale_document','Observe the current root before resolving its file picker');
        const pickers=await native('chooser',{action:'list',agent:true}),picker=pickers.choosers?.find(row=>row.id===options.chooser);
        if(!pickers.ok||!picker?.supported||!picker.frame||!picker.frame_document)throw issue('chooser_changed','The exact frame-owned file picker is no longer available');
        const route={frame:picker.frame,frame_document:picker.frame_document},chain=await pinChain(route),meta=await evaluate(route,'describe',{});
        if(!meta.ok||!/^d-[a-f0-9-]{36}$/i.test(meta.document))throw issue('frame_response_invalid','The picker document identity is invalid');
        await uploadParents(route,chain);
        const target=await native('chooser',{action:'target',agent:true,chooser:picker.id,document:meta.document,frame_chain:chain});
        if(!target.ok)return target;
        if(target.document!==meta.document||!/^e[1-9][0-9]{0,12}$/.test(target.element?.ref)||JSON.stringify(await pinChain(route))!==JSON.stringify(chain))
          throw issue('target_changed','The picker no longer belongs to its original input and document');
        await inspectFileInput({...route,document:meta.document,ref:target.element.ref},target,chain);
        return{...target,document:options.document,element:{...target.element,frame:route.frame,ref:`r1|${route.frame}|${route.frame_document}|${meta.document}|${target.element.ref}`},
          native_target:{document:meta.document,ref:target.element.ref,frame_chain:chain}};
      }
      if(method==='captureArea')return await prepareFrameCapture({options,native,evaluate,nativeCapture:captureOwner,
        describe:args=>fromReader('describe',args),locate:args=>fromReader('locate',args)});
      const found=await fromReader('locate',{document:options.document,target:options.target,_upload:method==='uploadInfo'});
      if(!found.ok)return found;
      const route=found.route,localOptions={...options,document:route.document,target:{ref:route.ref}};
      const local=await evaluate(route,method,localOptions);
      if(!local.ok)return local;
      if(local.document!==route.document||local.element&&local.element.ref!==route.ref||local.ref&&local.ref!==route.ref)
        throw issue('target_changed','The operation returned a different target or document');
      const out={...local,document:options.document};
      if(local.element)out.element={...local.element,ref:found.element.ref,frame:route.frame};
      if(local.ref)out.ref=found.element.ref;
      if(Object.hasOwn(local,'frame'))out.frame=route.frame;
      if(method==='prepare')return{...out,...await mapPoint(route,local),native_target:{document:route.document,ref:route.ref}};
      if(method==='uploadInfo'){
        const chain=await pinChain(route);await uploadParents(route,chain);
        await inspectFileInput(route,local,chain);
        return{...out,native_target:{document:route.document,ref:route.ref,frame_chain:chain}};
      }
      if(method==='verify'&&local.focused){
        const prepared=await evaluate(route,'prepare',{...localOptions,scroll:false});
        if(!prepared.ok)return{...out,focused:false};
        try{const mapped=await mapPoint(route,prepared);out.focused=mapped.parents_focused;}
        catch(error){if(interruption)throw interruption;if(!['frame_owner_hidden','frame_owner_obscured','frame_point_outside_viewport','unsupported_geometry'].includes(error.code))throw error;out.focused=false;}
      }
      return out;
    }catch(error){
      if(interruption)throw interruption;
      return{ok:false,error:{code:typeof error?.code==='string'?error.code:'frame_unavailable',
        message:String(error?.message||'Frame operation failed').slice(0,256),effect_may_have_occurred:possibleEffect}};
    }
  }
  return Object.freeze({read,evaluationPreparation:'per_expression'});
}
