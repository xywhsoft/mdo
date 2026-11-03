import { api } from "../api/client.js";
import { targetState, subscribeTarget } from "../api/target.js";

export function isExtensionUpdate(local,remote){const old=local?.installation;return Boolean(old&&local.source==="store"&&local.id===remote?.id&&old.xid===remote.xid&&Number(old.serial)>0&&Number(remote.serial)>Number(old.serial)&&old.sha256!==remote.sha256);}

export function createExtensionUpdateState({transport=api,target=()=>targetState().selected?.id||"local"}={}){
  const listeners=new Set();let owner=target(),epoch=0,pending=null,requested=false,snapshot={items:[],stale:true,busy:false};
  const publish=()=>{for(const fn of listeners)fn(snapshot);};
  function reset(){owner=target();epoch++;pending=null;requested=false;snapshot={items:[],stale:true,busy:false};publish();}
  async function refresh({check=false}={}){
    if(owner!==target())reset();if(pending){if(check)requested=true;return pending;}
    const current=owner,n=epoch;
    const own=Promise.resolve().then(async()=>{try{
      const response=check?await transport.post("/ecosystem",{action:"check_updates"}):await transport.get("/ecosystem?scope=updates");
      if(n!==epoch||current!==target())return;
      if(!response.data||!Array.isArray(response.data.items))throw Error("Invalid extension update snapshot");
      snapshot=response.data;publish();return snapshot;
    }catch(error){if(n===epoch&&current===target()){snapshot={...snapshot,stale:true,message:error.message};publish();}return snapshot;}
    finally{if(pending===own){pending=null;if(requested){requested=false;void refresh({check:true});}}}});pending=own;return own;
  }
  return {refresh,reset,get:()=>snapshot,find(item){const row=snapshot.items.find(row=>row.id===item.id&&row.kind===item.kind);return row&&(!item.source||item.source==="store"||item.installed_sha256||item.source==="builtin"&&row.delivery==="apk")?row:undefined;},subscribe(fn){listeners.add(fn);return()=>listeners.delete(fn);}};
}
export const extensionUpdates=createExtensionUpdateState();
let scope=targetState().selected?.id||"local";
subscribeTarget(()=>{const next=targetState().selected?.id||"local";if(scope!==next||targetState().runtimeChanged){scope=next;extensionUpdates.reset();}});
globalThis.window?.addEventListener("mdo-extensions-changed",()=>void extensionUpdates.refresh({check:true}));
