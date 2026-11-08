// Bytes cross the original task SDK boundary; browser-device paths never do.
import {safeUploadName,directoryManifest} from './upload-tree.js';
export async function executeFileUpload({params,view,api,action,guard,read,deadline,setDispatched,fail}){
  let ticket=null,navigation,targetIdentity=null,stage='capability',sourceIndex=null,requested=false,cleanupRequested=false,clearStarted=false,retained=false,settled=false,completed=false;
  const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  const isDirectory=params.action==='set'&&params.directory!==undefined;
  const remaining=()=>Math.max(1,Math.ceil(deadline-performance.now()));
  const transfer=result=>{
    if(ticket)result.result.transfer={ticket,retained,assignment_requested:requested};
    return result;
  };
  const stopped=async()=>{const checked=await guard(true);if(checked.handled)return transfer(checked);
    if(isDirectory&&navigation!==undefined&&!requested){
      if(!checked.state.directory_upload_capability)return transfer(fail('unsupported_capability','This displaying device cannot receive verified directory selections; no files were assigned'));
      if(!checked.state.visible)return transfer(fail('upload_view_hidden','Reveal the browser viewport before selecting a directory'));
    }
    if(params.chooser&&params.action==='set'&&!requested){
      if(!checked.state.chooser_upload_capability)return transfer(fail('unsupported_capability','This displaying device cannot accept file pickers; no source files were read'));
      const pickers=await view.execute('chooser',{action:'list',agent:true});
      if(!pickers.choosers?.some(row=>row.id===params.chooser))return transfer(fail('chooser_changed','The exact file picker expired, was cancelled or was consumed; do not repeat its opening click'));
    }
    return navigation!==undefined&&checked.state.navigation!==navigation?transfer(fail('stale_document','The document changed during file transfer')):checked;};
  const native=params=>view.execute('upload',params);
  const targetOf=pinned=>{
    const target=pinned.native_target;
    if(target===undefined){
      if(pinned.element?.ref?.startsWith('r1|'))throw Object.assign(Error('The qualified upload target has no native frame binding'),{code:'upload_target_invalid'});
      return{document:params.document,ref:pinned.element.ref};
    }
    const chain=target?.frame_chain,bounded=value=>typeof value==='string'&&value.length>0&&value.length<=64;
    if(typeof target.document!=='string'||!target.document||target.document.length>100||!/^e[1-9][0-9]{0,12}$/.test(target.ref)||
       !Array.isArray(chain)||!chain.length||chain.length>32||chain.some(pin=>!bounded(pin?.frame)||!bounded(pin?.document))||
       chain[0].frame!==pinned.element.frame||chain.at(-1).frame!=='f0'||new Set(chain.map(pin=>pin.frame)).size!==chain.length)
      throw Object.assign(Error('The upload target returned an incomplete frame document binding'),{code:'upload_target_invalid'});
    return{document:target.document,ref:target.ref,frame_chain:chain.map(pin=>({frame:pin.frame,document:pin.document}))};
  };
  const assign=async parameters=>{
    let result,error;stage=clearStarted?'native_clear':'native_assignment';requested=true;retained=!clearStarted;setDispatched(true);
    const pending=native(parameters).then(value=>{result=value;settled=true;},value=>{error=value;settled=true;});
    while(!settled){await Promise.race([pending,pause(40)]);if(!settled){const checked=await stopped();if(checked.handled)return checked;}}
    if(error)throw error;
    if(clearStarted&&typeof result.ticket==='string')ticket=result.ticket;
    if(result.dispatched===false){setDispatched(false);retained=false;requested=false;}
    if(!result.accepted)return transfer(fail(result.error?.code||'action_uncertain',result.error?.message||'Inspect the page before another upload'));
    completed=true;retained=result.retained===true;
    return transfer({handled:true,result:{ok:true,action:action.id,...result}});
  };
  try{
    let checked=await stopped();if(checked.handled)return checked;
    if(!checked.state.file_input_capability||!(params.action==='clear'?checked.state.file_clear_capability:checked.state.upload_capability))
      return fail('unsupported_capability','This displaying device cannot receive verified native file inputs; no source files were read');
    if(isDirectory&&!checked.state.directory_upload_capability)return fail('unsupported_capability','This displaying device cannot receive verified directory selections; no source files were read');
    if(isDirectory&&(!params.directory||typeof params.directory!=='object'||Array.isArray(params.directory)||params.files!==undefined))return fail('arguments_invalid','Choose source files or one source directory, never both');
    if(params.action==='status'||params.action==='release'){
      stage='transfer_'+params.action;
      if(params.action==='release'){cleanupRequested=true;setDispatched(true);}
      const result=await native({action:params.action,ticket:params.ticket,confirmed:params.confirmed});
      return {handled:true,result:{ok:true,action:action.id,...result}};
    }
    if(checked.state.upload_cleanup_failed)return fail('upload_cleanup_failed','End this browser before another file transfer; native cleanup was not confirmed');
    while(checked.state.loading||checked.state.input_busy||checked.state.capture_busy){
      await pause(30);checked=await stopped();if(checked.handled)return checked;
    }
    navigation=checked.state.navigation;
    if(params.chooser&&!checked.state.chooser_upload_capability)return fail('unsupported_capability','This browser cannot accept intercepted file pickers; no source files were read');
    stage='target_resolution';
    let pinned=params.chooser?await read('chooserInfo',{chooser:params.chooser,document:params.document}):await read('uploadInfo',{document:params.document,target:params.target});
    if(!pinned.ok)return{handled:true,result:pinned};
    targetIdentity=targetOf(pinned);
    if(params.action==='clear'){
      checked=await stopped();if(checked.handled)return checked;
      clearStarted=true;
      return await assign({action:'clear',upload_key:action.id,navigation,...targetIdentity,timeout_ms:remaining()});
    }
    if(Boolean(pinned.element.directory)!==isDirectory||!isDirectory&&params.files.length>1&&!pinned.element.multiple)
      return fail('upload_target_invalid','Choose an enabled input matching the file or directory selection');
    const snapshots=[];let total=0,tree=null,treeRevision=null;
    const treeOptions=isDirectory?{path:params.directory.path,...(params.directory.space?{space:params.directory.space}:{})}:null;
    if(isDirectory){
      if(typeof treeOptions.path!=='string'||!treeOptions.path||/[\u0000-\u001f\u007f]/.test(treeOptions.path))return fail('upload_path_invalid','Choose an unambiguous source directory path without control characters');
      checked=await stopped();if(checked.handled)return checked;
      stage='source_directory_name';
      const resolved=params.directory.name===undefined?await api.call('files.resolve',treeOptions):null;
      if(resolved!==null&&(typeof resolved?.path!=='string'||!resolved.path))return fail('upload_snapshot_invalid','The task host did not resolve the selected directory name');
      const absolute=resolved?.path?.replace(/[\\/]+$/,'');
      const name=params.directory.name??(/^[a-z]:$/i.test(absolute)?absolute[0]:absolute?.split(/[\\/]/).at(-1)||'root');
      if(!safeUploadName(name))return fail('upload_name_invalid','Choose a safe selected-directory basename or provide an explicit safe name');
      checked=await stopped();if(checked.handled)return checked;
      stage='source_directory_snapshot';const snapshot=await api.call('files.tree',treeOptions);
      tree=directoryManifest(snapshot,name);treeRevision=snapshot.revision;
      if(!tree.files.length)return fail('upload_directory_empty','The selected directory contains no files; its empty directories cannot form an HTML FileList');
      for(const file of snapshot.files)snapshots.push({source:{path:treeOptions.path.replace(/[\\/]+$/,'')+'/'+file.path,...(treeOptions.space?{space:treeOptions.space}:{})},
        name:file.path.split('/').at(-1),path:file.path,size:file.size,sha256:file.sha256,revision:file.revision});
    }else for(const file of params.files){
      sourceIndex=snapshots.length;stage='source_snapshot';
      checked=await stopped();if(checked.handled)return checked;
      if(typeof file.path!=='string'||/[\u0000-\u001f\u007f]/.test(file.path))return fail('upload_path_invalid','Choose an unambiguous source path without control characters');
      const name=file.name??file.path.split(/[\\/]/).at(-1);
      if(!safeUploadName(name))
        return fail('upload_name_invalid','Choose a safe file basename or provide an explicit safe name');
      const snapshot=await api.call('files.digest',{path:file.path,...(file.space?{space:file.space}:{}),max_bytes:33554432});
      if(!Number.isSafeInteger(snapshot.size)||snapshot.size<0||snapshot.size>33554432||typeof snapshot.revision!=='string'||!snapshot.revision||!/^[a-f0-9]{64}$/.test(snapshot.sha256))
        return fail('upload_snapshot_invalid','The task host did not return a complete bounded source snapshot');
      total+=snapshot.size;if(total>67108864)return fail('upload_budget_exceeded','The upload exceeds its 64 MiB total byte budget');
      snapshots.push({source:file,name,size:snapshot.size,sha256:snapshot.sha256,revision:snapshot.revision});
    }
    checked=await stopped();if(checked.handled)return checked;
    if(checked.state.navigation!==navigation)return fail('stale_document','The document changed before transfer admission');
    sourceIndex=null;stage='native_admission';
    const admitted=await native({action:'begin',navigation,upload_key:action.id,timeout_ms:Math.min(120000,remaining()+5000),
      ...(params.chooser?{chooser:params.chooser,document:params.document,ref:pinned.element.ref}:{}),
      ...(pinned.native_target?targetIdentity:{}),
      ...(tree?{directory:tree}:{files:snapshots.map(file=>({name:file.name,size:file.size,sha256:file.sha256}))})});
    if(typeof admitted.ticket!=='string'||!/^u-[a-f0-9-]{36}$/i.test(admitted.ticket))return fail('upload_receipt_invalid','The browser did not return a valid staging ticket');
    ticket=admitted.ticket;
    for(let file=0;file<snapshots.length;file++){
      sourceIndex=file;
      const snapshot=snapshots[file];let offset=0;
      while(offset<snapshot.size){
        checked=await stopped();if(checked.handled)return checked;
        if(checked.state.navigation!==navigation)return transfer(fail('stale_document','The document changed during byte transfer'));
        stage='source_read';const limit=Math.min(65536,snapshot.size-offset),chunk=await api.call('files.read',{
          path:snapshot.source.path,...(snapshot.source.space?{space:snapshot.source.space}:{}),revision:snapshot.revision,offset,limit});
        if(chunk.revision!==snapshot.revision||chunk.bytes!==limit||chunk.next!==offset+limit||typeof chunk.base64!=='string'||chunk.base64.length>87384)
          return transfer(fail('upload_snapshot_changed','The source range did not match its pinned revision; no file was assigned'));
        checked=await stopped();if(checked.handled)return checked;
        stage='native_chunk';await native({action:'chunk',ticket,navigation,file,offset,base64:chunk.base64});offset=chunk.next;
      }
    }
    checked=await stopped();if(checked.handled)return checked;
    sourceIndex=null;stage='native_seal';const sealed=await native({action:'seal',ticket,navigation});
    if(!sealed.sealed)return transfer(fail('upload_snapshot_invalid','The receiving browser could not verify complete file bytes'));
    if(tree){
      checked=await stopped();if(checked.handled)return checked;
      stage='source_directory_revalidation';const current=await api.call('files.tree',{...treeOptions,revision:treeRevision});
      if(current.revision!==treeRevision||JSON.stringify(directoryManifest(current,tree.name))!==JSON.stringify(tree))return transfer(fail('upload_snapshot_changed','The source directory changed during transfer; no files were assigned'));
    }
    // Revalidate the pinned reference, never a newly matching replacement node.
    stage='target_revalidation';pinned=await read('uploadInfo',{document:params.document,target:{ref:pinned.element.ref}});
    if(!pinned.ok)return transfer({handled:true,result:pinned});
    if(JSON.stringify(targetOf(pinned))!==JSON.stringify(targetIdentity))return transfer(fail('upload_target_changed','The sealed transfer no longer belongs to its original frame document; no file was assigned'));
    checked=await stopped();if(checked.handled)return checked;
    if(checked.state.navigation!==navigation)return transfer(fail('stale_document','The document changed before file assignment'));
    return await assign({action:'assign',ticket,navigation,...targetIdentity,timeout_ms:remaining()});
  }catch(error){
    const result=transfer(fail(requested||cleanupRequested?'action_uncertain':error?.code||'upload_transfer_failed',requested?
      'File assignment was not confirmed; inspect the page and retained transfer before repeating':
      cleanupRequested?'Transfer release was not confirmed; inspect its ticket before retrying':
      params.action==='status'?'The transfer status request failed; inspect its stage and cause':
      'The file transfer stopped before native assignment; inspect the failing stage and cause'));
    result.result.error.stage=stage;
    if(sourceIndex!==null)result.result.error.source_index=sourceIndex;
    result.result.error.cause={code:String(error?.code||'transport_failed').slice(0,128),message:String(error?.message||error).slice(0,512)};
    return result;
  }finally{
    if(clearStarted&&!completed)try{await native({action:'clear_cancel',upload_key:action.id});}catch{}
    else if(ticket&&!completed)try{await native({action:'cancel',ticket});}catch{}
  }
}
