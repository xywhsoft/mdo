#ifndef MDO_AGENT_CONFIG_H
#define MDO_AGENT_CONFIG_H
#include "extension_files.h"
#include "agent_config_format.h"
#include "extensions.h"

static inline xvalue* MdoAgentConfigResolveDepth(cstr Id, cstr Text, bool Main,
    cstr Root,const char** Stack, unsigned Depth, char* Error, size_t Capacity)
{
    xvalue* Own = NULL; xvalue* Base = NULL; char* ParentText = NULL;
    cstr Failure = "invalid, missing or cyclic Agent inheritance";
    if (Depth == 16u) goto failed;
    char Identity[96]; snprintf(Identity,sizeof(Identity),"%s:%s",Main?"agents":"subagents",Id);
    for (unsigned i=0;i<Depth;++i) if (!strcmp(Identity,Stack[i])) goto failed;
    Stack[Depth] = Identity;
    xstrview Trimmed = xrtStrTrim(xrtStrView(Text));
    Own = Trimmed.Size && Trimmed.Data[0]=='{' ? xrtJsonParse(Trimmed) : MdoPromptParse(Text,true,Error,Capacity);
    if (!Own || xrtValueType(Own)!=XVALUE_OBJECT) goto failed;
    bool ReadPromptFile=MdoAgentConfigGet(Own,"prompt_file")!=NULL;
    const xvalue* OwnContext=MdoAgentConfigGet(Own,"context");
    for(size_t I=0;I<xrtValueCount(OwnContext);I++){
        xvalue* Spec=(xvalue*)xrtValueObjectAt(OwnContext,I,NULL);
        if(MdoAgentConfigText(Spec,"file") && (!Root||!MdoAgentConfigSetText(Spec,"origin",Root)))goto failed;
    }
    cstr Parent = MdoAgentConfigText(Own,"extends");
    if (Parent && Parent[0]) {
        if (!strcmp(Parent,"mdo.general")) {
            Base=xrtValueObject();
            if(!Base || !MdoAgentConfigSetText(Base,"prompt",MDO_AGENT_SYSTEM_PROMPT))goto failed;
        } else {
            bool ParentMain=Main; cstr ParentId=Parent;
            if (!strncmp(Parent,"agent.",6)) { ParentMain=true; ParentId=Parent+6; }
            else if (!strncmp(Parent,"subagent.",9)) { ParentMain=false; ParentId=Parent+9; }
            else if (!strcmp(Parent,"mdo.default")) { ParentMain=true; ParentId="default"; }
            if (!MdoExtensionAlias(ParentId)) goto failed;
            char Why[512]={0};xvalue* ParentDirectory=MdoExtensionsFindAlias(ParentMain?"agents":"subagents",ParentId,Why);
            cstr ParentRoot=MdoAgentConfigText(ParentDirectory,"root"),ParentEntry=MdoAgentConfigText(ParentDirectory,"entry");
            char* Path=ParentRoot&&ParentEntry?xrtFormat("%s/%s",ParentRoot,ParentEntry):NULL;
            ParentText=Path?MdoExtensionRead(Path,true,MDO_EXTENSION_TEXT_LIMIT,NULL):NULL;xrtFree(Path);
            if(ParentText)Base=MdoAgentConfigResolveDepth(ParentId,ParentText,ParentMain,ParentRoot,Stack,Depth+1u,Error,Capacity);
            xrtValueRelease(ParentDirectory);
            xrtFree(ParentText); ParentText=NULL;
        }
        if (!Base) goto failed;
        cstr Before=MdoAgentConfigText(Base,"prompt"), After=MdoAgentConfigText(Own,"prompt");
        cstr Mode=MdoAgentConfigText(Own,"prompt_mode");
        char* Joined=NULL;
        if (Before && After && After[0] && (!Mode || !strcmp(Mode,"append"))) {
            Joined=xrtFormat("%s\n\n%s",Before,After);
            if (!Joined || strlen(Joined)>MDO_EXTENSION_TEXT_LIMIT || !MdoAgentConfigSetText(Own,"prompt",Joined)) {xrtFree(Joined);goto failed;}
            xrtFree(Joined);
        } else if (Before && (!After || !After[0]) && !MdoAgentConfigSetText(Own,"prompt",Before)) goto failed;
        if (!MdoAgentConfigMerge(Base,Own,0)) goto failed;
        xrtValueRelease(Own);Own=Base;Base=NULL;
    }
    cstr PromptFile=ReadPromptFile?MdoAgentConfigText(Own,"prompt_file"):NULL;
    if (PromptFile) {
        if (!MdoAgentConfigPathValid(PromptFile)) {Failure="prompt_file requires a portable relative path";goto failed;}
        char* Path=Root?xrtFormat("%s/%s",Root,PromptFile):NULL;
        char* Body=Path?MdoExtensionRead(Path,true,MDO_EXTENSION_TEXT_LIMIT,NULL):NULL;xrtFree(Path);
        if (!Body || !MdoAgentConfigSetText(Own,"prompt",Body)) {xrtFree(Body);Failure="cannot read Agent prompt_file";goto failed;}
        xrtFree(Body);
    }
    if (!MdoAgentConfigValidate(Own,Error,Capacity)) goto failed;
    return Own;
failed:
    xrtFree(ParentText);xrtValueRelease(Own);xrtValueRelease(Base);
    if (Error && Capacity && !Error[0]) snprintf(Error,Capacity,"%s",Failure);
    return NULL;
}

static inline xvalue* MdoAgentConfigResolveAt(cstr Id,cstr Text,bool Main,cstr Root,char* Error,size_t Capacity)
{
    const char* Stack[16]={0};
    if(Error&&Capacity)Error[0]=0;
    return MdoAgentConfigResolveDepth(Id,Text,Main,Root,Stack,0,Error,Capacity);
}
static inline xvalue* MdoAgentConfigResolve(cstr Id,cstr Text,bool Main,char* Error,size_t Capacity)
{
    char Why[512]={0};xvalue* Directory=MdoExtensionsFindAlias(Main?"agents":"subagents",Id,Why);
    xvalue* Result=MdoAgentConfigResolveAt(Id,Text,Main,MdoAgentConfigText(Directory,"root"),Error,Capacity);
    xrtValueRelease(Directory);return Result;
}

#endif
