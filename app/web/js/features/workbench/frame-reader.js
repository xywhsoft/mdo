import {browserRuntime} from './automation-runtime.js';

// Internal read-only coordinator. Native aliases/documents select worlds;
// semantic data from one world never selects a different world or protocol ID.
const frameId=/^(?:f0|f-[a-f0-9-]{36})$/i, nativeDocument=/^fd-[a-f0-9-]{36}$/i;
const localDocument=/^d-[a-f0-9-]{36}$/i, elementId=/^e[1-9][0-9]{0,12}$/;
const methods=new Set(['describe','observe','locate']);
const problem=(code,message)=>Object.assign(new Error(message),{code});
const publicUrl=value=>{try{const url=new URL(value);url.username='';url.password='';return url.href.slice(0,2048);}catch{return '';}};
const reference=(frame,document,ref)=>`r1|${frame.id}|${frame.document}|${document}|${ref}`;
function parseReference(value){
  if(typeof value!=='string'||value.length>256)throw problem('target_invalid','The frame reference is malformed');
  const parts=value.split('|');
  if(parts.length!==5||parts[0]!=='r1'||!frameId.test(parts[1])||!nativeDocument.test(parts[2])||
     !localDocument.test(parts[3])||!elementId.test(parts[4]))throw problem('target_invalid','The frame reference is malformed');
  return{frame:parts[1],nativeDocument:parts[2],document:parts[3],ref:parts[4]};
}

export function createFrameReader({execute,checkpoint=async()=>{}}){
  const call=async(command,params,scope)=>{
    await checkpoint();if(performance.now()>=scope.deadline)throw problem('timed_out','The combined frame read exceeded its deadline');
    const value=await execute(command,params);await checkpoint(value?.ok===false&&typeof value.error?.code==='string');
    if(performance.now()>=scope.deadline)throw problem('timed_out','The combined frame read exceeded its deadline');return value;
  };
  async function inventory(scope,freshObservation=false){
    const value=await call('frames',{action:'list',agent:true},scope);
    if(!value?.ok){
      const code=value?.error?.code||'frame_unavailable';
      // Navigation may invalidate this read before any document has been bound.
      // Only a fresh observation may discard it and restart under the caller's
      // existing deadline. Pinned locators/evaluations keep their stale refusal.
      if(freshObservation&&code==='stale_frame')throw problem('frame_tree_changed','Navigation changed the unpinned frame inventory');
      throw problem(code,'The native frame inventory is unavailable');
    }
    if(value.complete!==true)throw problem('frame_not_ready','The frame process tree is still being established');
    if(!Array.isArray(value.frames)||!value.frames.length||value.frames.length>32)throw problem('frame_limit','The frame inventory exceeds the 32-frame observation budget');
    const byId=new Map();
    for(const row of value.frames){
      if(!frameId.test(row?.id)||!nativeDocument.test(row?.document)||byId.has(row.id)||
         typeof row.loading!=='boolean'||typeof row.isolated!=='boolean'||row.parent!==null&&!frameId.test(row.parent))
        throw problem('frame_response_invalid','The native frame inventory is malformed');
      byId.set(row.id,row);
    }
    if(byId.get('f0')?.parent!==null)throw problem('frame_response_invalid','The frame inventory has no original root');
    for(const row of byId.values()){
      let current=row,depth=0;
      for(;current?.id!=='f0'&&depth<32;depth++)current=byId.get(current?.parent);
      if(!current||depth>=32)throw problem('frame_response_invalid','The frame ancestry does not reach the original root');
    }
    return[byId.get('f0'),...value.frames.filter(row=>row.id!=='f0')];
  }
  async function evaluate(frame,method,options,scope){
    if(!methods.has(method))throw problem('frame_method_invalid','Only fixed read-only frame operations are permitted');
    const native=await call('frames',{action:'evaluate',agent:true,frame:frame.id,document:frame.document,
      script:`(${browserRuntime.toString()})({local_frame:true}).${method}(${JSON.stringify(options||{})})`},scope);
    if(!native?.ok)throw problem(native?.error?.code||'frame_unavailable','The frame document changed or could not be read');
    if(native.frame!==frame.id||native.frame_document!==frame.document||typeof native.value?.ok!=='boolean')
      throw problem('frame_response_invalid','The frame reply does not match its pinned document');
    return native.value;
  }
  async function begin(expected,scope){
    const frames=await inventory(scope,!expected),root=frames[0];
    if(root.loading||!root.isolated)throw problem('frame_not_ready','The original frame is not ready for isolated observation');
    const meta=await evaluate(root,'describe',{},scope);
    if(!meta.ok||!localDocument.test(meta.document))throw problem('frame_response_invalid','The root document identity is invalid');
    if(expected&&expected!==meta.document)throw problem('stale_document','Observe the current root document before locating an element');
    return{frames,root,meta,scope,freshObservation:!expected};
  }
  async function revalidate(context,relevant=context.frames,wholeTree=true){
    const current=await inventory(context.scope,context.freshObservation),byId=new Map(current.map(row=>[row.id,row]));
    const root=byId.get('f0');
    if(root?.document!==context.root.document||root.loading)throw problem('stale_document','The original document changed during the read');
    for(const row of relevant){
      const now=byId.get(row.id);
      if(!now||now.document!==row.document||now.parent!==row.parent||!row.loading&&now.loading)
        throw problem('stale_frame','A selected frame changed during the read');
    }
    if(wholeTree&&current.length!==context.frames.length)throw problem('frame_tree_changed','The frame tree changed during the read');
  }
  function ownElement(element,frame,document){
    if(!elementId.test(element?.ref)||!localDocument.test(document)||element.frame!=='f0')
      throw problem('frame_response_invalid','The local element identity is invalid');
    return{...element,ref:reference(frame,document,element.ref),frame:frame.id};
  }
  const guarded=operation=>async options=>{
    try{return await operation(options||{},{deadline:performance.now()+Math.min(60000,Math.max(1,Number(options?.timeout_ms)||10000))});}
    catch(error){return{ok:false,error:{code:error?.code||'frame_unavailable',message:String(error?.message||'Frame observation failed').slice(0,256),effect_may_have_occurred:false}};}
  };
  function bounded(result){
    if(new TextEncoder().encode(JSON.stringify(result)).length>262144)throw problem('frame_result_limit','The combined frame result exceeds 256 KiB; narrow the observation');
    return result;
  }
  const observe=guarded(async(options,scope)=>{
    const context=await begin(null,scope),limit=Math.min(400,Math.max(1,Number(options.limit)||200));
    const maximum=Math.min(32768,Math.max(256,Number(options.max_bytes)||16384)),encoder=new TextEncoder();
    const result={ok:true,...context.meta,url:publicUrl(context.meta.url),title:String(context.meta.title||'').slice(0,512),
      text:'',elements:[],frames:[],visited:0,truncated:false,semantics:'dom-subset-v1',frame_routing:'native-isolated-v1'};
    let textBytes=0;
    for(const frame of context.frames){
      const row={id:frame.id,document:frame.document,parent:frame.parent,url:publicUrl(frame.url),name:String(frame.name||'').slice(0,256),
        dom:false,loading:frame.loading,separate_process:frame.separate_process===true};
      result.frames.push(row);
      if(frame.loading||!frame.isolated){row.reason=frame.loading?'frame_loading':'frame_session_unavailable';result.truncated=true;continue;}
      if(result.visited>=6000||result.elements.length>=limit&&textBytes>=maximum){row.reason='observation_budget';result.truncated=true;continue;}
      const local=await evaluate(frame,'observe',{limit:limit-result.elements.length,max_bytes:Math.max(256,maximum-textBytes),
        _include_text:textBytes<maximum,_max_nodes:6000-result.visited},scope);
      if(!local.ok||!Array.isArray(local.elements)||!Array.isArray(local.frames)||local.frames.length!==1||
         !localDocument.test(local.document)||!Number.isInteger(local.visited)||local.visited<0||local.visited>6000-result.visited||local.document!==context.meta.document&&frame.id==='f0')
        throw problem('frame_response_invalid','The local observation is malformed or changed documents');
      row.dom=true;result.visited+=local.visited;result.truncated ||= local.truncated===true;
      for(const element of local.elements){if(result.elements.length>=limit){result.truncated=true;break;}result.elements.push(ownElement(element,frame,local.document));}
      const text=typeof local.text==='string'?local.text:'',prefix=result.text&&text?'\n':'';
      const encoded=encoder.encode(prefix+text),available=maximum-textBytes;
      let clipped=encoded.subarray(0,available),decoded='';
      while(clipped.length){try{decoded=new TextDecoder('utf-8',{fatal:true}).decode(clipped);break;}catch{clipped=clipped.subarray(0,clipped.length-1);}}
      result.text+=decoded;textBytes+=clipped.length;if(encoded.length>clipped.length)result.truncated=true;
      bounded(result);
    }
    await revalidate(context);return bounded(result);
  });
  const locate=guarded(async(options,scope)=>{
    if(!options.document)throw problem('stale_document','A fresh root document identity is required');
    const context=await begin(options.document,scope),target=options.target||{},ref=target.ref?parseReference(target.ref):null;
    if(ref&&Object.keys(target).some(key=>!['ref','frame'].includes(key)))throw problem('target_invalid','Use an element reference or semantic selectors separately');
    if(ref&&target.frame&&target.frame!==ref.frame)throw problem('target_invalid','The frame selector conflicts with the element reference');
    const selected=target.frame||ref?.frame,frames=selected?context.frames.filter(row=>row.id===selected):context.frames;
    if(!frames.length)throw problem('stale_frame','The selected frame is no longer present');
    const matches=[];let visited=0;
    for(const frame of frames){
      if(frame.loading||!frame.isolated)throw problem('frame_not_ready','A searched frame is not ready; a unique match cannot be established');
      if(ref&&frame.document!==ref.nativeDocument)throw problem('stale_frame','The element reference belongs to an earlier frame document');
      if(visited>=6000)throw problem('observation_incomplete','Narrow the locator to one frame before acting');
      const localTarget={...target};delete localTarget.frame;
      if(ref)localTarget.ref=ref.ref;
      const local=await evaluate(frame,ref?'locate':'describe',ref?{document:ref.document,target:localTarget,_upload:options._upload===true}:{},scope);
      const document=ref?ref.document:local.document;
      if(!ref&&(!local.ok||!localDocument.test(document)))throw problem('frame_response_invalid','The searched document identity is invalid');
      const found=ref?local:await evaluate(frame,'locate',{document,target:localTarget,_max_nodes:6000-visited,_upload:options._upload===true},scope);
      if(!found.ok&&!['element_not_found','element_ambiguous'].includes(found.error?.code))return found;
      if(!Number.isInteger(found.visited)||found.visited<0||found.visited>6000-visited)
        throw problem('frame_response_invalid','The locator did not return bounded traversal usage');
      visited+=found.visited;
      const candidates=found.ok?[found.element]:found.error?.code==='element_ambiguous'?found.error.candidates:[];
      if(!Array.isArray(candidates)||candidates.length>8)throw problem('frame_response_invalid','The locator candidate response is malformed');
      for(const element of candidates)matches.push({element:ownElement(element,frame,document),route:{frame:frame.id,frame_document:frame.document,document,ref:element.ref}});
      if(matches.length>1)break;
    }
    await revalidate(context,frames,!selected);
    if(!matches.length)return{ok:false,error:{code:'element_not_found',message:'No visible element matches in the searched frames'}};
    if(matches.length>1)return{ok:false,error:{code:'element_ambiguous',message:'More than one element matches; choose an exact frame or reference',candidates:matches.slice(0,8).map(row=>row.element)}};
    return{ok:true,document:context.meta.document,...matches[0],visited};
  });
  const describe=guarded(async(options,scope)=>{
    const context=await begin(options.document,scope);await revalidate(context);return{...context.meta,frames:context.frames};
  });
  return Object.freeze({describe,observe,locate});
}
