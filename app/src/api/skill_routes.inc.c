#include "skill_scope.inc.h"

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
    if(!Ok){xrtFree(Workspace);MdoApiJsonBodyUnit(&Input);return MdoApiReplyError(Context,400,"skill_arguments_invalid","Skill id must be text up to 192 bytes and arguments must be text up to 16 KiB",NULL);}
    MdoSkillCatalog* Catalog=MdoSkillCatalogForWorkspace(Workspace);xrtFree(Workspace);
    if(!Catalog){MdoApiJsonBodyUnit(&Input);return MdoApiReplyError(Context,503,"skill_scope_unavailable","Cannot load scoped Skills; check project configuration and package receipts",NULL);}
    MdoSkillInfo Info={0}; MdoSkillContent Body={0}; Info.Size=sizeof(Info);Body.Size=sizeof(Body);
    char* Name=Ok?xrtStrDupN(Id.Data,Id.Size):NULL;
    bool Found=Name && Catalog && MdoSkillCatalogFind(Catalog,Name,&Info);
    if(Found&&!Info.UserInvocable){xrtFree(Name);MdoSkillCatalogRelease(Catalog);MdoApiJsonBodyUnit(&Input);
        return MdoApiReplyError(Context,403,"skill_user_invocation_disabled","This Skill is available to the model only",NULL);}
    Ok=Ok && Found && MdoSkillCatalogLoadBody(Catalog,Name,&Body);
    char* Base=Ok?MdoSkillCatalogBaseDirectory(Catalog,Name):NULL;
    char* Rendered=Base?MdoSkillRenderArguments(Body.Text,Base,Args.Data,Info.ArgumentNames,Info.ArgumentCount):NULL;
    char* Text=Rendered?xrtFormat("Use the explicitly selected Skill %s. Its instructions do not grant tool permissions.\nBase directory: %s\nResolve Skill-relative file paths against this directory; keep the task workspace as the working directory.\n\n%s\n\nUser task:\n%.*s",
        Name,Base,Rendered,(int)Args.Size,Args.Data?Args.Data:""):NULL;
    size_t Escaped=0;
    for(const unsigned char* P=(const unsigned char*)Text;P&&*P;P++) {
        Escaped+=*P<0x20?6:(*P=='"'||*P=='\\'?2:1);
        if(Escaped>1024u*1024u-8192u){xrtFree(Text);Text=NULL;break;}
    }
    xvalue* Data=Text?xrtValueObject():NULL;
    Ok=Text && Data && MdoApiValueSetString(Data,"id",Name) && MdoApiValueSetString(Data,"text",Text);
    xrtFree(Rendered);xrtFree(Base);xrtFree(Text);xrtFree(Name);MdoSkillContentUnit(&Body);MdoSkillCatalogRelease(Catalog);MdoApiJsonBodyUnit(&Input);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(Context,Found?422:404,
        "skill_invocation_unavailable","Skill is unavailable or its expanded text is too large; refresh the list, check files, or reduce repeated arguments",NULL);}
    return MdoApiReplySuccessTake(Context,200,Data,NULL);
}
