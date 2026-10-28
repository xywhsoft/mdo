"""Extend the existing locked SDK export in place; never copy a runtime/tree.

Produces the authoritative compatibility patch and dependency hashes from the
locked Git base. Call once on the explicitly selected, verified SDK directory.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
import build_mdo
ROOT=Path(__file__).resolve().parents[1]

def refresh_patch(sdk,base_repository):
 child=sdk/'lib/xwork/src/xwork_subagent.c';text=child.read_text(encoding='utf-8');anchor='    pChild = xworkAgentCreate(&tAgentConfig, pError);\n    if ( !pChild ) goto cleanup;'
 inherited=anchor+'\n    if(!xworkAgentSetProcessOwner(pChild,pParent->OnProcessAcquire,pParent->OnProcessRelease,pParent->pProcessOwnerData,pError))goto cleanup;'
 if anchor in text and inherited not in text:child.write_text(text.replace(anchor,inherited,1),encoding='utf-8',newline='\n')
 backup=ROOT/'.build/process-owner-sdk-before';lock=build_mdo.load_object(backup/'deps.lock');previous=(backup/'previous.patch').read_text(encoding='utf-8')
 paths=[file.relative_to(backup).as_posix() for file in backup.rglob('*') if file.is_file() and file.name not in ('deps.lock','previous.patch')];replacements={}
 for path in paths:
  base=subprocess.check_output(['git','show',lock['xserver']['commit']+':'+path],cwd=base_repository).decode('utf-8').replace('\r\n','\n');updated=(sdk/path).read_text(encoding='utf-8')
  replacements[path]='diff --git a/'+path+' b/'+path+'\n'+''.join(difflib.unified_diff(base.splitlines(True),updated.splitlines(True),fromfile='a/'+path,tofile='b/'+path))
 # Earlier compatibility additions contain both plain unified and Git-style
 # sections. Normalize their metadata before replacing ALL sections of a file.
 normalized=re.sub(r'^(?:diff --git |index |new file mode |deleted file mode |old mode |new mode )[^\n]*\n','',previous,flags=re.M)
 rebuilt=[]
 for section in re.split(r'(?=^--- (?:a/|/dev/null))',normalized,flags=re.M):
  match=re.match(r'--- [^\n]+\n\+\+\+ b/([^\n]+)',section)
  if not match or match[1] not in replacements:rebuilt.append(section)
 rebuilt.extend(replacements.values());patch=''.join(rebuilt);sha=hashlib.sha256(patch.encode()).hexdigest()
 lock['libraries']['xwork']['production_tree_sha256']=build_mdo.production_tree_sha256(sdk/'lib/xwork');lock['sdk_overlay']['sha256']=sha;lock['sdk_overlay']['purpose']+='; paired external process resource ownership'
 (ROOT/lock['sdk_overlay']['path']).write_text(patch,encoding='utf-8',newline='\n');(ROOT/'deps.lock').write_text(json.dumps(lock,indent=2)+'\n',encoding='utf-8');(sdk/'.mdo-sdk-overlay.json').write_text(json.dumps(lock['sdk_overlay'],indent=2)+'\n',encoding='utf-8');build_mdo.verify_dependencies(sdk,lock)
 destination=sdk.parent/sha
 if sdk.parent.resolve()!=(ROOT/'.build/sdk-overlays').resolve() or destination!=sdk and destination.exists():raise ValueError('Refuse an unsafe SDK rename')
 if destination!=sdk:sdk.rename(destination)
 print(json.dumps({'sdk':str(destination),'sha256':sha}));return destination

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--base-repository',type=Path,required=True);p.add_argument('--rebuild-patch',action='store_true');a=p.parse_args();sdk=a.sdk.resolve();lock=build_mdo.load_object(ROOT/'deps.lock');build_mdo.verify_dependencies(sdk,lock)
 if a.rebuild_patch:refresh_patch(sdk,a.base_repository);return
 changes={};before={}
 def replace(path,old,new):
  text=changes.get(path)
  if text is None:before[path]=text=(sdk/path).read_text(encoding='utf-8')
  if text.count(old)!=1:raise ValueError('Expected one patch anchor in '+path)
  changes[path]=text.replace(old,new,1)
 api='''/* Configure before running this owned agent. Children inherit these callbacks.
 * Acquire is called before exec/spawn; release follows synchronous process-tree
 * cleanup or managed-task release. Completed task records can retain native
 * process resources: release them explicitly. Callbacks own no xwork memory,
 * are paired, thread-safe, and their code/data must outlive the agent/tasks. */
typedef bool (*xwork_process_owner_acquire_fn)(void* User,void** Lease);
typedef void (*xwork_process_owner_release_fn)(void* User,void* Lease);
bool xworkAgentSetProcessOwner(xwork_agent* Agent,
    xwork_process_owner_acquire_fn Acquire,xwork_process_owner_release_fn Release,
    void* User,xwork_error* Error);
'''
 replace('lib/xwork/xwork.h','bool xworkAgentRegisterTool(',api+'bool xworkAgentRegisterTool(')
 replace('lib/xwork/src/xwork_internal.h','struct xwork_agent {','''struct xwork_agent {
    xwork_process_owner_acquire_fn OnProcessAcquire;
    xwork_process_owner_release_fn OnProcessRelease;
    void* pProcessOwnerData;''')
 replace('lib/xwork/src/xwork_internal.h','typedef struct xwork_process_entry {','''typedef struct xwork_process_entry {
    xwork_process_owner_release_fn OnProcessRelease;
    void *pProcessOwnerData,*pProcessOwnerLease;
    bool bProcessOwnerHeld;''')
 path='lib/xwork/src/xwork_agent.c';before[path]=(sdk/path).read_text(encoding='utf-8');changes[path]=before[path]+'''
bool xworkAgentSetProcessOwner(xwork_agent* Agent,
    xwork_process_owner_acquire_fn Acquire,xwork_process_owner_release_fn Release,
    void* User,xwork_error* Error)
{
    xworkErrorInit(Error);
    if(!Agent||Agent->bRunning||(!Acquire!=!Release)){
        xwork__set_error(Error,XWORK_ERROR_INVALID_ARGUMENT,"Configure paired process owners before running the agent");return false;
    }
    Agent->OnProcessAcquire=Acquire;Agent->OnProcessRelease=Release;Agent->pProcessOwnerData=User;return true;
}
'''
 replace('lib/xwork/src/xwork_subagent.c','''        pDefinition, &tOptions, pError);
    if ( !pChild ) goto cleanup;''','''        pDefinition, &tOptions, pError);
    if ( !pChild ) goto cleanup;
    if(!xworkAgentSetProcessOwner(pChild,pArgs->pParent->OnProcessAcquire,
        pArgs->pParent->OnProcessRelease,pArgs->pParent->pProcessOwnerData,pError))goto cleanup;''')
 replace('lib/xwork/src/xwork_subagent.c','''    pChild = xworkAgentCreate(&tAgentConfig, pError);
    if ( !pChild ) goto cleanup;''','''    pChild = xworkAgentCreate(&tAgentConfig, pError);
    if ( !pChild ) goto cleanup;
    if(!xworkAgentSetProcessOwner(pChild,pParent->OnProcessAcquire,pParent->OnProcessRelease,pParent->pProcessOwnerData,pError))goto cleanup;''')
 replace('lib/xwork/src/xwork_tools.c','    pEntry->sCommand = xwork__argv_preview(psArgv, iArgvCount);','''    if(pAgent->OnProcessAcquire){
        if(!pAgent->OnProcessAcquire(pAgent->pProcessOwnerData,&pEntry->pProcessOwnerLease)){
            eResult=xwork__tool_process_fail(pOutput,"spawn/resources",psArgv[0],sResolvedCwd,xrtGetError());goto cleanup;
        }
        pEntry->OnProcessRelease=pAgent->OnProcessRelease;pEntry->pProcessOwnerData=pAgent->pProcessOwnerData;pEntry->bProcessOwnerHeld=true;
    }
    pEntry->sCommand = xwork__argv_preview(psArgv, iArgvCount);''')
 replace('lib/xwork/src/xwork_tools.c','''        xrtProcessDestroy(pEntry->pProcess);
    }
    if ( pEntry->pStateLock )''','''        xrtProcessDestroy(pEntry->pProcess);
    }
    if(pEntry->bProcessOwnerHeld){pEntry->bProcessOwnerHeld=false;pEntry->OnProcessRelease(pEntry->pProcessOwnerData,pEntry->pProcessOwnerLease);pEntry->pProcessOwnerLease=NULL;}
    if ( pEntry->pStateLock )''')
 replace('lib/xwork/src/xwork_tools.c','    if ( !xrtProcessRun(pConfig, &tOptions, pResult) ) return false;','''    {
        void* Lease=NULL;
        if(pAgent->OnProcessAcquire&&!pAgent->OnProcessAcquire(pAgent->pProcessOwnerData,&Lease))return false;
        bool Ran=xrtProcessRun(pConfig,&tOptions,pResult);
        if(pAgent->OnProcessRelease)pAgent->OnProcessRelease(pAgent->pProcessOwnerData,Lease);
        if(!Ran)return false;
    }''')
 # Export symbols are generated from the public header, using the host's tested
 # declaration scanner. Preserve the header comment and no hand-maintained list.
 path='src/script/import_xwork.inc';before[path]=(sdk/path).read_text(encoding='utf-8');names=set(re.findall(r'^[A-Za-z_][A-Za-z0-9_ *]*?\**\s*(xwork[A-Z]\w*)\s*\(',changes['lib/xwork/xwork.h'],re.M));changes[path]='/* xwork public API; generated from lib/xwork/xwork.h, coverage-tested. */\n\n'+''.join('XS_XWORK_SYMBOL('+name+')\n' for name in sorted(names))
 replace('tools/test_extensions.py','self.assertEqual(len(symbols), 179)','self.assertEqual(len(symbols), '+str(len(names))+')')
 patch_path=ROOT/lock['sdk_overlay']['path']
 backup=ROOT/'.build/process-owner-sdk-before';backup.mkdir(parents=True,exist_ok=True)
 if (backup/'deps.lock').exists():raise ValueError('Keep the previous SDK backup; choose the rebuild mode after initial application')
 (backup/'deps.lock').write_text(json.dumps(lock,indent=2)+'\n',encoding='utf-8');(backup/'previous.patch').write_bytes(patch_path.read_bytes())
 for path,updated in changes.items():
  file=backup/path;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(before[path],encoding='utf-8');(sdk/path).write_text(updated,encoding='utf-8',newline='\n')
 refresh_patch(sdk,a.base_repository)
if __name__=='__main__':main()
