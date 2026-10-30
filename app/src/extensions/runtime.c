#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/skills.h"
#include "../../include/mdo/knowledge.h"

static bool MdoDependencyProjectAllows(cstr Workspace,const xvalue* Row,char Error[512])
{
    if(!Workspace||!Workspace[0])return true;
    cstr Kind=MdoExtensionText(Row,"kind",32),Id=MdoExtensionText(Row,"id",MDO_EXTENSION_REF_BYTES);if(!Kind||!Id)return false;
    xrtClearError();xroot Root=xrtRootOpen(Workspace);if(!Root){const xerror* E=xrtGetError();if(E&&xrtErrorKind(E)==XERR_NOT_FOUND){xrtClearError();return true;}if(Error)snprintf(Error,512,"Cannot inspect dependency project scope");return false;}
    char Path[96];snprintf(Path,sizeof(Path),".mdo/%s.json",Kind);xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ|XFILE_NOFOLLOW;xfile F=xrtRootFileOpen(Root,Path,&O);xrtRootClose(Root);
    if(!F){const xerror* E=xrtGetError();if(E&&xrtErrorKind(E)==XERR_NOT_FOUND){xrtClearError();return true;}if(Error)snprintf(Error,512,"Cannot read project extension selection");return false;}
    uint64 Size=0;bool Ok=xrtFileSize(F,&Size)&&Size<=65536;char* Text=Ok?xrtMalloc((size_t)Size+1):NULL;Ok=Ok&&Text&&xrtReadFull(F,Text,(size_t)Size,NULL);xrtClose(F);
    xvalue* Settings=NULL;if(Ok){Text[Size]=0;Ok=!memchr(Text,0,(size_t)Size)&&xrtUtf8Valid(xrtStrViewN(Text,(size_t)Size),NULL);if(Ok)Settings=xrtJsonParse(xrtStrViewN(Text,(size_t)Size));}xrtFree(Text);
    bool Commands=!strcmp(Kind,"commands");const xvalue* Flags=Commands?xrtValueObjectGet(Settings,XRT_STR_LITERAL("disabled")):Settings;
    Ok=Ok&&xrtValueType(Flags)==XVALUE_OBJECT;
    for(size_t I=0;Ok&&I<xrtValueCount(Flags);I++){xstrview Key;const xvalue* Value=xrtValueObjectAt(Flags,I,&Key);bool B;cstr Type;Ok=Key.Size<=MDO_EXTENSION_REF_BYTES&&!memchr(Key.Data,0,Key.Size)&&(Type=MdoExtensionRefKind(Key.Data))&&!strcmp(Type,Kind)&&xrtValueGetBool(Value,&B);}
    bool Selected=true;const xvalue* Flag=xrtValueObjectGet(Flags,xrtStrView(Id));if(Ok&&Flag){bool B;Ok=xrtValueGetBool(Flag,&B);Selected=Commands?!B:B;}
    xrtValueRelease(Settings);if((!Ok||!Selected)&&Error)snprintf(Error,512,"%.160s: %s",Id,Ok?"disabled by this project":"project extension selections require valid typed IDs; migrate the existing selections");return Ok&&Selected;
}
static bool MdoDependencyToolAvailable(cstr Ref,xwork_agent* Agent)
{
    xwork_tool_catalog* Tools=Agent?xworkAgentToolCatalogSnapshot(Agent):NULL;MdoModuleCatalog* Modules=MdoModuleCatalogSnapshot();bool Found=false;
    for(size_t I=0;!Found&&I<MdoModuleCatalogToolCount(Modules);I++){
        MdoModuleToolInfo Module={0};Module.Size=sizeof(Module);if(!MdoModuleCatalogToolAt(Modules,I,&Module)||!Module.ExtensionId||strcmp(Module.ExtensionId,Ref))continue;
        if(!Agent){Found=true;break;}for(size_t J=0;J<xworkToolCatalogCount(Tools);J++){xwork_tool_info Tool={0};if(xworkToolCatalogToolAt(Tools,J,&Tool)&&!strcmp(Tool.sName,Module.Id)){Found=true;break;}}
    }
    xworkToolCatalogRelease(Tools);MdoModuleCatalogRelease(Modules);return Found;
}
bool MdoExtensionsProjectAllows(cstr Id,cstr Workspace,char Error[512])
{
    xvalue* Row=MdoExtensionsResolve(Id,Error);bool Ok=Row&&MdoDependencyProjectAllows(Workspace,Row,Error);xrtValueRelease(Row);return Ok;
}
bool MdoExtensionsRuntimeCheck(cstr Id,cstr Workspace,cstr Scope,xwork_agent* Agent,bool ForModel,char Error[512])
{
    if(Error)Error[0]=0;
    if(!Id||!Id[0])return true;
    if(!MdoExtensionsPreflight(Id,Error))return false;
    xvalue* Rows=MdoExtensionsDependencies(Id,false,Error),*References=NULL;bool Ok=Rows!=NULL,ReadReferences=false;uint64 KnowledgeBudget=16u*1024u*1024u;
    for(size_t I=0;Ok&&I<xrtValueCount(Rows);I++){
        const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Ref=MdoExtensionText(Row,"id",MDO_EXTENSION_REF_BYTES),Kind=MdoExtensionText(Row,"kind",32);Ok=MdoDependencyProjectAllows(Workspace,Row,Error);
        if(Ok&&!strcmp(Kind,"mcp")){
            for(size_t P=0;Ok&&P<xrtValueCount(Rows);P++){
                const xvalue* Parent=xrtValueArrayGet(Rows,P),*Deps=xrtValueObjectGet(Parent,XRT_STR_LITERAL("dependencies"));
                for(size_t D=0;Ok&&D<xrtValueCount(Deps);D++){
                    const xvalue* Spec=xrtValueArrayGet(Deps,D);cstr Target=MdoExtensionText(Spec,"id",MDO_EXTENSION_REF_BYTES);bool Optional=false;xrtValueGetBool(xrtValueObjectGet(Spec,XRT_STR_LITERAL("optional")),&Optional);
                    if(Optional||!Target||strcmp(Target,Ref))continue;
                    char* Preferred=MdoExtensionsBinding(MdoExtensionText(Parent,"id",MDO_EXTENSION_REF_BYTES),Ref,Scope,Error);if(Error&&Error[0])Ok=false;
                    char* Connection=Ok?MdoMcpDependencyConnection(Ref,Workspace,Preferred,true,Error):NULL;Ok=Ok&&Connection;xrtFree(Connection);xrtFree(Preferred);
                }
            }
        }else if(Ok&&!strcmp(Kind,"knowledge")){
            if(ForModel){
                if(!ReadReferences){References=MdoKnowledgeReferencesForWorkspace(Workspace);ReadReferences=true;}
                bool Found=false;for(size_t J=0;J<xrtValueCount(References);J++){cstr Candidate=MdoExtensionText(xrtValueArrayGet(References,J),"id",MDO_EXTENSION_REF_BYTES);if(Candidate&&!strcmp(Candidate,Ref)){Found=true;break;}}
                Ok=References&&Found;if(!Ok&&Error)snprintf(Error,512,"%.160s: required knowledge entry or its complete dependency set is unavailable in the current prompt budget and project",Ref);
            }else Ok=MdoKnowledgeReferenceReady(Ref,false,&KnowledgeBudget,Error);
        }
        else if(Ok&&(ForModel||I>0)&&!strcmp(Kind,"skills")){
            MdoSkillCatalog* Skills=MdoSkillCatalogForWorkspace(Workspace?Workspace:".");MdoSkillInfo Info={0};Info.Size=sizeof(Info);cstr Alias=MdoExtensionText(Row,"alias",64),Directory=MdoExtensionText(Row,"directory_id",96);Ok=Skills&&Alias&&MdoSkillCatalogFind(Skills,Alias,&Info)&&Info.DirectoryId&&Directory&&!strcmp(Info.DirectoryId,Directory);MdoSkillCatalogRelease(Skills);
            if(!Ok&&Error)snprintf(Error,512,"%.160s: required Skill is disabled, invalid or unavailable in this project",Ref);
        }else if(Ok&&(ForModel||I>0)&&!strcmp(Kind,"tools")){
            Ok=MdoDependencyToolAvailable(Ref,Agent);if(!Ok&&Error)snprintf(Error,512,"%.160s: no tool from this required provider is allowed by the current role and parent policy",Ref);
        }
    }
    xrtValueRelease(References);xrtValueRelease(Rows);return Ok;
}
