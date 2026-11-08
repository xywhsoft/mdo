/* Legacy browser root recovery reads the original persisted task, never the
 * selected chat/project default. Lookup is read-only; adoption is explicit,
 * revision checked and fill-only. Keep a physical pin for subsequent saves. */
#include "../../include/mdo/sessions.h"
static bool BuWorkspaceBound(const mdo_sdk_context_v1* C,const xvalue* Row,bool Default,mdo_error_v1* E){
    const xvalue* Scope=SdkGet(Row,"context");
    if(!Row||!WbBound(C,Row)||!WbEqual(C->Namespace,SdkField(Scope,"namespace"))||!WbEqual(C->MemberId,SdkField(Scope,"member")))
        return WbFail(E,"context_mismatch","Browser recovery belongs to its original owner, device, task and private member",409);
    const cstr TextKeys[]={"workspace","namespace","member"};
    for(size_t I=0;I<3;I++)if(SdkGet(Scope,TextKeys[I])&&!SdkField(Scope,TextKeys[I]))
        return WbFail(E,"browser_context_invalid","Preserve this browser: its original context is not readable",409);
    cstr Workspace=SdkField(Scope,"workspace");
    if((Workspace&&Workspace[0]&&!Default)||SdkGet(Scope,"workspace_snapshot"))
        return WbFail(E,"browser_workspace_bound","This browser already has a directory binding; restore its original directory instead of replacing it",409);
    return true;
}
static bool BuWorkspaceNamed(cstr Path,const xfileinfo* Opened){
    xfileinfo Named={0};return xrtPathStat(Path,false,&Named)&&Named.Type==XFILE_TYPE_DIRECTORY&&
        (Named.Available&XFILE_INFO_IDENTITY)&&Named.Device==Opened->Device&&Named.Identity==Opened->Identity;
}
static bool BuWorkspaceTask(const mdo_sdk_context_v1* C,MdoSessionInfo* Session,mdo_error_v1* E){
    if(!C->SessionId||!SdkIdValid(C->SessionId))return WbFail(E,"browser_workspace_task_missing","This browser has no original task; no directory was inferred",409);
    xwork_error Error;MdoSessionCatalog* Catalog=MdoSessionCatalogSnapshot(&Error);
    if(!Catalog)return WbFail(E,"browser_workspace_task_unavailable","Cannot read the original task catalog; no model was opened",503);
    bool Found=false,Valid=true;
    for(size_t I=0;I<MdoSessionCatalogCount(Catalog);I++){
        Session->Size=sizeof(*Session);
        if(!MdoSessionCatalogAt(Catalog,I,Session)){Valid=false;break;}
        if(WbEqual(Session->ProjectId,C->ProjectId&&C->ProjectId[0]?C->ProjectId:"default")&&WbEqual(Session->Id,C->SessionId)){Found=true;break;}
    }
    MdoSessionCatalogRelease(Catalog);
    if(!Valid)return WbFail(E,"browser_workspace_task_unavailable","Cannot inspect the original task catalog",503);
    if(!Found||Session->Status==MDO_SESSION_TRASH||!Session->WorkspaceRoot[0]||!xrtPathIsAbs(Session->WorkspaceRoot))
        return WbFail(E,"browser_workspace_task_missing","The original task directory cannot be established; no current-chat fallback is allowed",409);
    return true;
}
static bool BuWorkspaceOpen(cstr Path,bool MissingAllowed,xroot* Root,xfileinfo* Directory,bool* Exists,mdo_error_v1* E){
    xfileinfo Named={0};*Exists=false;
    if(!xrtPathStat(Path,false,&Named)){
        const xerror* Error=xrtGetError();
        if(MissingAllowed&&Error&&xrtErrorKind(Error)==XERR_NOT_FOUND)return true;
        return WbFail(E,"browser_workspace_unavailable","Restore the original task directory before binding it",409);
    }
    *Exists=true;
    if(Named.Type!=XFILE_TYPE_DIRECTORY||!(*Root=xrtRootOpen(Path))||!xrtRootStat(*Root,".",false,Directory)||
       Directory->Type!=XFILE_TYPE_DIRECTORY||!(Directory->Available&XFILE_INFO_IDENTITY)||!BuWorkspaceNamed(Path,Directory))
        return WbFail(E,"browser_workspace_unavailable","The original task directory is not a stable physical directory",409);
    return true;
}
static bool BuBrowserWorkspace(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    bool Prepare=!strcmp(Method,"browser.workspace.prepare"),Adopt=!strcmp(Method,"browser.workspace.adopt"),Default=false;
    bool Confirmed=false,Ok=false,Locked=false,Changed=false,Exists=false,Effect=false;
    cstr Id=SdkField(P,"id");xvalue *Seed=NULL,*Candidate=NULL,*Metadata=NULL,*Next=NULL,*Reply=NULL;
    xroot Root=NULL;MdoProjectLease* Lease=NULL;xfileinfo Directory={0};MdoSessionInfo Session={0};
    if(!SdkIdValid(Id))return WbFail(E,"browser_arguments_invalid","Supply an owned browser identity",422);
    if(SdkGet(P,"prepare_default")&&(strcmp(Method,"browser.workspace.lookup")||!xrtValueGetBool(SdkGet(P,"prepare_default"),&Default)))
        return WbFail(E,"browser_arguments_invalid","Default directory inspection needs a boolean flag",422);
    Default|=Prepare;
    if((Adopt||Prepare)&&(!xrtValueGetBool(SdkGet(P,"confirmed"),&Confirmed)||!Confirmed))
        return WbFail(E,"browser_workspace_confirmation","Review and explicitly confirm the original task directory before binding it",422);
    xrtMutexLock(Workbench.Lock);Locked=true;
    const xvalue* Initial=WbFind(Workbench.State,"browsers",Id);
    if(!BuWorkspaceBound(C,Initial,Default,E))goto done;
    Seed=xrtValueClone(Initial);xrtMutexUnlock(Workbench.Lock);Locked=false;if(!Seed)goto done;
    if(!BuWorkspaceTask(C,&Session,E))goto done;
    if(Default){
        char* Expected=MdoHomeDefaultWorkspacePath(false);
        cstr Bound=SdkField(SdkGet(Seed,"context"),"workspace");
        bool Match=Expected&&WbEqual(Session.ProjectId,"default")&&WbEqual(Expected,Session.WorkspaceRoot)&&
            (!Bound||!Bound[0]||WbEqual(Bound,Expected));xrtFree(Expected);
        if(!Match){WbFail(E,"browser_workspace_not_default","Only the original task's canonical default directory may be prepared",409);goto done;}
        if(C->Workspace&&C->Workspace[0]&&!WbEqual(C->Workspace,Session.WorkspaceRoot)){
            WbFail(E,"context_mismatch","Directory preparation requires the frozen original task workspace",409);goto done;
        }
    }
    if(Prepare){
        xwork_error Error;Lease=MdoProjectLeaseAcquire("default",MDO_PROJECT_LEASE_SHARED,&Error);
        if(!Lease){WbFail(E,"browser_workspace_unavailable","The original default project is unavailable",409);goto done;}
    }
    if(!BuFileContinue(C,E))goto done;
    if(!BuWorkspaceOpen(Session.WorkspaceRoot,Default,&Root,&Directory,&Exists,E))goto done;
    if(Exists)Metadata=SdkFileInfo(&Directory);
    cstr Snapshot=SdkField(Metadata,"revision");uint64 Revision=0;
    if((Exists&&(!Snapshot||!BuPublishRootMatches(Snapshot,&Directory)))||!SdkUInt(SdkGet(Seed,"revision"),&Revision))goto done;
    Candidate=xrtValueObject();
    if(!Candidate||!SdkPut(Candidate,"id",Id)||!SdkPutUInt(Candidate,"revision",Revision)||!SdkPut(Candidate,"project",Session.ProjectId)||
       !SdkPut(Candidate,"session",Session.Id)||!SdkPutUInt(Candidate,"session_revision",Session.Revision)||
       !SdkPut(Candidate,"workspace",Session.WorkspaceRoot)||(Exists&&!SdkPut(Candidate,"root_snapshot",Snapshot)))goto done;
    if(Default&&(!xrtValueObjectSetNew(Candidate,XRT_STR_LITERAL("prepare_default"),xrtValueBool(true))||
       !xrtValueObjectSetNew(Candidate,XRT_STR_LITERAL("root_exists"),xrtValueBool(Exists))))goto done;
    if(!Adopt&&!Prepare){Reply=Candidate;Candidate=NULL;Ok=true;goto done;}
    uint64 ExpectedSession=0;bool ReviewedExists=true;
    if(!SdkUInt(SdkGet(P,"session_revision"),&ExpectedSession)||ExpectedSession!=Session.Revision||
       !WbEqual(SdkField(P,"workspace"),Session.WorkspaceRoot)||
       (Prepare&&(!xrtValueGetBool(SdkGet(P,"root_exists"),&ReviewedExists)||ReviewedExists!=Exists))||
       (Exists&&!BuPublishRootMatches(SdkField(P,"root_snapshot"),&Directory))||(!Exists&&SdkGet(P,"root_snapshot"))){
        WbFail(E,"browser_workspace_conflict","The reviewed task or physical directory changed; read the original task again",412);goto done;
    }
    xrtMutexLock(Workbench.Lock);Locked=true;
    const xvalue* Current=WbFind(Workbench.State,"browsers",Id);
    if(!BuWorkspaceBound(C,Current,Default,E)||!WbRevision(P,Current,E)||!BuFileContinue(C,E))goto done;
    if(Prepare){
        MdoSessionInfo Fresh={0};
        if(!BuWorkspaceTask(C,&Fresh,E))goto done;
        if(Fresh.Revision!=Session.Revision||!WbEqual(Fresh.WorkspaceRoot,Session.WorkspaceRoot)){
            WbFail(E,"browser_workspace_conflict","The original task changed; review its directory again",412);goto done;
        }
    }
    if(Exists&&!BuWorkspaceNamed(Session.WorkspaceRoot,&Directory)){WbFail(E,"browser_workspace_conflict","The reviewed physical directory changed; binding was not saved",412);goto done;}
    uint64 CurrentRevision=0,StateRevision=0;
    if(!SdkUInt(SdkGet(Current,"revision"),&CurrentRevision)||CurrentRevision==UINT64_MAX||
       !SdkUInt(SdkGet(Workbench.State,"revision"),&StateRevision)||StateRevision==UINT64_MAX)goto done;
    Next=xrtValueClone(Workbench.State);if(!Next)goto done;
    xvalue* Row=WbFind(Next,"browsers",Id),*Scope=(xvalue*)SdkGet(Row,"context");
    if(!Exists){
        // Create through the retained Home root, never through a caller path.
        Effect=true;Root=MdoHomeOpenStorageDirectory(MDO_DEFAULT_WORKSPACE_PATH);
        if(!Root||!xrtRootStat(Root,".",false,&Directory)||Directory.Type!=XFILE_TYPE_DIRECTORY||
           !(Directory.Available&XFILE_INFO_IDENTITY)||!BuWorkspaceNamed(Session.WorkspaceRoot,&Directory)){
            WbFail(E,"browser_workspace_unavailable","The original default directory could not be prepared and confirmed",409);goto done;
        }
        Exists=true;
        Metadata=SdkFileInfo(&Directory);Snapshot=SdkField(Metadata,"revision");
        if(!Snapshot||!BuPublishRootMatches(Snapshot,&Directory)||!BuFileContinue(C,E))goto done;
    }
    if(!Scope||!SdkPut(Scope,"workspace",Session.WorkspaceRoot)||!SdkPut(Scope,"workspace_snapshot",Snapshot)||
       !SdkPutUInt(Row,"revision",CurrentRevision+1)||!SdkPutUInt(Next,"revision",StateRevision+1))goto done;
    Reply=xrtValueClone(Row);if(!Reply)goto done;
    if(!BuFileContinue(C,E))goto done;
    if(!BuWorkspaceNamed(Session.WorkspaceRoot,&Directory)){WbFail(E,"browser_workspace_conflict","The original directory changed before binding was saved",412);goto done;}
    if(Prepare)Effect=true;
    if(!WbSave(Next)){WbFail(E,"workbench_save_failed","Browser workspace binding could not be confirmed; reread the original binding before retrying",503);goto done;}
    xrtValueRelease(Workbench.State);Workbench.State=Next;Next=NULL;Ok=true;Changed=true;
done:
    if(Locked)xrtMutexUnlock(Workbench.Lock);
    if(Root&&!xrtRootClose(Root)&&!Ok&&!E->Code[0])WbFail(E,"browser_workspace_unavailable","Cannot close the inspected task root",503);
    MdoProjectLeaseRelease(Lease);
    xrtValueRelease(Seed);xrtValueRelease(Candidate);xrtValueRelease(Metadata);xrtValueRelease(Next);
    if(Ok){*R=Reply;if(Changed){xvalue* Facts=xrtValueObject();if(Facts&&SdkPut(Facts,"method",Method)&&SdkPut(Facts,"id",Id))MdoApplicationEvent("workbench.changed",C,Facts);xrtValueRelease(Facts);}}
    else{xrtValueRelease(Reply);if(!E->Code[0])WbFail(E,"browser_workspace_failed","Cannot confirm the workspace recovery result; inspect the original binding",503);if(Effect)E->MayHaveCompleted=true;}
    return Ok;
}
