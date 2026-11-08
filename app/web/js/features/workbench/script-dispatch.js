const frameId = /^(?:f0|f-[a-f0-9-]{36})$/i;
const frameDocument = /^fd-[a-f0-9-]{36}$/i;

// Scripts get a separate execution world when requested. They never run inside
// the trusted Browser Use runtime or receive the mdo main-window bridge.
export function browserScriptExpression(script, maximum) {
  return `(async()=>{const maximum=${maximum};try{const value=await(async()=>{\n${script}\n})();
    const type=value===null?'null':typeof value;let nodes=0,used=0;
    const text=JSON.stringify(value===undefined?null:value,(key,item)=>{
      if(++nodes>4096)throw new Error('script_result_limit');
      used+=key.length+8+(typeof item==='string'?item.length:0);
      if(used>maximum)throw new Error('script_result_limit');
      if(['bigint','function','symbol'].includes(typeof item))throw new Error('script_result_not_json');
      return item;});
    if(typeof text!=='string')throw new Error('script_result_not_json');
    if(new TextEncoder().encode(text).length>maximum)throw new Error('script_result_limit');
    return{ok:true,value:JSON.parse(text),value_type:type};
  }catch(error){let message='JavaScript execution failed';try{message=String(error?.message||error).slice(0,1024);}catch{}
    return{ok:false,error:{code:message==='script_result_limit'?'script_result_limit':message==='script_result_not_json'?'script_result_not_json':'javascript_error',message}};}})()`;
}

export async function executeBrowserScript({params,view,read,guard,deadline,arm,setDispatched,fail,action}) {
  const script=params.script,maximum=params.max_bytes??16384,world=params.world??'page',selected=params.frame??'f0';
  if(typeof script!=='string'||!script.trim()||new TextEncoder().encode(script).length>65536||
     !['page','isolated'].includes(world)||!frameId.test(selected)||!Number.isInteger(maximum)||maximum<256||maximum>65536||
     selected!=='f0'&&!frameDocument.test(params.frame_document||'')||params.frame_document!==undefined&&!frameDocument.test(params.frame_document))
    return fail('script_arguments_invalid','Use a bounded script body, explicit current document and observed frame identity');
  let checked=await guard(true);if(checked.handled)return checked;
  if(checked.state.platform!=='webview2'||checked.state.script_execution!==true)
    return fail('unsupported_capability','This browser host does not support pinned JavaScript execution');
  const snapshot=await read('observe',{limit:1,max_bytes:256});
  if(!snapshot?.ok)return{handled:true,result:snapshot};
  if(snapshot.document!==params.document)return fail('stale_document','Observe the current document before executing JavaScript');
  const frame=snapshot.frames?.find(row=>row.id===selected);
  if(!frame||!frameDocument.test(frame.document)||frame.loading||frame.dom!==true)
    return fail('frame_not_ready','Select one ready iframe from the current observation');
  if(params.frame_document!==undefined&&params.frame_document!==frame.document)
    return fail('stale_frame','The selected iframe document changed; observe it before another execution');
  const boundary=await arm();if(boundary)return boundary;
  checked=await guard(true);if(checked.handled)return checked;
  if(checked.state.input_busy)return fail('browser_busy','Original native input is still completing; no script was dispatched');
  const remaining=Math.min(10000,Math.floor(deadline-performance.now()));
  if(remaining<100)return fail('timed_out','The original script deadline expired before dispatch');
  setDispatched(true);
  const native=await view.execute('frames',{action:'script',agent:true,frame:selected,document:frame.document,world,
    script:browserScriptExpression(script,maximum),timeout_ms:remaining});
  checked=await guard(true);if(checked.handled)return checked;
  if(native?.ok!==true){
    if(native?.error?.evaluation_may_have_occurred===false)setDispatched(false);
    const code=/^[a-z][a-z0-9_]{0,63}$/.test(native?.error?.code||'')?native.error.code:'script_execution_uncertain';
    return fail(code,'JavaScript execution was not confirmed; inspect the original page before repeating it');
  }
  if(native.frame!==selected||native.frame_document!==frame.document||native.world!==world||typeof native.value?.ok!=='boolean')
    return fail('script_response_invalid','The script result is not bound to its original frame and execution world');
  if(native.value.ok!==true){
    const code=['javascript_error','script_result_limit','script_result_not_json'].includes(native.value.error?.code)?native.value.error.code:'javascript_error';
    return fail(code,String(native.value.error?.message||'JavaScript execution failed').slice(0,1024));
  }
  const encoded=JSON.stringify(native.value.value);
  if(typeof encoded!=='string'||new TextEncoder().encode(encoded).length>maximum)
    return fail('script_result_limit','The script result exceeds its requested JSON budget');
  return{handled:true,result:{ok:true,action:action.id,document:params.document,frame:selected,frame_document:frame.document,world,
    value:native.value.value,value_type:native.value.value_type,dispatched:true,verification:'javascript_return'}};
}
