/* Explicit user selection is rendered as an ordinary reviewable draft. No
 * script runs and no permission is granted by reading a Skill. */
bool MdoApiSkillInvokeRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Input = {0};
    MdoApiBodyStatus Status = MdoApiJsonBodyRead(Context, &Input);
    if (Status != MDO_API_BODY_OK) return MdoApiReplyBodyError(Context,Status);
    xstrview Id={0}, Args={0};
    const xvalue* A=xrtValueObjectGet(Input.Value,XRT_STR_LITERAL("arguments"));
    bool Ok=xrtValueGetString(xrtValueObjectGet(Input.Value,XRT_STR_LITERAL("id")),&Id) &&
        Id.Size && Id.Size<=96 && !memchr(Id.Data,0,Id.Size) &&
        (!A || (xrtValueGetString(A,&Args) && Args.Size<=16384 && !memchr(Args.Data,0,Args.Size)));
    MdoSkillCatalog* Catalog=MdoSkillCatalogSnapshot();
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
