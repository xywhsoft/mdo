// Private response bytes never enter model results or persistent UI receipts.
export async function executeBrowserDownload({params,view,guard,deadline,setDispatched,fail}){
  const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  const readOnly=['list','get','wait'].includes(params.action);
  const native=(action,fields={})=>view.execute('downloads',{agent:true,action,download:params.download,...fields});
  let stage='capability',started=false;
  try{
    let checked=await guard(true);if(checked.handled)return checked;
    if(!checked.state.download_capability)return fail('unsupported_capability','This browser cannot intercept and verify native response downloads');
    if(params.action==='wait'){
      stage='wait';
      for(;;){
        const download=await native('get');
        checked=await guard(true);if(checked.handled)return checked;
        if(download.state==='completed')return{handled:true,result:{ok:true,completed:true,verified:true,download}};
        if(['failed','cancelled'].includes(download.state)){
          const result=fail(download.error||'download_interrupted','The browser response did not complete; inspect its download receipt instead of repeating the click');
          result.result.download=download;return result;
        }
        if(performance.now()+50>=deadline)return{handled:true,result:{ok:true,completed:false,outcome:download.state==='pending'?'download_pending':'download_in_progress',download}};
        await pause(50);checked=await guard(true);if(checked.handled)return checked;
      }
    }
    stage=params.action;
    if(!readOnly){started=true;setDispatched(true);}
    const result=await native(params.action,params.action==='accept'?{
      ...(params.download_max_bytes?{max_bytes:params.download_max_bytes}:{}),
      ...(params.transfer_timeout_ms?{timeout_ms:params.transfer_timeout_ms}:{})}:{});
    if(params.action==='list')return{handled:true,result:{ok:true,...result}};
    if(params.action==='release')return{handled:true,result:{ok:true,...result}};
    if(params.action==='accept'&&['failed','cancelled'].includes(result.state)){
      const rejected=fail(result.error||'download_rejected','The browser response was not accepted; inspect its terminal download receipt');
      rejected.result.download=result;return rejected;
    }
    return{handled:true,result:{ok:true,completed:result.state==='completed',verified:result.state==='completed',download:result}};
  }catch(error){
    const result=fail(started?'action_uncertain':'download_unavailable',started?
      'The download mutation was not confirmed; inspect its identity before retrying':
      'The browser download could not be inspected');
    result.result.error.stage=stage;result.result.error.cause={code:String(error?.code||'native_failed').slice(0,128),message:String(error?.message||error).slice(0,512)};
    return result;
  }
}
