#ifndef MDO_AGENT_CONFIG_H
#define MDO_AGENT_CONFIG_H
#include "extension_files.h"
#include "agent_config_format.h"

static inline xvalue* MdoAgentConfigResolveDepth(cstr Id, cstr Text, bool Main,
    const char** Stack, unsigned Depth, char* Error, size_t Capacity)
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
        if(MdoAgentConfigText(Spec,"file") && !MdoAgentConfigSetText(Spec,"origin",Main?"agents":"subagents"))goto failed;
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
            if (!MdoExtensionIdValid(ParentId)) goto failed;
            char Path[160]; snprintf(Path,sizeof(Path),"%s/%s.md",ParentMain?"agents":"subagents",ParentId);
            ParentText=MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,NULL);
            if (!ParentText) goto failed;
            Base=MdoAgentConfigResolveDepth(ParentId,ParentText,ParentMain,Stack,Depth+1u,Error,Capacity);
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
        char* Path=xrtFormat("%s/%s",Main?"agents":"subagents",PromptFile);
        char* Body=Path?MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,NULL):NULL;xrtFree(Path);
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

static inline xvalue* MdoAgentConfigResolve(cstr Id,cstr Text,bool Main,char* Error,size_t Capacity)
{
    const char* Stack[16]={0};
    if (Error && Capacity) Error[0]=0;
    return MdoAgentConfigResolveDepth(Id,Text,Main,Stack,0,Error,Capacity);
}

#endif
