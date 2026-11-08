import {executeDownloadSave,cancelDownloadSave,downloadSaveState} from './download-save.js';

// Trusted chrome actions only. The page and model cannot supply actor flags or
// replace this frozen browser context with the currently selected conversation.
export function createDownloadUserActions({browser,sdk,execute,signal,alive=()=>true}){
  let context=Object.freeze({...browser.context,extension:browser.owner}),api=sdk.bind(context);
  let defaultReview=(context.project||'default')==='default'&&!Object.hasOwn(context,'workspace_snapshot');
  let stopped=false,active=null;
  const valid=()=>!stopped&&!signal?.aborted&&alive();
  const error=(code,message)=>Object.assign(new Error(message),{code});
  const check=()=>{if(!valid())throw error('context_changed','浏览器所属窗口或设备已改变');};
  const operationId=()=>`sdk-${crypto.randomUUID().replaceAll('-','')}`;
  const cancel=async()=>{
    const current=active;if(!current)return false;current.abort.abort();
    if(current.kind==='save')return cancelDownloadSave(browser.id);
    if(!current.id)return true; // A plain binding call may have committed; reread it.
    const timeout=new AbortController(),timer=setTimeout(()=>timeout.abort(),1500);
    try{await api.call('operation.cancel',{id:current.id},{signal:timeout.signal});}catch{}finally{clearTimeout(timer);}return true;
  };
  const inspect=async()=>{check();if(!context.workspace)throw error('workspace_context_missing','此标签没有原任务目录，请从所属任务重新创建浏览器');
    return api.call('files.receive.list',{}, {signal});};
  async function save(download,path){
    check();if(active||downloadSaveState(browser.id))throw error('browser_busy','当前浏览器仍在保存文件');
    if(!context.workspace)throw error('workspace_context_missing','此标签没有原任务目录，请从所属任务重新创建浏览器');
    const current={kind:'save',abort:new AbortController()};active=current;
    const deadline=performance.now()+60000;let dispatched=false;
    const fail=(code,message)=>({handled:true,result:{ok:false,error:{code,message,effect_may_have_occurred:dispatched}}});
    const guard=async()=>{
      if(!valid()||current.abort.signal.aborted)return fail('cancelled','保存已停止，请检查已保存文件与恢复记录');
      if(performance.now()>=deadline)return fail('timed_out','保存超时，请检查恢复记录');
      return{state:await execute('state')}; // Manual control intentionally allowed.
    };
    try{
      const root=await api.call('files.stat',{path:'.'},{signal});check();
      if(current.abort.signal.aborted)throw error('cancelled','保存已取消');
      if(Object.hasOwn(context,'workspace_snapshot')){
        const pin=typeof context.workspace_snapshot==='string'&&/^f-(\d+)-(\d+)-/.exec(context.workspace_snapshot),now=/^f-(\d+)-(\d+)-/.exec(root.revision);
        if(!pin||!now||pin[1]!==now[1]||pin[2]!==now[2])throw error('workspace_snapshot_changed','已确认的原任务目录被替换，请核对目录后继续');
      }
      const result=await executeDownloadSave({browser:browser.id,params:{download,path,root_snapshot:root.revision},view:{execute},api,
        action:{id:`uia-${crypto.randomUUID().replaceAll('-','')}`,context},guard,deadline,nativeAgent:false,
        setDispatched:value=>{dispatched=value;},fail});
      return result.result;
    }finally{if(active===current)active=null;}
  }
  const bindOriginal=row=>{
    if(row?.id!==browser.id||row.owner!==browser.owner||!row.context)throw error('context_mismatch','浏览器原任务身份不一致');
    for(const key of ['device','project','session','namespace','member']){
      const normalize=value=>key==='project'?value||'default':value||'';
      if(normalize(context[key])!==normalize(row.context[key]))throw error('context_mismatch','浏览器原任务或成员身份不一致');
    }
    if(!row.context.workspace)return false;
    if(typeof row.context.workspace!=='string'||(context.workspace&&row.context.workspace!==context.workspace)||
       (!context.workspace&&typeof row.context.workspace_snapshot!=='string'))throw error('workspace_snapshot_invalid','原任务目录绑定无法确认');
    if(Object.hasOwn(row.context,'workspace_snapshot')&&(typeof row.context.workspace_snapshot!=='string'||
       row.context.workspace_snapshot.length>256||!/^f-\d+-\d+-/.test(row.context.workspace_snapshot)))
      throw error('workspace_snapshot_invalid','原任务目录身份无法确认');
    if(Object.hasOwn(context,'workspace_snapshot')&&context.workspace_snapshot!==row.context.workspace_snapshot)
      throw error('workspace_snapshot_changed','已确认的原任务目录身份已改变');
    context=Object.freeze({...row.context,extension:browser.owner});api=sdk.bind(context);
    if(Object.hasOwn(context,'workspace_snapshot'))defaultReview=false;return true;
  };
  const workspaceCandidate=async()=>{
    check();if(active)throw error('browser_busy','请等待当前操作结束');
    const row=await api.call('browser.get',{id:browser.id},{signal});check();
    const bound=bindOriginal(row);
    if(bound&&!defaultReview)return{bound:true}; // Recover a previously lost binding reply.
    if(defaultReview){
      try{const candidate=await api.call('browser.workspace.lookup',{id:browser.id,prepare_default:true},{signal});check();return candidate;}
      catch(cause){check();if(cause?.code!=='browser_workspace_not_default')throw cause;defaultReview=false;}
      if(bound)return{bound:true}; // A custom root is never prepared as default.
    }
    const candidate=await api.call('browser.workspace.lookup',{id:browser.id},{signal});check();return candidate;
  };
  const adoptWorkspace=async candidate=>{
    check();const preparing=candidate?.prepare_default===true;
    if(active||downloadSaveState(browser.id)||(preparing?!defaultReview:Boolean(context.workspace)))
      throw error('browser_busy','请先刷新目录绑定与当前操作状态');
    if(candidate?.id!==browser.id)throw error('context_mismatch','目录确认不属于当前浏览器');
    const current={kind:'workspace',abort:new AbortController()};active=current;
    try{
      const {id,revision,session_revision,workspace,root_snapshot,root_exists}=candidate;
      const row=await api.call(preparing?'browser.workspace.prepare':'browser.workspace.adopt',
        {id,revision,session_revision,workspace,...(root_snapshot!==undefined?{root_snapshot}:{}),
         ...(preparing?{root_exists}:{}),confirmed:true},{signal:current.abort.signal});
      check();if(current.abort.signal.aborted)throw error('operation_uncertain','目录绑定结果需重新读取');
      if(!bindOriginal(row))throw error('workspace_snapshot_invalid','原任务目录绑定未确认');return row;
    }finally{if(active===current)active=null;}
  };
  async function recover(method,params){
    check();if(active||downloadSaveState(browser.id))throw error('browser_busy','请先等待或取消当前保存');
    const current={kind:'recovery',id:operationId(),abort:new AbortController()};active=current;
    const abort=()=>current.abort.abort();signal?.addEventListener('abort',abort,{once:true});
    const timer=setTimeout(abort,60000),options={signal:current.abort.signal};let terminal=false;
    try{
      await api.call('operation.start',{id:current.id,method,params},options);
      for(;;){
        check();if(current.abort.signal.aborted)throw error('cancelled','核对或发布已停止，请刷新恢复记录');
        const state=await api.call('operation.get',{id:current.id},options);
        if(state.terminal){terminal=true;
          if(state.state!=='succeeded')throw error(state.error?.code||'file_operation_failed','任务目录操作未能完成，请核对恢复记录');
          return await api.call('operation.result',{id:current.id},options);
        }
        await new Promise(resolve=>setTimeout(resolve,50));
      }
    }catch(cause){throw Object.assign(error(cause?.code||'operation_uncertain','操作未确认，请刷新并核对记录后继续'),{operation:current.id});}
    finally{
      clearTimeout(timer);signal?.removeEventListener('abort',abort);
      const cleanup=new AbortController(),timeout=setTimeout(()=>cleanup.abort(),1500);
      try{await api.call(terminal?'operation.release':'operation.cancel',{id:current.id},{signal:cleanup.signal});}catch{}finally{clearTimeout(timeout);}
      if(active===current)active=null;
    }
  }
  const stop=()=>{stopped=true;void cancel();};signal?.addEventListener('abort',stop,{once:true});
  return Object.freeze({get context(){return context;},save,inspect,cancel,workspaceCandidate,adoptWorkspace,
    get workspaceReviewAvailable(){return !context.workspace||defaultReview;},
    reconcile:id=>recover('files.receive.reconcile',{id}),
    publish:(id,path)=>recover('files.receive.publish',{id,path}),
    async release(id){check();if(active||downloadSaveState(browser.id))throw error('browser_busy','请先等待或取消当前保存');return api.call('files.receive.release',{id},{signal});},
    get busy(){return Boolean(active);},
    dispose(){stop();signal?.removeEventListener('abort',stop);}
  });
}
