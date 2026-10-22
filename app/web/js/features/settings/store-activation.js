export async function reloadStorePluginRuntime(call, {wait = ms=>new Promise(resolve=>setTimeout(resolve,ms)), attempts = 60} = {}) {
  const receipt=await call("runtime.reload",{domain:"host"});
  if(!receipt?.id)throw Error("Runtime did not return a reload receipt");
  for(let n=0;n<attempts;n++){
    let result=null;try{result=await call("runtime.reload.status",{id:receipt.id});}catch{/* Transport can reconnect while the host changes generation. */}
    if(result?.state===3)return result;
    if(result?.state>=4)throw Error(result.message||"Plugin runtime reload failed; the previous generation is retained");
    if(n+1<attempts)await wait(750);
  }
  throw Error("Runtime reload has not completed; inspect its status before retrying");
}
