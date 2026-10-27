static bool MdoSkillCheckString(const xvalue* Input,cstr Key,size_t Limit,xstrview* Text)
{
    const xvalue* Value=xrtValueObjectGet(Input,xrtStrView(Key));
    return !Value||(xrtValueGetString(Value,Text)&&Text->Size<=Limit&&
        !memchr(Text->Data,0,Text->Size));
}

bool MdoSkillCheckRequest(const xvalue* Input,cstr Workspace,xvalue* Report)
{
    xstrview Content={0},Name={0},Scope={0};
    const xvalue* Draft=xrtValueObjectGet(Input,XRT_STR_LITERAL("content"));
    bool Request=Input&&xrtValueType(Input)==XVALUE_OBJECT&&Report&&
        MdoSkillCheckString(Input,"content",MDO_SKILL_TEXT_LIMIT,&Content)&&
        MdoSkillCheckString(Input,"name",64,&Name)&&MdoSkillCheckString(Input,"scope",16,&Scope)&&
        (Draft?(!Name.Size&&!Scope.Size):Name.Size>0);
    static const char* const LoadOnly[]={"path","query","arguments","offset","limit","cursor"};
    for(size_t i=0;Request&&i<sizeof(LoadOnly)/sizeof(*LoadOnly);i++)
        if(xrtValueObjectGet(Input,xrtStrView(LoadOnly[i])))Request=false;
    char* Id=Request&&Name.Size?xrtStrDupN(Name.Data,Name.Size):NULL;
    cstr Selected=Scope.Size?Scope.Data:Workspace?"project":"user";
    if(!Draft)Request=Request&&Id&&MdoSkillsIdValid(Id)&&
        (!strcmp(Selected,"user")||(!strcmp(Selected,"project")&&Workspace));
    if(!Request){xrtFree(Id);MdoSkillsSetError("Validate a content string up to 128 KiB, or a portable name with scope user/project; never supply arbitrary paths");return false;}

    xvalue* Errors=xrtValueArray(),*Warnings=xrtValueArray();
    bool Ok=Errors&&Warnings,Valid=false,Resources=false;
    char Error[1024]={0};size_t ResourceCount=0;
    char* SourcePath=NULL;
    if(Ok&&Draft) {
        Valid=MdoSkillValidateText(Content.Data,Error,sizeof(Error));
        if(!Valid)Ok=xrtValueArrayAppendNew(Errors,xrtValueString(xrtStrView(Error[0]?Error:"Invalid Skill document")));
        if(Valid) {
            xvalue* Doc=MdoPromptParse(Content.Data,true,Error,sizeof(Error));
            char* Body=Doc?MdoPromptString(Doc,"prompt"):NULL;
            if(!Doc||!Body)Ok=false;
            else if(!xrtStrTrim(xrtStrView(Body)).Size)
                Ok=xrtValueArrayAppendNew(Warnings,xrtValueString(XRT_STR_LITERAL("The Skill body is empty; add its workflow and expected output.")));
            xrtFree(Body);xrtValueRelease(Doc);
        }
        if(Ok)Ok=xrtValueArrayAppendNew(Warnings,xrtValueString(XRT_STR_LITERAL("Document check only: save the files and validate the installed folder to check resource paths and limits.")));
    } else if(Ok) {
        MdoSkillCatalog* Catalog=!strcmp(Selected,"project")?MdoSkillCatalogForWorkspace(Workspace):MdoSkillCatalogSnapshot();
        const MdoSkillEntry* Entry=Catalog?MdoSkillsFind(Catalog,Id):NULL;
        MdoSkillContent Body={0};Body.Size=sizeof(Body);
        Valid=Entry&&MdoSkillsInstalledAvailable(Entry)&&MdoSkillCatalogLoadBody(Catalog,Id,&Body);Resources=Valid;
        if(Entry){ResourceCount=Entry->ResourceCount;SourcePath=xrtStrDup(Entry->SourcePath);Ok=SourcePath!=NULL;}
        if(Ok&&!Valid)Ok=xrtValueArrayAppendNew(Errors,xrtValueString(xrtStrView(xrtGetError()?xrtErrorMessage(xrtGetError()):"Select an enabled Skill installed in Home; project directories are import sources only.")));
        if(Ok&&Valid&&!xrtStrTrim(xrtStrView(Body.Text)).Size)Ok=xrtValueArrayAppendNew(Warnings,xrtValueString(XRT_STR_LITERAL("The Skill body is empty; add its workflow and expected output.")));
        MdoSkillContentUnit(&Body);MdoSkillCatalogRelease(Catalog);
    }
    if(Ok)Ok=xrtValueObjectSetNew(Report,XRT_STR_LITERAL("valid"),xrtValueBool(Valid))&&
        xrtValueObjectSetNew(Report,XRT_STR_LITERAL("resources_checked"),xrtValueBool(Resources))&&
        xrtValueObjectSetNew(Report,XRT_STR_LITERAL("behavior_tested"),xrtValueBool(false))&&
        xrtValueObjectSetNew(Report,XRT_STR_LITERAL("resource_count"),xrtValueUInt(ResourceCount))&&
        xrtValueObjectSetNew(Report,XRT_STR_LITERAL("source_path"),xrtValueString(xrtStrView(SourcePath?SourcePath:"")))&&
        xrtValueObjectSet(Report,XRT_STR_LITERAL("errors"),Errors)&&
        xrtValueObjectSet(Report,XRT_STR_LITERAL("warnings"),Warnings);
    xrtFree(SourcePath);xrtFree(Id);xrtValueRelease(Errors);xrtValueRelease(Warnings);
    if(Ok)xrtClearError();
    return Ok;
}
