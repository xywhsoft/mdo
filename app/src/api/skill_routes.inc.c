#include "../../include/mdo/projects.h"

/* Project IDs, never caller-supplied filesystem paths, select the catalog. */
static bool MdoApiSkillScope(MdoApiContext* Context,char** Workspace)
{
    *Workspace=NULL;xstrview Query=Context->Target.Query;
    if(!Query.Size)return true;
    char Id[65];size_t Size=0;
    if(Query.Size<9 || memcmp(Query.Data,"project=",8) ||
        memchr(Query.Data+8,'&',Query.Size-8) ||
        !xrtPercentDecode(xrtStrViewN(Query.Data+8,Query.Size-8),Id,64,&Size) ||
        !Size || memchr(Id,0,Size)) {
        MdoApiReplyError(Context,400,"skill_scope_invalid","Expected a single project ID",NULL);return false;
    }
    Id[Size]=0;
    if(!strcmp(Id,"default"))*Workspace=MdoHomeDefaultWorkspacePath(true);
    else {
        MdoProjectInfo Info={0};Info.Size=sizeof(Info);bool Found=false;xwork_error Error;
        if(!MdoProjectGet(Id,&Info,&Found,&Error)||!Found) {
            MdoApiReplyError(Context,404,"skill_project_missing","Project is unavailable",NULL);return false;
        }
        *Workspace=xrtPathAbs(Info.WorkspaceRoot);
    }
    if(!*Workspace){MdoApiReplyError(Context,503,"skill_scope_unavailable","Project directory is unavailable",NULL);return false;}
    return true;
}

/* Explicit user selection is rendered as an ordinary reviewable draft. No
 * script runs and no permission is granted by reading a Skill. */
bool MdoApiSkillInvokeRoute(MdoApiContext* Context)
{
    char* Workspace=NULL;
    if(!MdoApiSkillScope(Context,&Workspace))return true;
    MdoApiJsonBody Input = {0};
    MdoApiBodyStatus Status = MdoApiJsonBodyRead(Context, &Input);
    if (Status != MDO_API_BODY_OK) {xrtFree(Workspace);return MdoApiReplyBodyError(Context,Status);}
    xstrview Id={0}, Args={0};
    const xvalue* A=xrtValueObjectGet(Input.Value,XRT_STR_LITERAL("arguments"));
    bool Ok=xrtValueGetString(xrtValueObjectGet(Input.Value,XRT_STR_LITERAL("id")),&Id) &&
        Id.Size && Id.Size<=192 && !memchr(Id.Data,0,Id.Size) &&
        (!A || (xrtValueGetString(A,&Args) && Args.Size<=16384 && !memchr(Args.Data,0,Args.Size)));
    MdoSkillCatalog* Catalog=MdoSkillCatalogForWorkspace(Workspace);xrtFree(Workspace);
    if(!Catalog){MdoApiJsonBodyUnit(&Input);return MdoApiReplyError(Context,503,"skill_scope_unavailable","Cannot load scoped Skills; check project configuration and package receipts",NULL);}
    MdoSkillInfo Info={0}; MdoSkillContent Body={0}; Info.Size=sizeof(Info);Body.Size=sizeof(Body);
    char* Name=Ok?xrtStrDupN(Id.Data,Id.Size):NULL;
    bool Found=Name && Catalog && MdoSkillCatalogFind(Catalog,Name,&Info);
    if(Found&&!Info.UserInvocable){xrtFree(Name);MdoSkillCatalogRelease(Catalog);MdoApiJsonBodyUnit(&Input);
        return MdoApiReplyError(Context,403,"skill_user_invocation_disabled","This Skill is available to the model only",NULL);}
    Ok=Ok && Found && MdoSkillCatalogLoadBody(Catalog,Name,&Body);
    char* Base=Ok?MdoSkillCatalogBaseDirectory(Catalog,Name):NULL;
    char* Rendered=Base?MdoSkillRender(Body.Text,Base,Args.Data):NULL;
    char* Text=Rendered?xrtFormat("Use the explicitly selected Skill %s. Its instructions do not grant tool permissions.\nBase directory: %s\nResolve Skill-relative file paths against this directory; keep the task workspace as the working directory.\n\n%s\n\nUser task:\n%.*s",
        Name,Base,Rendered,(int)Args.Size,Args.Data?Args.Data:""):NULL;
    xvalue* Data=Text?xrtValueObject():NULL;
    Ok=Text && Data && MdoApiValueSetString(Data,"id",Name) && MdoApiValueSetString(Data,"text",Text);
    xrtFree(Rendered);xrtFree(Base);xrtFree(Text);xrtFree(Name);MdoSkillContentUnit(&Body);MdoSkillCatalogRelease(Catalog);MdoApiJsonBodyUnit(&Input);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(Context,Found?422:404,
        "skill_invocation_unavailable","Skill is missing, disabled, invalid or unreadable; refresh the Skill list",NULL);}
    return MdoApiReplySuccessTake(Context,200,Data,NULL);
}
