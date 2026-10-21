#ifndef MDO_API_SKILL_SCOPE_H
#define MDO_API_SKILL_SCOPE_H
#include "../../include/mdo/projects.h"

/* Project IDs, never caller-supplied filesystem paths, select the catalog. */
static bool MdoApiSkillScopeMode(MdoApiContext* Context,char** Workspace,bool Create)
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
    if(!strcmp(Id,"default"))*Workspace=MdoHomeDefaultWorkspacePath(Create);
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

static bool MdoApiSkillScope(MdoApiContext* Context,char** Workspace)
{
    return MdoApiSkillScopeMode(Context,Workspace,true);
}

#endif
