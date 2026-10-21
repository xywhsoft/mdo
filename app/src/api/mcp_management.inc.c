/* Uses the same anchored, atomic editor primitives as project Skills. */
static xfile McpProjectWriterLock(xroot Root)
{
    if(!SkillParents(Root,".mdo/.mcp.lock"))return NULL;
    xfileoptions Options;xrtFileOptionsInit(&Options);
    Options.Flags=XFILE_READ|XFILE_WRITE|XFILE_CREATE|XFILE_NOFOLLOW;
    xfile Lock=xrtRootFileOpen(Root,".mdo/.mcp.lock",&Options);
    if(Lock&&!xrtFileLock(Lock,XFILE_LOCK_EXCLUSIVE,false)){xrtClose(Lock);return NULL;}
    return Lock;
}
static xvalue* McpProjectItem(xroot Root,cstr Workspace,cstr Id)
{
    char Path[96],Hash[65],Error[1024]={0};bool Missing=false;
    snprintf(Path,sizeof(Path),".mdo/mcp/%s.json",Id);
    char* Text=SkillRead(Root,Path,&Missing);
    if(!Text)return NULL;
    xvalue* Doc=xrtJsonParse(xrtStrView(Text));xvalue* Item=xrtValueObject();
    xstrview Name={0},Description={0};bool Enabled=true;
    if(Doc){(void)ExtensionString(Doc,"name",&Name);(void)ExtensionString(Doc,"description",&Description);
        (void)xrtValueGetBool(xrtValueObjectGet(Doc,XRT_STR_LITERAL("enabled")),&Enabled);}
    bool Valid=MdoMcpValidateText(Id,Text,Error,sizeof(Error));
    bool Ok=Item&&MdoExtensionHash(Text,Hash)&&MdoApiValueSetString(Item,"id",Id)&&
        MdoApiValueSetString(Item,"scope","project")&&MdoApiValueSetString(Item,"path",Path)&&
        MdoApiValueSetString(Item,"content",Text)&&MdoApiValueSetString(Item,"revision",Hash)&&
        MdoApiValueSetStringView(Item,"name",Name.Size?Name:xrtStrView(Id))&&
        MdoApiValueSetStringView(Item,"description",Description)&&MdoApiValueSetBool(Item,"enabled",Enabled)&&
        MdoApiValueSetBool(Item,"valid",Valid)&&MdoApiValueSetBool(Item,"external",true)&&
        MdoApiValueSetString(Item,"error",Error);
    Ok=Ok&&MdoApiValueSetBool(Item,"has_oauth",Doc&&xrtValueObjectHas(Doc,XRT_STR_LITERAL("oauth")));
    if(Ok&&MdoExtensionHash(Workspace,Hash)) {
        char* RuntimeId=xrtFormat("mdo-project-%.16s.%s",Hash,Id);
        Ok=RuntimeId&&MdoApiValueSetString(Item,"runtime_id",RuntimeId);xrtFree(RuntimeId);
    }
    xrtValueRelease(Doc);xrtFree(Text);
    if(!Ok){xrtValueRelease(Item);return NULL;}return Item;
}

static bool McpProjectList(MdoApiContext* C,xroot Root,cstr Workspace)
{
    xvalue* Data=xrtValueObject(),*Items=xrtValueArray();bool Ok=Data&&Items;
    xdir Dir=Root?xrtRootDirOpen(Root,".mdo/mcp",XDIR_STAT):NULL;
    if(!Dir&&Root&&xrtGetError()&&xrtErrorKind(xrtGetError())!=XERR_NOT_FOUND)Ok=false;
    xrtClearError();xdirentry Entry={0};xdirnext Next=XDIR_NEXT_END;size_t Count=0;
    while(Ok&&Dir&&(Next=xrtDirNext(Dir,&Entry))==XDIR_NEXT_ITEM) {
        size_t N=Entry.Name.Size;
        if(N<=5||N>69||memcmp(Entry.Name.Data+N-5,".json",5))continue;
        char Id[65];memcpy(Id,Entry.Name.Data,N-5);Id[N-5]=0;
        if(!MdoExtensionIdValid(Id))continue;
        if(++Count>128){Ok=false;break;}
        xvalue* Item=McpProjectItem(Root,Workspace,Id);
        if(Item){Ok=xrtValueArrayAppend(Items,Item);xrtValueRelease(Item);}
    }
    if(Next==XDIR_NEXT_ERROR)Ok=false;
    if(Dir&&!xrtDirClose(Dir))Ok=false;
    Ok=Ok&&MdoApiValueSetTake(Data,"items",&Items);xrtValueRelease(Items);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,503,"mcp_project_unreadable","Project MCP definitions are unavailable",NULL);}
    return MdoApiReplySuccessTake(C,200,Data,NULL);
}

bool MdoApiMcpProjectRoute(MdoApiContext* C)
{
    unsigned Method=C->Request->head->MethodCode;
    bool Write=Method!=XHTTP_METHOD_GET;
    char* Workspace=NULL;if(!MdoApiSkillScopeMode(C,&Workspace,Write))return true;
    if(!Workspace)return MdoApiReplyError(C,400,"mcp_project_required","A project ID is required",NULL);
    char Id[65]={0};bool Result=false;
    if(C->ParamCount) {
        if(C->Params[0].Size>64){xrtFree(Workspace);return MdoApiReplyError(C,400,"mcp_id_invalid","Invalid server ID",NULL);}
        memcpy(Id,C->Params[0].Data,C->Params[0].Size);Id[C->Params[0].Size]=0;
        if(!MdoExtensionIdValid(Id)){xrtFree(Workspace);return MdoApiReplyError(C,400,"mcp_id_invalid","Invalid server ID",NULL);}
    }
    if(!Id[0]&&Method==XHTTP_METHOD_POST) {
        xwork_error Error;xworkErrorInit(&Error);
        bool Ok=MdoMcpConfigureAgent(NULL,Workspace,&Error);xrtFree(Workspace);
        return Ok?MdoApiReplySuccessTake(C,200,xrtValueObject(),NULL):MdoApiReplyError(C,409,"mcp_project_unavailable","Cannot prepare project MCP; check server configuration and diagnostics",NULL);
    }
    char Project[65];size_t ProjectSize=0;
    (void)xrtPercentDecode(xrtStrViewN(C->Target.Query.Data+8,C->Target.Query.Size-8),Project,64,&ProjectSize);Project[ProjectSize]=0;
    xwork_error LeaseError;MdoProjectLease* Lease=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,&LeaseError);
    if(!Lease){xrtFree(Workspace);return MdoApiReplyError(C,409,"mcp_project_busy","Project is busy",NULL);}
    xrtMutexLock(g_MdoExtensionLock);xroot Root=xrtRootOpen(Workspace);
    xfile Lock=Write&&Root?McpProjectWriterLock(Root):NULL;
    if(Write&&Root&&!Lock)Result=MdoApiReplyError(C,409,"mcp_project_busy","Project MCP files are being edited",NULL);
    else if(!Id[0])Result=McpProjectList(C,Root,Workspace);
    else if(Method==XHTTP_METHOD_GET) {
        xvalue* Item=Root?McpProjectItem(Root,Workspace,Id):NULL;
        Result=Item?MdoApiReplySuccessTake(C,200,Item,NULL):MdoApiReplyError(C,404,"mcp_project_missing","Project MCP server is unavailable",NULL);
    } else if(!Root)Result=MdoApiReplyError(C,503,"mcp_project_unavailable","Project directory is unavailable",NULL);
    else {
        char Path[96];snprintf(Path,sizeof(Path),".mdo/mcp/%s.json",Id);
        bool Missing=false;char* Old=SkillRead(Root,Path,&Missing);
        if(!Old&&!Missing)Result=MdoApiReplyError(C,503,"mcp_project_unreadable","Existing server cannot be read safely",NULL);
        else if(!ExtensionMatch(C,Old))Result=true;
        else if(Method==XHTTP_METHOD_DELETE) {
            Result=xrtRootRemove(Root,Path)?MdoApiReplySuccessTake(C,200,xrtValueObject(),NULL):MdoApiReplyError(C,409,"mcp_project_delete_failed","Cannot delete project MCP",NULL);
        } else {
            MdoApiJsonBody Body={0};MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
            if(Status!=MDO_API_BODY_OK)Result=MdoApiReplyBodyError(C,Status);
            else {
                char* Text=NULL;char Error[1024]={0};char References[32][MDO_SECRET_VAULT_REFERENCE_CAPACITY];size_t RefCount=0;
                xstrview Content={0};bool Ok=false;
                if(Method==XHTTP_METHOD_PUT) {
                    if(ExtensionString(Body.Value,"content",&Content)&&Content.Size<=MDO_SKILL_TEXT_LIMIT)
                        Text=xrtStrDupN(Content.Data,Content.Size);
                    Ok=Text&&ExtensionMcpCredentials(&Text,xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("secrets")),References,&RefCount);
                } else if(Old) {
                    bool Enabled=false;xvalue* Doc=xrtJsonParse(xrtStrView(Old));
                    Ok=Doc&&xrtValueGetBool(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("enabled")),&Enabled)&&
                        xrtValueObjectSetNew(Doc,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled));
                    Text=Ok?xrtJsonStringify(Doc,true,NULL):NULL;xrtValueRelease(Doc);Ok=Text!=NULL;
                }
                Ok=Ok&&MdoMcpValidateText(Id,Text,Error,sizeof(Error));
                bool Saved=Ok&&SkillWrite(Root,Path,Text,strlen(Text));
                if(Saved){xvalue* Item=McpProjectItem(Root,Workspace,Id);Result=Item?MdoApiReplySuccessTake(C,200,Item,NULL):MdoApiReplyError(C,503,"mcp_project_result_unavailable","Saved MCP cannot be read",NULL);}
                else {for(size_t i=0;i<RefCount;i++)MdoSecretDiscard(References[i]);Result=MdoApiReplyError(C,400,"mcp_project_invalid",Error[0]?Error:"MCP configuration or credentials are invalid",NULL);}
                xrtFree(Text);ExtensionBodyUnit(&Body);
            }
        }
        xrtFree(Old);
    }
    if(Lock)xrtClose(Lock);
    if(Root)xrtRootClose(Root);
    xrtMutexUnlock(g_MdoExtensionLock);MdoProjectLeaseRelease(Lease);xrtFree(Workspace);return Result;
}
