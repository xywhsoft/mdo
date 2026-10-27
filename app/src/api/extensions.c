#include "internal.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/skills.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/commands.h"
#include "../../include/mdo/secrets.h"

static xmutex* g_MdoExtensionLock;
bool MdoApiExtensionsInit(void){g_MdoExtensionLock=xrtMutexCreate();return g_MdoExtensionLock&&MdoApiExtensionTasksInit();}
void MdoApiExtensionsUnit(void){MdoApiExtensionTasksUnit();if(g_MdoExtensionLock)xrtMutexDestroy(g_MdoExtensionLock);g_MdoExtensionLock=NULL;}
static void ExtensionBodyUnit(MdoApiJsonBody* Body){
 const xvalue* Secrets=xrtValueObjectGet(Body->Value,XRT_STR_LITERAL("secrets"));
 for(size_t I=0;I<xrtValueCount(Secrets);I++){const xvalue* V=xrtValueType(Secrets)==XVALUE_OBJECT?xrtValueObjectAt(Secrets,I,NULL):xrtValueArrayGet(Secrets,I);xstrview S={0};if(xrtValueGetString(V,&S))xrtSecureZero((void*)S.Data,S.Size);}
 if(Body->Document)xrtSecureZero(Body->Document,Body->Size);MdoApiJsonBodyUnit(Body);
}
static bool ExtensionString(const xvalue* Object,cstr Key,xstrview* Text)
{
    return xrtValueGetString(xrtValueObjectGet(Object,xrtStrView(Key)),Text) &&
        memchr(Text->Data,0,Text->Size)==NULL && xrtUtf8Valid(*Text,NULL);
}
static bool ExtensionMatch(MdoApiContext* C,cstr Text)
{
    const xhttpfield* Field=NULL; char Tag[72]; char Hash[65]; xstrview Value;
    xhttpnext Next=xrtHttpFieldGetUnique(C->Request->head->Fields,C->Request->head->FieldCount,
        XRT_STR_LITERAL("If-Match"),&Field);
    if (Next==XHTTP_NEXT_END) { (void)MdoApiReplyError(C,428,"precondition_required","Resource revision is required",NULL); return false; }
    if (Text) { if (!MdoExtensionHash(Text,Hash)) return false; snprintf(Tag,sizeof(Tag),"\"%s\"",Hash); }
    else strcpy(Tag,"\"new\"");
    Value=Field?xrtStrTrim(Field->Value):xrtStrView("");
    if (Next!=XHTTP_NEXT_ITEM || Value.Size!=strlen(Tag) || memcmp(Value.Data,Tag,Value.Size)) {
        (void)MdoApiReplyError(C,412,"revision_conflict","Resource changed; reload before saving or deleting",NULL); return false;
    }
    return true;
}

#include "directory_management.inc.c"
#include "project_extension_selection.inc.c"
#include "skill_management.inc.c"
#include "agent_authoring.inc.c"
#include "mcp_directory_connections.inc.c"
#include "mcp_auth_routes.inc.c"
