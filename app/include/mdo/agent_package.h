#ifndef MDO_AGENT_PACKAGE_H
#define MDO_AGENT_PACKAGE_H
#include "agent_config_format.h"

/* Pure portable validation, also compiled by the website. It never resolves
 * inheritance against a user's Home or executes a module. */
#define MDO_ROLE_MIN_BUILD 70000009u
static bool MdoPackageRoleKind(cstr Kind)
{return Kind&&(!strcmp(Kind,"agents")||!strcmp(Kind,"subagents"));}
static xvalue* MdoPackageRoleDocument(cstr Text,char* Error,size_t Capacity)
{
    if(!Text)return NULL;
    xstrview S=xrtStrTrim(xrtStrView(Text));
    xvalue* D=S.Size&&S.Data[0]=='{'?xrtJsonParse(S):MdoPromptParse(Text,true,Error,Capacity);
    if(!D||!MdoAgentConfigValidate(D,Error,Capacity)){xrtValueRelease(D);return NULL;}
    return D;
}
static bool MdoPackageRoleModern(const xvalue* R)
{
    if(xrtValueObjectGet(R,XRT_STR_LITERAL("files")))return true;
    cstr Text=MdoPackageText(R,"content",131072);if(!Text)return false;
    xstrview Trim=xrtStrTrim(xrtStrView(Text));if(Trim.Size&&Trim.Data[0]=='{')return true;
    xvalue* D=MdoPromptParse(Text,true,NULL,0);bool Modern=D&&MdoAgentConfigGet(D,"schema")!=NULL;xrtValueRelease(D);return Modern;
}
static bool MdoPackageRoleCode(const xvalue* R)
{
    if(!MdoPackageRoleKind(MdoPackageText(R,"kind",16)))return false;
    xvalue* D=MdoPackageRoleDocument(MdoPackageText(R,"content",131072),NULL,0);
    bool Code=MdoAgentConfigBool(D,"code",false);xrtValueRelease(D);return Code;
}
static bool MdoRoleDependency(xvalue* Out,cstr Kind,cstr Id,bool Required)
{
    if(!Id||!Id[0])return true;
    for(size_t I=0;I<xrtValueCount(Out);I++){
        xvalue* R=(xvalue*)xrtValueArrayGet(Out,I);
        if(!strcmp(MdoPackageText(R,"kind",32),Kind)&&!strcmp(MdoPackageText(R,"id",256),Id)){
            if(Required)return xrtValueObjectSetNew(R,XRT_STR_LITERAL("required"),xrtValueBool(true));
            return true;
        }
    }
    xvalue* R=xrtValueObject();bool Ok=R&&MdoAgentConfigSetText(R,"kind",Kind)&&MdoAgentConfigSetText(R,"id",Id)&&
        xrtValueObjectSetNew(R,XRT_STR_LITERAL("required"),xrtValueBool(Required))&&xrtValueArrayAppend(Out,R);
    xrtValueRelease(R);return Ok;
}
static xvalue* MdoPackageRoleDependencies(const xvalue* D,cstr Kind)
{
    xvalue* Out=xrtValueArray();bool Ok=Out!=NULL;cstr Parent=MdoAgentConfigText(D,"extends"),Model=MdoAgentConfigText(D,"model");
    if(Parent&&Parent[0]&&strcmp(Parent,"mdo.general")&&strcmp(Parent,"mdo.default")){
        cstr K=Kind,Id=Parent;if(!strncmp(Parent,"agent.",6)){K="agents";Id=Parent+6;}
        else if(!strncmp(Parent,"subagent.",9)){K="subagents";Id=Parent+9;}
        Ok=Ok&&MdoRoleDependency(Out,K,Id,true);
    }
    if(Model&&strcmp(Model,"inherit"))Ok=Ok&&MdoRoleDependency(Out,"models",Model,true);
    const char* Lists[]={"skills","mcp_servers"};const char* Kinds[]={"skills","mcp"};
    for(size_t I=0;Ok&&I<2;I++){
        const xvalue* List=MdoAgentConfigGet(D,Lists[I]);
        /* Legacy frontmatter lists are normalized by the local exporter. */
        for(size_t J=0;Ok&&J<xrtValueCount(List);J++){
            xstrview Id;Ok=xrtValueGetString(xrtValueArrayGet(List,J),&Id)&&MdoRoleDependency(Out,Kinds[I],Id.Data,true);
        }
    }
    const xvalue* Context=MdoAgentConfigGet(D,"context");const char* Sources[]={"knowledge","external_tools"};
    for(size_t I=0;Ok&&I<2;I++){
        const xvalue* Spec=MdoAgentConfigGet(Context,Sources[I]);if(!MdoAgentConfigBool(Spec,"enabled",true))continue;
        const xvalue* List=MdoAgentConfigGet(Spec,"include");
        for(size_t J=0;Ok&&J<xrtValueCount(List);J++){xstrview Id;Ok=xrtValueGetString(xrtValueArrayGet(List,J),&Id)&&MdoRoleDependency(Out,I?"external-tools":"knowledge",Id.Data,false);}
    }
    const xvalue* Delegation=MdoAgentConfigGet(D,"delegation");const xvalue* Allow=MdoAgentConfigGet(Delegation,"allow");
    for(size_t I=0;Ok&&I<xrtValueCount(Allow);I++){xstrview Id;Ok=xrtValueGetString(xrtValueArrayGet(Allow,I),&Id)&&MdoRoleDependency(Out,"roles",Id.Data,false);}
    if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
static bool MdoPackageRoleFile(const xvalue* Files,cstr Path)
{
    for(size_t I=0;I<xrtValueCount(Files);I++)if(!strcmp(Path,MdoPackageText(xrtValueArrayGet(Files,I),"path",220)))return true;
    return false;
}
static bool MdoPackageRoleReference(const xvalue* Files,cstr Id,cstr Path)
{
    if(!Path)return true;
    size_t N=strlen(Id);return !strncmp(Path,Id,N)&&Path[N]=='/'&&
        MdoPackagePath(Path+N+1)&&strcmp(Path+N+1,"program.c")&&MdoPackageRoleFile(Files,Path+N+1);
}
static bool MdoPackageRoleValidate(const xvalue* R,bool Strict,char* Error,size_t Capacity)
{
    cstr Kind=MdoPackageText(R,"kind",16),Id=MdoPackageText(R,"id",64),Text=MdoPackageText(R,"content",131072);
    xvalue* D=MdoPackageRoleDocument(Text,Error,Capacity);if(!D)return false;
    cstr Why="Invalid portable role resource",Schema=MdoAgentConfigText(D,"schema"),Mode=MdoAgentConfigText(D,"mode");
    const xvalue* Files=xrtValueObjectGet(R,XRT_STR_LITERAL("files"));bool Code=MdoAgentConfigBool(D,"code",false);
    bool Ok=(!Mode||!strcmp(Mode,"both")||!strcmp(Mode,!strcmp(Kind,"agents")?"primary":"subagent"));
    if(!Ok){Why="Role mode does not match its store category";goto done;}
    if(!Strict&&!Files)goto done;
    Ok=Schema&&!strcmp(Schema,"mdo.agent.v1")&&xrtStrTrim(xrtStrView(Text)).Data[0]=='{';if(!Ok){Why="Portable roles require mdo.agent.v1";goto done;}
    Ok=Code==MdoPackageRoleFile(Files,"program.c");if(!Ok){Why="C roles must include program.c; configuration roles must not include it";goto done;}
    Ok=MdoPackageRoleReference(Files,Id,MdoAgentConfigText(D,"prompt_file"));
    const xvalue* Context=MdoAgentConfigGet(D,"context");
    for(size_t I=0;Ok&&I<xrtValueCount(Context);I++){
        const xvalue* Spec=xrtValueObjectAt(Context,I,NULL);cstr Origin=MdoAgentConfigText(Spec,"origin");
        Ok=(!Origin||!strcmp(Origin,Kind))&&MdoPackageRoleReference(Files,Id,MdoAgentConfigText(Spec,"file"));
    }
    if(!Ok){Why="Role file references must name included files in this role's own directory";goto done;}
    for(size_t I=0;Ok&&I<xrtValueCount(Files);I++){
        const xvalue* F=xrtValueArrayGet(Files,I);cstr Path=MdoPackageText(F,"path",220);size_t N=0;
        cstr B64=MdoPackageText(F,"base64",90u*1024u*1024u);
        void* Bytes=B64?xrtBase64DecodeNew(B64,strlen(B64),&N,NULL):NULL;
        Ok=Bytes&&N<=MDO_EXTENSION_TEXT_LIMIT&&!memchr(Bytes,0,N)&&xrtUtf8Valid(xrtStrViewN(Bytes,N),NULL);
        /* All role attachments are reviewable source/documents, never native binaries. */
        if(Ok&&strcmp(Path,"program.c"))Ok=strncmp(Path,"assets/",7)==0;
        xrtFree(Bytes);
        for(size_t J=0;Ok&&J<I;J++){
            cstr Other=MdoPackageText(xrtValueArrayGet(Files,J),"path",220);size_t A=strlen(Path),B=strlen(Other),Short=A<B?A:B;
            char Prefix[221];cstr Longer=A<B?Other:Path,Smaller=A<B?Path:Other;memcpy(Prefix,Longer,Short);Prefix[Short]=0;
            if(Longer[Short]=='/'&&MdoPackageSamePath(Prefix,Smaller))Ok=false;
        }
    }
    if(!Ok)Why="Role attachments must be UTF-8 program.c or assets/ documents within 128 KiB each";
done:
    xrtValueRelease(D);if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"%s",Why);return Ok;
}
static bool MdoPackageRoleSingle(const xvalue* P,char* Error,size_t Capacity)
{
    const xvalue* Rs=xrtValueObjectGet(P,XRT_STR_LITERAL("resources"));const xvalue* R=xrtValueArrayGet(Rs,0);
    if(!MdoPackageRoleKind(MdoPackageText(R,"kind",16)))return true;
    int64 Min=0;if(!MdoPackageV2(P)||!MdoPackageNumber(xrtValueObjectGet(P,XRT_STR_LITERAL("manifest")),"min_build_id",&Min)||Min<MDO_ROLE_MIN_BUILD){
        if(Error&&Capacity)snprintf(Error,Capacity,"Agent and SubAgent stores require a 0.7.0 capable client");return false;
    }
    return xrtValueCount(Rs)==1&&MdoPackageRoleValidate(R,true,Error,Capacity);
}
#endif
