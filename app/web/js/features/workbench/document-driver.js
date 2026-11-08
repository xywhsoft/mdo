import {browserRuntime} from './automation-runtime.js';

const uuid='[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}';
const domId=new RegExp(`^d-${uuid}$`,'i'),nativeId=new RegExp(`^fd-${uuid}$`,'i'),elementId=/^e[1-9][0-9]{0,12}$/;
const allowed=new Set(['describe','observe','locate','prepare','verify','optionList','selectOptions','scrollPoint','captureArea']);
const inputs=new Set(['click','check','fill','type','key','select','scroll','hover']);
const scope={frame_scope:'main_document',frame_routing:'android-main-document-v1',limitations:Object.freeze(['child_frames_unavailable'])};
const issue=(code,message)=>Object.assign(new Error(message),{code});
const publicUrl=value=>{try{const url=new URL(value);url.username='';url.password='';return url.href.slice(0,2048);}catch{return '';}};
const coordinate=value=>typeof value==='number'&&Number.isFinite(value)&&Math.abs(value)<=1e8;
const rectangle=value=>value&&['x','y','width','height'].every(key=>coordinate(value[key]))&&value.width>0&&value.height>0;
function captureResult(value,pin,document,mode){
  const {region,scroll,viewport,device_pixel_ratio:dpr}=value;
  if(!rectangle(region)||region.width>32768||region.height>32768||region.width*region.height>16777216||
     !scroll||!coordinate(scroll.x)||!coordinate(scroll.y)||!viewport||!['width','height'].every(key=>coordinate(viewport[key])&&viewport[key]>0&&viewport[key]<=32768)||
     typeof dpr!=='number'||!Number.isFinite(dpr)||dpr<=0||dpr>16||region.width*region.height*dpr*dpr>33554432||
     !Number.isInteger(value.visited)||value.visited<0||value.visited>6000||!Array.isArray(value.redactions)||value.redactions.length>256||
     value.redaction!=='form-controls-and-opaque-content')
    throw issue('document_response_invalid','The original screenshot privacy scan is incomplete or invalid');
  if(region.x<scroll.x-1||region.y<scroll.y-1||region.x+region.width>scroll.x+viewport.width+1||region.y+region.height>scroll.y+viewport.height+1)
    throw issue('unsupported_geometry','This Android capture adapter supports only the visible viewport');
  if(mode==='viewport'&&(['x','y'].some(key=>Math.abs(region[key]-scroll[key])>1)||
     ['width','height'].some(key=>Math.abs(region[key]-viewport[key])>1)))
    throw issue('document_response_invalid','The original viewport screenshot region is incomplete');
  const owners=new Set(),redactions=value.redactions.map(mask=>{
    if(!rectangle(mask)||mask.x<region.x-.05||mask.y<region.y-.05||mask.x+mask.width>region.x+region.width+.05||mask.y+mask.height>region.y+region.height+.05)
      throw issue('document_response_invalid','The original screenshot privacy mask is invalid');
    if(mask.frame_ref!==undefined){
      if(!elementId.test(mask.frame_ref)||owners.has(mask.frame_ref))throw issue('document_response_invalid','The opaque frame mask has no unique original identity');
      owners.add(mask.frame_ref);
    }
    return{x:mask.x,y:mask.y,width:mask.width,height:mask.height};
  });
  const copyRegion={x:region.x,y:region.y,width:region.width,height:region.height},copyScroll={x:scroll.x,y:scroll.y},
    copyViewport={width:viewport.width,height:viewport.height};
  return{ok:true,document,region:copyRegion,scroll:copyScroll,viewport:copyViewport,redactions,device_pixel_ratio:dpr,
    redaction:'form-controls-and-opaque-content',visited:value.visited,...scope,
    frame_stamp:JSON.stringify([['f0',pin.native,null,false,true,'opaque_child_frames',document,copyScroll,
      {...copyViewport,device_pixel_ratio:dpr},{opaque_frames:[...owners].sort()}]]),
    frame_masks:{readable:1,opaque:owners.size},
    native_capture:{adapter:'android-main-document-v1',native_document:pin.native,navigation:pin.navigation,document,
      scroll:copyScroll,viewport:copyViewport,device_pixel_ratio:dpr},
    limitations:['child_frames_masked','opaque_media_masked','image_actions_unavailable','full_page_capture_unavailable']};
}

// The Android bridge owns one canonical main-frame peer. Do not invent a complete
// native frame tree or route page-provided aliases to other execution worlds.
export function createDocumentDriver({execute,checkpoint=async()=>{}}){
  async function read(method,options={},control={}){
    let possibleEffect=false,interruption=null;
    const deadline=performance.now()+Math.min(60000,Math.max(1,Number(options.timeout_ms)||10000));
    const expiry=issue('timed_out','The main-document operation exceeded its original deadline');
    const within=async operation=>{
      const remaining=deadline-performance.now();if(remaining<=0)throw expiry;
      let timer;
      try{return await new Promise((resolve,reject)=>{
        timer=setTimeout(()=>reject(expiry),Math.ceil(remaining));
        Promise.resolve().then(()=>{if(performance.now()>=deadline)throw expiry;return operation();}).then(resolve,reject);
      });}finally{clearTimeout(timer);}
    };
    const check=async()=>{
      try{await within(checkpoint);if(control.checkpoint)await within(()=>control.checkpoint());}
      catch(error){if(error!==expiry)interruption=error;throw error;}
    };
    const call=async(command,params)=>{
      await check();try{return await within(()=>execute(command,params));}finally{await check();}
    };
    async function state(){
      const value=await call('state');const world=value?.isolated_world;
      if(value?.platform!=='android-webview'||world?.adapter!=='android-main-document-v1'||
         world.installed!==true||value.browser_use?.available!==true)
        throw issue('unsupported_capability','This Android browser has not enabled its main-document automation adapter');
      if(typeof value.loading!=='boolean')throw issue('document_response_invalid','The native readiness flag is invalid');
      if(value.loading||world.ready!==true)throw issue(world.phase==='world_not_ready'||value.loading?'frame_not_ready':world.phase||'frame_not_ready',
        'The original main document is not ready for this operation');
      if(!Number.isSafeInteger(value.navigation)||value.navigation<1||!nativeId.test(world.native_document))
        throw issue('document_response_invalid','The native main-document identity is invalid');
      return{native:world.native_document,navigation:value.navigation,state:value};
    }
    async function evaluate(pin,name,args){
      await check();if(name==='selectOptions')possibleEffect=true;
      const result=await call('automation',{agent:true,native_document:pin.native,navigation:pin.navigation,
        timeout_ms:Math.max(1,Math.min(10000,Math.ceil(deadline-performance.now()))),
        script:`(${browserRuntime.toString()})({local_frame:true}).${name}(${JSON.stringify(args)})`});
      if(result?.ok===false&&/^[a-z][a-z0-9_]{0,63}$/.test(result.error?.code||'')){
        if(result.error.evaluation_may_have_occurred===false)possibleEffect=false;
        throw issue(result.error.code,'The native main-document executor rejected or could not confirm the operation');
      }
      if(result?.ok!==true||result.native_document!==pin.native||result.navigation!==pin.navigation||typeof result.value?.ok!=='boolean')
        throw issue('document_response_invalid','The isolated reply does not match the original native document');
      if(name==='selectOptions'&&result.value.error?.effect_may_have_occurred===false)possibleEffect=false;
      return result.value;
    }
    async function revalidate(pin){
      const current=await state();
      if(current.native!==pin.native||current.navigation!==pin.navigation)
        throw issue('stale_document','The original main document changed during the operation');
    }
    function qualify(row,pin,document){
      if(!elementId.test(row?.ref)||row.frame!=='f0')throw issue('document_response_invalid','The element does not belong to the original main document');
      return{...row,ref:`r1|f0|${pin.native}|${document}|${row.ref}`};
    }
    function target(value,pin,document){
      const out={...value};if(out.frame&&out.frame!=='f0')throw issue('unsupported_capability','Child-frame operations are unavailable in this Android adapter');
      delete out.frame;
      if(out.ref){
        if(Object.keys(out).some(key=>key!=='ref')||typeof out.ref!=='string'||out.ref.length>256)
          throw issue('target_invalid','Use one original element reference or semantic selectors');
        const parts=out.ref.split('|');
        if(parts.length!==5||parts[0]!=='r1'||parts[1]!=='f0'||!nativeId.test(parts[2])||!domId.test(parts[3])||!elementId.test(parts[4]))
          throw issue('target_invalid','The main-document element reference is malformed');
        if(parts[2]!==pin.native||parts[3]!==document)throw issue('stale_document','The element reference belongs to another main document');
        out.ref=parts[4];
      }
      return out;
    }
    function bounded(value){
      if(new TextEncoder().encode(JSON.stringify(value)).length>262144)
        throw issue('document_result_limit','The main-document result exceeds 256 KiB; narrow the observation');
      return value;
    }
    try{
      if(control.navigation_ref!==undefined)throw issue('unsupported_capability','This main-document adapter cannot prove the original native navigation loader');
      if(!allowed.has(method))throw issue('unsupported_capability','This Android adapter does not provide the requested operation');
      if(method==='prepare'&&!inputs.has(options.op))throw issue('unsupported_capability','The requested Android native input kind is not implemented');
      const captureMode=options.mode||'viewport';
      if(method==='captureArea'&&!['viewport','element'].includes(captureMode))
        throw issue(captureMode==='full_page'?'unsupported_capability':'capture_mode_invalid','This Android adapter currently captures visible viewport or element regions');
      if(!['observe','describe'].includes(method)&&!domId.test(options.document))
        throw issue('stale_document','Observe the original main document before acting');
      const pin=await state();
      if(['prepare','captureArea'].includes(method)&&pin.state.native_scale_ready!==true)throw issue('frame_not_ready','The original native scale has not been reported');
      if(['prepare','captureArea'].includes(method)&&pin.state.input_cleanup_confirmed===false&&!pin.state.input_busy)
        throw issue('input_cleanup_failed','Native input cleanup is unconfirmed; end this browser before further input');
      if(method==='captureArea'&&pin.state.input_busy)throw issue('browser_busy','Original native input has not settled before screenshot preparation');
      const meta=await evaluate(pin,'describe',{});
      if(!meta.ok||!domId.test(meta.document))throw issue('document_response_invalid','The isolated main-document identity is invalid');
      if(options.document&&options.document!==meta.document)throw issue('stale_document','The observed main document changed');
      let value;
      if(method==='describe')value=meta;
      else{
        const args={...options,document:meta.document};delete args.timeout_ms;
        if(method==='captureArea'){args.mode=captureMode;args._max_nodes=6000;args._native_viewport=true;args._opaque_media=true;}
        if(method!=='observe')args.target=target(options.target||{},pin,meta.document);
        value=await evaluate(pin,method,args);
      }
      await revalidate(pin);
      if(!value.ok){
        if(!/^[a-z][a-z0-9_]{0,63}$/.test(value.error?.code||''))throw issue('document_response_invalid','The isolated error receipt is invalid');
        const error={...value.error,effect_may_have_occurred:possibleEffect};
        if(error.candidates){if(!Array.isArray(error.candidates)||error.candidates.length>8)throw issue('document_response_invalid','The candidate list is invalid');
          error.candidates=error.candidates.map(row=>qualify(row,pin,meta.document));}
        return bounded({...value,...scope,error});
      }
      if(value.document!==meta.document)throw issue('stale_document','The isolated main document changed during the operation');
      if(method==='captureArea')return bounded(captureResult(value,pin,meta.document,captureMode));
      const out={...value,...scope};
      if(value.element)out.element=qualify(value.element,pin,meta.document);
      if(value.ref)out.ref=qualify({ref:value.ref,frame:value.frame},pin,meta.document).ref;
      if(method==='describe'||method==='observe'){
        out.url=publicUrl(value.url);out.title=String(value.title||'').slice(0,512);
        out.frames=[{id:'f0',document:pin.native,parent:null,dom:true,url:out.url}];
      }
      if(method==='observe'){
        if(!Array.isArray(value.elements)||value.elements.length>400||!Array.isArray(value.frames)||value.frames.length!==1||value.frames[0]?.id!=='f0')
          throw issue('document_response_invalid','The bounded main-document observation is malformed');
        out.elements=value.elements.map(row=>qualify(row,pin,meta.document));
      }
      if(method==='prepare'){
        if(!out.element||!value.point||![value.point.x,value.point.y].every(n=>Number.isFinite(n)&&n>=0&&n<=32768)||
           !Array.isArray(value.geometry)||value.geometry.length<6||value.geometry.length>16||!value.geometry.every(Number.isFinite))
          throw issue('document_response_invalid','The main-document input geometry is invalid');
        const ref=value.element.ref;
        out.frame_chain=[{frame:'f0',document:pin.native}];out.native_target={document:meta.document,ref};
      }
      if(method==='scrollPoint'){
        if(!elementId.test(value.hit_ref)||!value.point||![value.point.x,value.point.y].every(n=>Number.isFinite(n)&&n>=0&&n<=32768)||
           value.point.x!==options.point?.x||value.point.y!==options.point?.y)
          throw issue('document_response_invalid','The original scroll point reply is invalid');
        out.hit_ref=qualify({ref:value.hit_ref,frame:'f0'},pin,meta.document).ref;
        out.frame_chain=[{frame:'f0',document:pin.native}];out.native_target={document:meta.document,ref:value.hit_ref};
      }
      return bounded(out);
    }catch(error){
      if(interruption)throw interruption;
      return{ok:false,...scope,error:{code:typeof error?.code==='string'?error.code:'document_unavailable',
        message:typeof error?.code==='string'?String(error.message).slice(0,256):'The original main-document operation was not confirmed',effect_may_have_occurred:possibleEffect}};
    }
  }
  return Object.freeze({read});
}
