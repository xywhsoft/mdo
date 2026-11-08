import {createDownloadUserActions} from './download-user.js';
import {downloadSaveState,cancelDownloadSave} from './download-save.js';

const nativeLabels={identifying:'正在识别响应',pending:'等待接受',receiving:'正在下载',verifying:'正在校验',completed:'已校验 · 等待保存',cancelling:'正在取消',cancelled:'已取消',failed:'下载失败'};
const phaseLabels={context:'正在准备',source_metadata:'核对响应',stream_open:'准备传输',receiver_admission:'准备接收',source_read:'读取响应',stream_write:'传输文件',stream_eof:'等待校验',receiver_result:'校验文件',publication_guard:'核对保存条件',publication_admission:'发布文件',publication_result:'确认保存结果',cleanup:'清理临时数据'};
const bytes=value=>!Number.isSafeInteger(value)||value<0?'大小未知':value<1024?`${value} B`:value<1048576?`${(value/1024).toFixed(1)} KiB`:`${(value/1048576).toFixed(1)} MiB`;
const filename=value=>(String(value||'download.bin').replace(/[\\/:*?"<>|\u0000-\u001f\u007f]/g,'_').replace(/[. ]+$/,'').slice(0,200)||'download.bin');
const message=error=>{
  const code=String(error?.code||'browser_download_failed').slice(0,128);
  const hints={sdk_file_exists:'目标文件已存在，请输入新的文件名；已有文件会保留。',sdk_workspace_conflict:'原任务目录已变化，请检查目录后再操作。',
    workspace_context_missing:'请先读取并确认此标签的原任务目录。',save_path_invalid:'请输入原任务目录内的新文件相对路径。',
    browser_workspace_task_missing:'原任务已不可用，无法恢复目录。下载响应仍会保留，请先恢复原任务。',
    browser_workspace_unavailable:'原任务目录不存在或无法确认，请恢复原目录后重新读取。',
    browser_workspace_conflict:'任务或目录已变化，请重新读取原任务目录并确认。',
    browser_workspace_confirmation:'请先检查原任务目录，再确认准备或使用此目录。',
    browser_workspace_failed:'目录操作结果尚未确认，请重新读取绑定后再继续。',
    workbench_save_failed:'目录绑定结果尚未确认，请重新读取原任务目录，避免重复操作。',
    browser_workspace_bound:'此标签已有目录身份，请恢复原目录后再保存。',
    revision_conflict:'标签状态已更新，请重新读取原任务目录后再确认。',
    workspace_snapshot_changed:'已确认的原任务目录被替换，请先恢复原目录；下载响应仍会保留。',
    sdk_arguments_invalid:'请检查文件相对路径，目录必须已存在。',sdk_file_receive_limit:'暂存空间已满，请核对并清理待处理记录后继续。',
    browser_busy:'当前浏览器仍在保存，请等待或取消当前保存。',cancelled:'操作已停止；已经保存的文件会保留，请核对恢复记录。',
    context_changed:'显示设备或窗口已改变，操作已停止，请回到原任务核对。',sdk_file_receive_receipt:'保存收据未确认，请先核对恢复记录，避免重复保存。'};
  return`${hints[code]||'操作未确认，请刷新状态或核对恢复记录后继续。'}（${code}）`;
};

// An in-flow tray resizes the native surface instead of floating over its
// separate WebView HWND. Keep DOM nodes stable while paths are being edited.
export function createBrowserDownloads({browser,sdk,execute,signal,alive,onChange,onBadge}){
  const make=(tag,cls,text)=>{const node=document.createElement(tag);node.className=cls;if(text!==undefined)node.textContent=text;return node;};
  const actions=createDownloadUserActions({browser,sdk,execute,signal,alive}),root=make('section','wb-downloads'),
    heading=make('div','wb-download-heading'),title=make('strong','','下载'),refreshButton=make('button','wb-button','刷新'),cancelButton=make('button','wb-button','取消操作'),
    info=make('p','wb-download-info'),binding=make('div','wb-download-binding'),candidateText=make('p','wb-download-info'),
    lookupButton=make('button','wb-button','读取原任务目录'),adoptButton=make('button','wb-button','确认使用此目录'),
    notice=make('p','wb-download-notice'),list=make('div','wb-download-list'),recoverTitle=make('strong','wb-download-recovery-title','原任务的待处理记录'),
    recovery=make('div','wb-download-list'),empty=make('p','wb-download-empty','暂无下载。网页提供文件后，会在这里显示。');
  root.hidden=true;root.setAttribute('role','region');root.setAttribute('aria-label','浏览器下载');notice.setAttribute('role','status');
  refreshButton.type=cancelButton.type=lookupButton.type=adoptButton.type='button';cancelButton.hidden=true;cancelButton.onclick=()=>{void actions.cancel();};heading.append(title,cancelButton,refreshButton);
  adoptButton.hidden=true;binding.append(candidateText,lookupButton,adoptButton);root.append(heading,info,binding,notice,list,empty,recoverTitle,recovery);
  const nativeRows=new Map(),recoveryRows=new Map();let disposed=false,busy=false,refreshing=false,latest={},nativeError=false,recoveryError=false,serial=0,workspaceReview=null;
  const changed=()=>onChange?.();
  const button=(label,fn,local=false)=>{const node=make('button','wb-button',label);node.type='button';node.onclick=()=>{if(local)fn();else void run(fn);};return node;};
  const describe=(row,text,isError=false)=>{row.status.textContent=text;row.status.classList.toggle('wb-error',isError);};
  // Polls must not detach a focused button or swallow a press between down/up.
  const retainControls=(row,previous)=>{
    const wanted=[...row.controls.children],old=[...previous.children];
    if(old.length===wanted.length&&old.every((node,index)=>node.textContent===wanted[index].textContent)){
      for(let index=0;index<old.length;index++){old[index].disabled=wanted[index].disabled;old[index].onclick=wanted[index].onclick;}
      row.controls=previous;
    }else previous.replaceWith(row.controls);
  };
  const run=async fn=>{
    if(disposed||busy)return;busy=true;serial++;notice.textContent='';paint();
    try{await fn();}catch(error){if(!disposed)notice.textContent=message(error);}
    finally{busy=false;if(!disposed){await refresh(latest,true);paint();}}
  };
  lookupButton.onclick=()=>{void run(async()=>{
    workspaceReview=null;adoptButton.hidden=true;candidateText.textContent='';
    const candidate=await actions.workspaceCandidate();if(candidate.bound){notice.textContent='已读取原任务的目录绑定。';return;}
    workspaceReview=candidate;
    const preparing=candidate.prepare_default===true&&candidate.root_exists===false;
    candidateText.textContent=`原任务目录：${candidate.workspace}。${preparing?'目录尚未创建，确认后将准备此默认目录。':'确认后将用于此标签的文件保存。'}`;
    adoptButton.textContent=preparing?'准备并确认此目录':'确认使用此目录';adoptButton.hidden=false;
  });};
  adoptButton.onclick=()=>{void run(async()=>{
    if(!workspaceReview)return;const reviewed=workspaceReview;workspaceReview=null;adoptButton.hidden=true;
    await actions.adoptWorkspace(reviewed);candidateText.textContent='';notice.textContent='原任务目录已确认；可直接保存已接收的响应。';
  });};
  function rowNode(id,kind){
    const node=make('article','wb-download-row'),name=make('strong','wb-download-name'),status=make('span','wb-download-state'),
      progress=make('progress','wb-download-progress'),controls=make('div','wb-download-actions'),form=make('form','wb-download-save'),
      path=make('input','wb-download-path'),submit=make('button','wb-button','保存'),dismiss=make('button','wb-button','取消'),
      detail=make('span','wb-download-detail');
    node.dataset.download=id;node.dataset.kind=kind;path.setAttribute('aria-label','原任务目录中的新文件相对路径');path.placeholder='输入相对路径，如 exports/report.bin';path.autocomplete='off';path.spellcheck=false;
    form.hidden=true;submit.type='submit';dismiss.type='button';dismiss.onclick=()=>{form.hidden=true;changed();};form.append(path,submit,dismiss);
    node.append(name,status,progress,controls,form,detail);return{node,name,status,progress,controls,form,path,submit,detail,data:null,saved:null,confirm:false};
  }
  const newPath=(row,value)=>{if(!row.path.value)row.path.value=value;row.form.hidden=false;row.path.focus();changed();};
  function paint(){
    const saving=downloadSaveState(browser.id),locked=busy||Boolean(saving),disabled=disposed||nativeError;
    info.textContent=actions.context.workspace?`保存到原任务目录：${actions.context.workspace}`:'此标签尚未记录原任务目录。可先接收下载，再读取原任务目录并确认保存位置。';
    binding.hidden=!actions.workspaceReviewAvailable;lookupButton.textContent=actions.context.workspace?'检查任务目录':'读取原任务目录';
    lookupButton.disabled=adoptButton.disabled=locked;
    for(const [id,row]of nativeRows){
      const previous=row.controls;row.controls=previous.cloneNode(false);
      const value=row.data,active=saving?.download===id;row.name.textContent=String(value.name||'下载文件');row.name.title=row.name.textContent;
      const label=active?`${phaseLabels[saving.stage]||'正在保存'} · ${bytes(saving.bytes)} / ${bytes(saving.total)}`:
        row.saved?`已保存：${row.saved.path}${row.saved.cleanup_pending?' · 临时数据待清理':''}`:
        `${nativeLabels[value.state]||'状态待确认'} · ${bytes(value.state==='completed'?value.size:value.received)}${value.expected_size>0?' / '+bytes(value.expected_size):''}`;
      describe(row,label,value.state==='failed');row.detail.textContent=row.confirm?'尚未保存的响应将被丢弃。再次点击确认。':value.error?`下载原因：${String(value.error).slice(0,128)}`:'';
      row.progress.hidden=!active&&!['receiving','verifying'].includes(value.state);row.progress.max=active?saving.total||1:value.expected_size||1;
      if(active||value.expected_size>0)row.progress.value=active?saving.bytes:value.received||0;else row.progress.removeAttribute('value');
      row.controls.replaceChildren();
      if(value.state==='pending')row.controls.append(button('接受下载',async()=>{await execute('downloads',{action:'accept',download:id,agent:false});}));
      if(['identifying','pending','receiving','verifying'].includes(value.state))row.controls.append(button('取消下载',async()=>{await execute('downloads',{action:'cancel',download:id,agent:false});}));
      if(value.state==='completed')row.controls.append(button('保存到任务',()=>newPath(row,filename(value.name)),true));
      if(['completed','failed','cancelled'].includes(value.state))row.controls.append(button(value.state==='completed'&&!row.saved?(row.confirm?'确认丢弃':'丢弃未保存响应'):'清除记录',async()=>{
        if(value.state==='completed'&&!row.saved&&!row.confirm){row.confirm=true;return;}
        await execute('downloads',{action:'release',download:id,agent:false});
      }));
      for(const control of row.controls.children)control.disabled=locked||disabled||(control.textContent==='保存到任务'&&!actions.context.workspace);
      row.submit.disabled=locked||disabled;row.path.disabled=locked;
      if(active)row.controls.append(button(saving.cancelled?'正在取消…':'取消保存',async()=>{await cancelDownloadSave(browser.id);}));
      // Cancellation must remain usable while a save owns the ordinary buttons.
      if(active){const cancel=row.controls.lastElementChild;cancel.disabled=Boolean(saving.cancelled);cancel.onclick=()=>{void cancelDownloadSave(browser.id).then(paint);};}
      retainControls(row,previous);
    }
    for(const row of recoveryRows.values()){
      const previous=row.controls;row.controls=previous.cloneNode(false);
      const value=row.data;row.name.textContent=value.destination||'未指定保存位置';row.name.title=row.name.textContent;
      const label=value.active?'操作仍在运行':value.state==='published'?'上次已确认保存 · 可再次核对':value.state==='publishing'?'保存结果未确认 · 请先核对':value.state==='ready'?'文件已校验 · 等待明确保存':'接收已中断 · 不会自动重放';
      describe(row,`${label} · ${bytes(value.size)}`);row.progress.hidden=true;row.controls.replaceChildren();
      if(!value.active&&['ready','publishing','published'].includes(value.state)&&value.destination)row.controls.append(button('核对结果',async()=>{
        const result=await actions.reconcile(value.id);notice.textContent=result.publication_verdict==='verified'?'已核对原目标文件，完整哈希一致。':result.publication_verdict==='missing'?'原目标不存在，暂存文件完整。确认路径后可保存。':'目标是其他文件，已保留。请为暂存文件选择新的保存路径。';
      }));
      if(!value.active&&value.state==='ready')row.controls.append(button('保存暂存文件',()=>newPath(row,value.destination||'download.bin'),true));
      if(!value.active)row.controls.append(button(row.confirm?'确认清理':'清理暂存',async()=>{
        if(!row.confirm){row.confirm=true;row.detail.textContent=value.state==='published'?'仅清理暂存链接，保留已保存文件。再次点击确认。':'尚未保存的暂存数据将被丢弃。再次点击确认。';return;}
        await actions.release(value.id);
      }));
      for(const control of row.controls.children)control.disabled=locked||recoveryError;row.submit.disabled=locked||recoveryError;row.path.disabled=locked;
      retainControls(row,previous);
    }
    const count=nativeRows.size+recoveryRows.size;empty.hidden=count>0;recoverTitle.hidden=recoveryRows.size===0;refreshButton.disabled=busy||refreshing;
    cancelButton.hidden=!actions.busy;onBadge?.(nativeRows.size,recoveryRows.size);changed();
  }
  function updateRows(values,map,container,kind){
    const ids=new Set();for(const value of values){if(!value||typeof value.id!=='string')continue;ids.add(value.id);
      let row=map.get(value.id);if(!row){row=rowNode(value.id,kind);map.set(value.id,row);container.append(row.node);
        row.form.onsubmit=event=>{event.preventDefault();void run(async()=>{
          if(kind==='download'){
            const result=await actions.save(value.id,row.path.value.trim());
            if(result.saved){row.saved={path:result.path,cleanup_pending:result.transfer?.cleanup_pending};row.form.hidden=true;}
            if(!result.ok)throw Object.assign(Error('Save requires review'),{code:result.error?.code});
          }else{await actions.publish(value.id,row.path.value.trim());row.form.hidden=true;notice.textContent='文件已保存；暂存记录可按需清理。';}
        });};
      }if(row.data?.state!==value.state){row.confirm=false;row.detail.textContent='';}row.data=value;
    }
    for(const [id,row]of map)if(!ids.has(id)){row.node.remove();map.delete(id);}
  }
  async function refresh(state={},force=false){
    latest=state;if(disposed||refreshing)return;refreshing=true;const observed=serial;
    try{
      if(state.download_capability){
        try{const result=await execute('downloads',{action:'list',agent:false});if(disposed||observed!==serial)return;
          nativeError=false;updateRows((result.downloads||[]).slice(0,16),nativeRows,list,'download');
        }catch(error){nativeError=true;if(!root.hidden)notice.textContent=message(error);}
      }else{nativeError=true;if(!root.hidden)notice.textContent='当前浏览器后端不支持受控下载。';}
      if((force||!root.hidden)&&actions.context.workspace){
        try{const values=await actions.inspect();if(disposed||observed!==serial)return;recoveryError=false;updateRows(values.slice(0,8),recoveryRows,recovery,'recovery');}
        catch(error){recoveryError=true;notice.textContent=message(error);}
      }
    }finally{refreshing=false;if(!disposed)paint();}
  }
  refreshButton.onclick=()=>{void refresh(latest,true);};
  return Object.freeze({element:root,refresh,
    toggle(){if(disposed)return;root.hidden=!root.hidden;if(!root.hidden)void refresh(latest,true);changed();return!root.hidden;},
    dispose(){disposed=true;serial++;actions.dispose();root.hidden=true;refreshButton.onclick=cancelButton.onclick=lookupButton.onclick=adoptButton.onclick=null;for(const row of [...nativeRows.values(),...recoveryRows.values()]){row.form.onsubmit=null;row.controls.replaceChildren();}nativeRows.clear();recoveryRows.clear();}
  });
}
