// Move only existing verified response bytes. This producer never requests a
// URL and never returns cache/staging paths or file bytes to the model.
const saves=new Map();
export function downloadSaveState(browser){
  const row=saves.get(browser);return row?Object.freeze({download:row.download,path:row.path,stage:row.stage,
    bytes:row.bytes,total:row.total,cancelled:row.cancelled}):null;
}
export async function cancelDownloadSave(browser){
  const row=saves.get(browser);if(!row)return false;row.cancelled=true;
  // Cancel only this attempt's known children. An already committed file is
  // retained and its receipt is reconciled; cancellation never means rollback.
  const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),1500);
  try{for(const id of [row.publish,row.receive])if(id)try{await row.api.call('operation.cancel',{id},{signal:abort.signal});}catch{}}
  finally{clearTimeout(timer);}return true;
}
export async function executeDownloadSave(options){
  const {browser,view,params,api,guard,fail}=options,key=browser||view;
  if(saves.has(key))return fail('browser_busy','A save for this browser is still completing; inspect or cancel it before retrying');
  const progress={api,download:params.download,path:params.path,stage:'context',bytes:0,total:0,cancelled:false};saves.set(key,progress);
  try{return await performDownloadSave({...options,progress,guard:force=>progress.cancelled?
    Promise.resolve(fail('cancelled','The save was cancelled; inspect any already published destination')):guard(force)});}
  finally{if(saves.get(key)===progress)saves.delete(key);}
}
async function performDownloadSave({params,view,api,action,guard,deadline,setDispatched,fail,progress,nativeAgent=true}){
  const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  let stage='context',stream,receive,publish,metadata,publication,publicationRequested=false,noPublicationEffect=false;
  const transfer={cleanup_pending:false};
  const native=(op,fields={})=>view.execute('downloads',{agent:nativeAgent,action:op,download:params.download,...fields});
  const phase=value=>{stage=value;progress.stage=value;};
  const annotate=result=>{
    result.result.transfer=transfer;
    if(metadata)result.result.download=metadata;
    if(publication)Object.assign(result.result,{saved:true,path:params.path,file:{size:publication.size,sha256:publication.sha256,revision:publication.revision}});
    return result;
  };
  const check=async(force=false)=>{const result=await guard(force);return result.handled?annotate(result):result;};
  const identity=async phase=>{
    const digest=new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(`browseruse-save/${phase}/${action.id}`)));
    return 'sdk-'+[...digest.subarray(0,16)].map(byte=>byte.toString(16).padStart(2,'0')).join('');
  };
  const operation=async id=>{
    for(;;){
      const current=await api.call('operation.get',{id});
      if(current.terminal){
        if(current.state!=='succeeded'){
          if(id===publish&&current.error?.may_have_completed===false)noPublicationEffect=true;
          throw Object.assign(Error('The task host did not confirm the file operation'),{code:current.error?.code||'file_operation_failed'});
        }
        return api.call('operation.result',{id});
      }
      const checked=await check();if(checked.handled)throw Object.assign(Error('Save stopped'),{checkpoint:checked});
      await pause(20);
    }
  };
  try{
    let checked=await check(true);if(checked.handled)return checked;
    if(!checked.state.download_capability)return fail('unsupported_capability','This browser cannot read a retained verified response');
    if(typeof action.context?.workspace!=='string'||!action.context.workspace||typeof params.root_snapshot!=='string'||!params.root_snapshot)
      return fail('workspace_context_missing','Save requires the original task workspace and physical root snapshot');
    if(typeof params.path!=='string'||!params.path||params.path.startsWith('/')||/[\\:\u0000-\u001f\u007f]/.test(params.path)||params.path.split('/').some(part=>!part||part==='.'||part==='..'))
      return fail('save_path_invalid','Choose a new relative destination inside the original workspace');
    const context=await api.call('sdk.context');
    const normalize=(key,value)=>key==='project'?value||'default':value||'';
    for(const key of ['extension','device','project','session','namespace','member','workspace'])
      if(normalize(key,context[key])!==normalize(key,action.context[key]))return fail('context_mismatch','The file host no longer matches the original task context');
    phase('source_metadata');metadata=await native('get');
    if(metadata.id!==params.download||metadata.state!=='completed'||!Number.isSafeInteger(metadata.size)||metadata.size<0||metadata.size>67108864||!/^[a-f0-9]{64}$/.test(metadata.sha256))
      return fail('download_not_verified','Accept and wait for this exact response to complete before saving it');
    progress.total=metadata.size;
    receive=await identity('receive');publish=await identity('publish');Object.assign(transfer,{receiver:receive,publication:publish});
    checked=await check(true);if(checked.handled)return checked;
    phase('stream_open');stream=(await api.call('stream.open',{capacity:65536})).handle;
    if(!/^str-[a-f0-9]{32}$/.test(stream||''))throw Object.assign(Error('Invalid stream'),{code:'stream_invalid'});
    phase('receiver_admission');progress.receive=receive;setDispatched(true);
    await api.call('operation.start',{id:receive,method:'files.receive.start',params:{handle:stream,root_snapshot:params.root_snapshot,
      size:metadata.size,sha256:metadata.sha256,destination:params.path,timeout_ms:Math.max(100,Math.min(600000,Math.ceil(deadline-performance.now())+1000)),idle_timeout_ms:30000}});
    let offset=0;
    while(offset<metadata.size){
      checked=await check();if(checked.handled)return checked;
      phase('source_read');const limit=Math.min(65536,metadata.size-offset),chunk=await native('read',{offset,limit});
      let decoded=-1;
      if(typeof chunk.base64==='string'&&chunk.base64.length===4*Math.ceil(limit/3))try{decoded=atob(chunk.base64).length;}catch{}
      if(chunk.id!==metadata.id||chunk.sha256!==metadata.sha256||chunk.offset!==offset||chunk.bytes!==limit||chunk.next!==offset+limit||
         chunk.eof!==(chunk.next===metadata.size)||decoded!==limit)
        throw Object.assign(Error('Invalid exact native range'),{code:'download_snapshot_changed'});
      phase('stream_write');
      for(;;){
        checked=await check();if(checked.handled)return checked;
        try{
          const written=await api.call('stream.write',{handle:stream,base64:chunk.base64});
          if(written.written!==limit)throw Object.assign(Error('Unconfirmed byte write'),{code:'stream_write_uncertain'});
          break;
        }catch(error){
          // Only this explicit zero-write backpressure result is a safe retry.
          if(error?.code!=='sdk_stream_would_block')throw error;
          const state=await api.call('operation.get',{id:receive});
          if(state.terminal)throw Object.assign(Error('Receiver stopped before consuming the next range'),{code:state.error?.code||'receiver_stopped'});
          await pause(10);
        }
      }
      offset=chunk.next;progress.bytes=offset;
    }
    checked=await check(true);if(checked.handled)return checked;
    phase('stream_eof');await api.call('stream.close',{handle:stream});
    phase('receiver_result');const staging=await operation(receive);
    if(staging.id!==receive||staging.state!=='ready'||staging.source!==`.mdo-receive-${receive.slice(4)}.part`||
       staging.workspace!==action.context.workspace||staging.root_snapshot!==params.root_snapshot||staging.destination!==params.path||staging.size!==metadata.size||staging.sha256!==metadata.sha256||typeof staging.source_revision!=='string'||!staging.source_revision)
      throw Object.assign(Error('Invalid staging receipt'),{code:'receiver_snapshot_invalid'});
    phase('publication_guard');checked=await check(true);if(checked.handled)return checked;
    const fresh=await native('get');
    if(fresh.id!==metadata.id||fresh.state!=='completed'||fresh.size!==metadata.size||fresh.sha256!==metadata.sha256)
      throw Object.assign(Error('Download ownership changed'),{code:'download_snapshot_changed'});
    checked=await check(true);if(checked.handled)return checked;
    phase('publication_admission');progress.publish=publish;publicationRequested=true;
    await api.call('operation.start',{id:publish,method:'files.receive.publish',params:{id:receive,path:params.path}});
    phase('publication_result');const file=await operation(publish);
    if(file.published!==true||file.path!==params.path||file.size!==metadata.size||file.sha256!==metadata.sha256||typeof file.revision!=='string'||!file.revision)
      throw Object.assign(Error('Unconfirmed publication'),{code:'file_publication_uncertain'});
    publication=file;checked=await check(true);if(checked.handled)return checked;
    return annotate({handled:true,result:{ok:true,saved:true,path:params.path,source_retained:true,action:action.id}});
  }catch(error){
    if(error.checkpoint)return error.checkpoint;
    const code=typeof error?.code==='string'?error.code.slice(0,128):'download_save_failed';
    const result=annotate(fail(code,publication?
      'The verified file was saved, but browser state confirmation stopped; inspect its metadata and remaining cleanup':publicationRequested?
      'File publication was not confirmed; inspect the retained publication identity and destination before retrying':
      'Save stopped before destination publication; inspect the original response and receiver before retrying'));
    result.result.error.stage=stage;
    // Internal transport messages may contain private native/staging paths.
    // Return bounded codes and recovery identities, never those raw messages.
    return result;
  }finally{
    phase('cleanup');
    const cleanup=new AbortController(),timer=setTimeout(()=>cleanup.abort(),2000),options={signal:cleanup.signal};
    const call=(method,params)=>api.call(method,params,options);
    try{
      if(publish&&publicationRequested&&!publication&&!noPublicationEffect){
        // Cancellation is not a claim that publication had no effect. It only
        // fences work that has not crossed the publisher's atomic boundary.
        try{await call('operation.cancel',{id:publish});}catch{}
      }
      if(receive&&(publication||!publicationRequested||noPublicationEffect)){
        let state;
        try{
          state=await call('operation.get',{id:receive});
          if(!state.terminal){await call('operation.cancel',{id:receive});state=await call('operation.wait',{id:receive,timeout_ms:500});}
          if(!state.terminal)transfer.cleanup_pending=true;
          else {
            try{await call('files.receive.release',{id:receive});}catch(error){if(error?.code!=='sdk_file_receive_missing')throw error;}
            await call('operation.release',{id:receive});
          }
        }catch(error){if(error?.code!=='sdk_operation_missing')transfer.cleanup_pending=true;}
      }else if(receive)transfer.cleanup_pending=true;
      if(publish&&(publication||noPublicationEffect))try{await call('operation.release',{id:publish});}catch{transfer.cleanup_pending=true;}
      if(stream)try{await call('stream.release',{handle:stream});}catch{transfer.cleanup_pending=true;}
    }finally{clearTimeout(timer);}
  }
}
