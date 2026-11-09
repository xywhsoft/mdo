#ifndef MDO_PROMPT_CONTRIBUTION_H
#define MDO_PROMPT_CONTRIBUTION_H
#include "extension_manifest.h"

#define MDO_PLUGIN_PROMPT_COUNT 16u
#define MDO_PLUGIN_PROMPT_BYTES 2048u
#define MDO_PLUGIN_PROMPTS_BYTES 4096u
#define MDO_ROLE_PLUGIN_PROMPTS_BYTES 16384u
#define MDO_PLUGIN_PROMPT_TOOLS 32u

static bool MdoPromptKeys(const xvalue* V,cstr Keys){
    if(xrtValueType(V)!=XVALUE_OBJECT)return false;
    for(size_t I=0;I<xrtValueCount(V);I++){
        xstrview K;xrtValueObjectAt(V,I,&K);char Token[128];
        if(K.Size>120||memchr(K.Data,0,K.Size))return false;
        snprintf(Token,sizeof(Token),"|%s|",K.Data);if(!strstr(Keys,Token))return false;
    }return true;
}
static const xvalue* MdoPromptGet(const xvalue* V,cstr Key){return xrtValueObjectGet(V,xrtStrView(Key));}
static bool MdoPromptToolsValid(const xvalue* V){
    if(!V)return true;
    if(xrtValueType(V)!=XVALUE_ARRAY||xrtValueCount(V)>MDO_PLUGIN_PROMPT_TOOLS)return false;
    for(size_t I=0;I<xrtValueCount(V);I++){
        xstrview S;if(!xrtValueGetString(xrtValueArrayGet(V,I),&S)||memchr(S.Data,0,S.Size)||!MdoExtensionIdentifier(S.Data,128))return false;
        for(size_t J=0;J<I;J++)if(xrtValueEqual(xrtValueArrayGet(V,I),xrtValueArrayGet(V,J)))return false;
    }return true;
}
static bool MdoPromptValid(const xvalue* P,char* Error,size_t Capacity){
    cstr Why="Invalid plugin prompt contribution";
    cstr Id=MdoExtensionText(P,"id",64),Text=MdoExtensionText(P,"text",MDO_PLUGIN_PROMPT_BYTES),File=MdoExtensionText(P,"file",MDO_EXTENSION_PATH_BYTES);
    if(!MdoPromptKeys(P,"|id||title||text||file||targets||activation||when||priority|")||!MdoExtensionAlias(Id))goto failed;
    if((Text!=NULL)==(File!=NULL)|| (Text&&!xrtStrTrim(xrtStrView(Text)).Size) || (File&&!MdoExtensionRelativePath(File))){Why="A prompt needs one nonempty UTF-8 text or own relative file (at most 2 KiB)";goto failed;}
    if(MdoPromptGet(P,"text")&&!Text)goto failed;
    if(MdoPromptGet(P,"file")&&!File)goto failed;
    if(MdoPromptGet(P,"title")&&!MdoExtensionText(P,"title",256))goto failed;
    const xvalue* Targets=MdoPromptGet(P,"targets");
    if(Targets){
        if(xrtValueType(Targets)!=XVALUE_ARRAY||!xrtValueCount(Targets)||xrtValueCount(Targets)>2)goto failed;
        for(size_t I=0;I<xrtValueCount(Targets);I++){
            xstrview S;if(!xrtValueGetString(xrtValueArrayGet(Targets,I),&S)||(!xrtStrEqual(S,XRT_STR_LITERAL("primary"))&&!xrtStrEqual(S,XRT_STR_LITERAL("subagent"))))goto failed;
            if(I&&xrtValueEqual(xrtValueArrayGet(Targets,0),xrtValueArrayGet(Targets,I)))goto failed;
        }
    }
    cstr Activation=MdoExtensionText(P,"activation",32);const xvalue* When=MdoPromptGet(P,"when");
    if(MdoPromptGet(P,"activation")&&(!Activation||(strcmp(Activation,"with-tools")&&strcmp(Activation,"explicit"))))goto failed;
    if(When&&!MdoPromptKeys(When,"|any_tools||all_tools|"))goto failed;
    const xvalue* Any=MdoPromptGet(When,"any_tools"),*All=MdoPromptGet(When,"all_tools");
    if(!MdoPromptToolsValid(Any)||!MdoPromptToolsValid(All))goto failed;
    if((!Activation||!strcmp(Activation,"with-tools"))&&!xrtValueCount(Any)&&!xrtValueCount(All)){Why="with-tools prompts require exact tool conditions; prompt-only contributions require explicit selection";goto failed;}
    int64 Priority=100;if(MdoPromptGet(P,"priority")&&(!MdoExtensionNumber(P,"priority",&Priority)||Priority<0||Priority>10000))goto failed;
    return true;
failed:
    if(Error&&Capacity)snprintf(Error,Capacity,"%s",Why);return false;
}
#endif
