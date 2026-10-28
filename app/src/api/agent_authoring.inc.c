#include "skill_scope.inc.h"
#include "../../include/mdo/agents.h"

bool MdoApiAgentPlanRoute(MdoApiContext* C)
{
    char Kind[33],Id[97],FullId[96],Alias[65]={0};
    if(!MdoDirectoryApiParams(C,Kind,Id) || !Id[0] || (strcmp(Kind,"agents") && strcmp(Kind,"subagents")))
        return MdoApiReplyError(C,400,"agent_plan_invalid","Select an Agent or SubAgent definition",NULL);
    char Why[512]={0};xvalue* Installed=MdoExtensionsRead(Kind,Id,Why);cstr InstalledAlias=MdoExtensionText(Installed,"alias",64);
    if(InstalledAlias)snprintf(Alias,sizeof(Alias),"%s",InstalledAlias);
    xrtValueRelease(Installed);
    char* Workspace=NULL;
    if(!MdoApiSkillScopeMode(C,&Workspace,false))return true;
    char Project[65]="";
    if(C->Target.Query.Size){size_t N=0;if(xrtPercentDecode(xrtStrViewN(C->Target.Query.Data+8,C->Target.Query.Size-8),Project,64,&N))Project[N]=0;}
    MdoApiJsonBody Body={0};char* Text=NULL;
    if(C->Request->head->MethodCode==XHTTP_METHOD_POST){
        MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
        if(Status!=MDO_API_BODY_OK){xrtFree(Workspace);return MdoApiReplyBodyError(C,Status);}
        xstrview Content;
        if(!ExtensionString(Body.Value,"content",&Content) || Content.Size>MDO_EXTENSION_TEXT_LIMIT){
            xrtFree(Workspace);MdoApiJsonBodyUnit(&Body);return MdoApiReplyError(C,400,"agent_plan_invalid","Expected a bounded role configuration",NULL);
        }
        Text=xrtStrDupN(Content.Data,Content.Size);
    }
    if(!Alias[0]&&Text&&MdoExtensionAlias(Id))snprintf(Alias,sizeof(Alias),"%s",Id);
    if(!Alias[0]){xrtFree(Text);xrtFree(Workspace);MdoApiJsonBodyUnit(&Body);return MdoApiReplyError(C,404,"agent_plan_missing","Role directory or callable alias is unavailable",NULL);}
    snprintf(FullId,sizeof(FullId),"%s.%s",!strcmp(Kind,"agents")?(!strcmp(Alias,"default")?"mdo":"agent"):"subagent",Alias);
    xwork_error Error;xvalue* Plan=MdoAgentDefinitionInspect(Text?Alias:FullId,Text,!strcmp(Kind,"agents"),Workspace,Project,&Error);
    xrtFree(Text);xrtFree(Workspace);MdoApiJsonBodyUnit(&Body);
    if(!Plan)return MdoApiReplyError(C,422,"agent_plan_invalid",Error.sMessage[0]?Error.sMessage:"Cannot inspect this role",NULL);
    return MdoApiReplySuccessTake(C,200,Plan,NULL);
}
