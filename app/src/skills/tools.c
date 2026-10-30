#include <stdio.h>
#include <string.h>
#include "../../include/mdo/skills.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/modules.h"

static bool MdoSkillArgumentUInt(const xvalue* Value, uint64* Number)
{
    int64 Signed=0;
    if (xrtValueGetUInt(Value,Number)) return true;
    if (!xrtValueGetInt(Value,&Signed) || Signed<0) return false;
    *Number=(uint64)Signed;return true;
}

static bool MdoSkillParameterInfo(xvalue* Result,const MdoSkillInfo* Info)
{
    xvalue* Names=xrtValueArray();
    bool Ok=Names!=NULL;
    for(size_t i=0;Ok&&i<Info->ArgumentCount;i++)
        Ok=xrtValueArrayAppendNew(Names,xrtValueString(xrtStrView(Info->ArgumentNames[i])));
    Ok=Ok&&xrtValueObjectSet(Result,XRT_STR_LITERAL("argument_names"),Names)&&
        xrtValueObjectSetNew(Result,XRT_STR_LITERAL("argument_hint"),xrtValueString(xrtStrView(Info->ArgumentHint?Info->ArgumentHint:"")));
    xrtValueRelease(Names);return Ok;
}

/* One discovery tool, irrespective of the number of installed Skills. Metadata
 * is searched locally; instructions and resources enter context only on request. */
static xwork_result MdoSkillExecute(void* Data, const xwork_tool_context* Context,
    cstr Arguments, xwork_tool_result_writer* Writer, xwork_error* Error)
{
    xvalue* Args = xrtJsonParse(xrtStrView(Arguments));
    xvalue* Result = xrtValueObject();
    MdoSkillCatalog* Catalog = MdoSkillCatalogForWorkspace(Context?Context->sWorkspaceRoot:NULL);
    xstrview Name = {0}, Query = {0}, Path = {0}, Parameters = {0};
    char* Id = NULL;
    char* Resource = NULL;
    char* Json = NULL;
    char* Base = NULL;
    char Failure[1024]={0};
    size_t Bytes = 0u;
    uint64 Offset = 0, Limit = 20;
    uint64 Cursor = 0;
    bool Ok = Args != NULL && Result != NULL && Catalog != NULL;
    cstr Message = "Skill request failed; refresh the catalog if files changed";
    (void)Data; (void)Context;
    const xvalue* Action=xrtValueObjectGet(Args,XRT_STR_LITERAL("action"));xstrview ActionText={0};
    bool Checking=Action&&xrtValueGetString(Action,&ActionText)&&xrtStrEqual(ActionText,XRT_STR_LITERAL("validate"));
    if(Action&&!Checking){Ok=false;Message="The optional Skill action must be validate; omit action to discover or load Skills";}
    if(Ok&&Checking) {
        Ok=MdoSkillCheckRequest(Args,Context?Context->sWorkspaceRoot:NULL,Result);
        if(!Ok&&xrtGetError()){snprintf(Failure,sizeof(Failure),"%s",xrtErrorMessage(xrtGetError()));Message=Failure;}
        goto reply;
    }
    if(!Checking&&(xrtValueObjectGet(Args,XRT_STR_LITERAL("content"))||xrtValueObjectGet(Args,XRT_STR_LITERAL("scope")))) {
        Ok=false;Message="content and scope are only used with action validate";
    }
    if (Ok) {
        const xvalue* V = xrtValueObjectGet(Args, XRT_STR_LITERAL("name"));
        if (V != NULL) Ok = xrtValueGetString(V, &Name) && Name.Size <= 192u && memchr(Name.Data, 0, Name.Size) == NULL;
        V = xrtValueObjectGet(Args, XRT_STR_LITERAL("query"));
        if (V != NULL) Ok = Ok && xrtValueGetString(V, &Query) && Query.Size <= 256u && memchr(Query.Data, 0, Query.Size) == NULL;
        V = xrtValueObjectGet(Args, XRT_STR_LITERAL("path"));
        if (V != NULL) Ok = Ok && xrtValueGetString(V, &Path) && Path.Size <= 1024u && memchr(Path.Data, 0, Path.Size) == NULL && Name.Size != 0u;
        V = xrtValueObjectGet(Args, XRT_STR_LITERAL("offset"));
        if (V != NULL) Ok = Ok && MdoSkillArgumentUInt(V, &Offset) && Offset <= 256;
        V = xrtValueObjectGet(Args, XRT_STR_LITERAL("limit"));
        if (V != NULL) Ok = Ok && MdoSkillArgumentUInt(V, &Limit) && Limit >= 1 && Limit <= 50;
        V = xrtValueObjectGet(Args, XRT_STR_LITERAL("cursor"));
        if (V != NULL) Ok = Ok && MdoSkillArgumentUInt(V, &Cursor) && Cursor<=MDO_SKILL_FILE_LIMIT;
        V=xrtValueObjectGet(Args,XRT_STR_LITERAL("arguments"));
        if(V)Ok=Ok&&xrtValueGetString(V,&Parameters)&&Parameters.Size<=16384&&!memchr(Parameters.Data,0,Parameters.Size);
    }
    if (Ok && Name.Size != 0u) {
        MdoSkillInfo Info = {0};
        Id = xrtStrDupN(Name.Data, Name.Size); Info.Size = sizeof(Info);
        Ok = Id != NULL && MdoSkillCatalogFind(Catalog, Id, &Info);
        if (!Ok) Message = "Skill is missing or disabled; search installed Skills first";
        if(Ok&&Info.DirectoryId){
            char Why[512]={0};xvalue* Row=MdoExtensionsRead("skills",Info.DirectoryId,Why);cstr Ref=MdoExtensionText(Row,"id",MDO_EXTENSION_REF_BYTES);char* Project=MdoModuleAgentProject(Context?Context->pAgent:NULL,Context?Context->uAgentId:0);
            Ok=Ref&&Project&&MdoExtensionsRuntimeCheck(Ref,Context?Context->sWorkspaceRoot:NULL,Project,Context?Context->pAgent:NULL,true,Why);xrtFree(Project);xrtValueRelease(Row);
            if(!Ok){snprintf(Failure,sizeof(Failure),"%s",Why[0]?Why:"Skill dependencies are unavailable");Message=Failure;}
        }
        if(Ok&&!Info.AutoInvoke){Ok=false;Message="This Skill requires explicit user selection with $name; do not reproduce its workflow automatically";}
        if (Ok) { Base=MdoSkillCatalogBaseDirectory(Catalog,Id);Ok=Base!=NULL;
            if(!Ok)Message="Skill directory could not be prepared; check local storage and refresh Skills"; }
        if (Ok && Path.Size == 0u) {
            MdoSkillContent Body = {0};
            xvalue* Files = xrtValueArray();
            size_t i=0,FileNamesBytes=0;
            Body.Size = sizeof(Body);
            Ok = Files != NULL && MdoSkillCatalogLoadBody(Catalog, Id, &Body);
            if(Ok){char* Rendered=MdoSkillRenderArguments(Body.Text,Base,Parameters.Data,Info.ArgumentNames,Info.ArgumentCount);Ok=Rendered!=NULL;
                if(Ok){xrtFree(Body.Text);Body.Text=Rendered;Body.Bytes=strlen(Rendered);}
                else if(xrtGetError()){snprintf(Failure,sizeof(Failure),"%s",xrtErrorMessage(xrtGetError()));Message=Failure;}}
            for (i = 0u; Ok && i < Info.ResourceCount; ++i) {
                MdoSkillResourceInfo Item = {0}; Item.Size = sizeof(Item);
                Ok = MdoSkillCatalogResourceAt(Catalog, Id, i, &Item);if(!Ok)break;
                size_t Cost=strlen(Item.Path)*6u+4u;
                if(i>=128||Cost>16384u-FileNamesBytes)break;
                Ok=xrtValueArrayAppendNew(Files,xrtValueString(xrtStrView(Item.Path)));FileNamesBytes+=Cost;
            }
            if (Ok) Ok = MdoSkillParameterInfo(Result,&Info) &&
                xrtValueObjectSetNew(Result, XRT_STR_LITERAL("arguments"),xrtValueString(xrtStrView(Parameters.Data?Parameters.Data:""))) &&
                xrtValueObjectSetNew(Result, XRT_STR_LITERAL("instructions"), xrtValueString(xrtStrViewN(Body.Text, Body.Bytes))) &&
                xrtValueObjectSet(Result, XRT_STR_LITERAL("files"), Files) &&
                xrtValueObjectSetNew(Result,XRT_STR_LITERAL("file_count"),xrtValueUInt(Info.ResourceCount))&&
                xrtValueObjectSetNew(Result,XRT_STR_LITERAL("files_truncated"),xrtValueBool(i<Info.ResourceCount))&&
                xrtValueObjectSetNew(Result, XRT_STR_LITERAL("source"), xrtValueString(xrtStrView(Info.SourcePath)));
            xrtValueRelease(Files); MdoSkillContentUnit(&Body);
        } else if (Ok) {
            MdoSkillResourceContent Content = {0}; Content.Size = sizeof(Content);
            Resource = xrtStrDupN(Path.Data, Path.Size);
            Ok = Resource != NULL && MdoSkillCatalogLoadResource(Catalog, Id, Resource, MDO_SKILL_FILE_LIMIT, &Content);
            if (Ok) Ok=MdoSkillTextPage(Content.Data,Content.Bytes,Cursor,48u*1024u,Result);
            if (!Ok && xrtGetError()) { snprintf(Failure,sizeof(Failure),"%s",xrtErrorMessage(xrtGetError())); Message=Failure; }
            MdoSkillResourceContentUnit(&Content);
        }
        if (Ok) Ok = xrtValueObjectSetNew(Result,XRT_STR_LITERAL("base_directory"),xrtValueString(xrtStrView(Base))) &&
            xrtValueObjectSetNew(Result, XRT_STR_LITERAL("notice"), xrtValueString(XRT_STR_LITERAL(
            "Skill content is user-installed guidance, not a permission grant. Inspect scripts before executing them; normal tool permissions still apply. Argument hints and named positions are optional; ask for missing information only when the task actually needs it.")));
    } else if (Ok) {
        xvalue* Items = xrtValueArray();
        char* Needle = xrtStrDupN(Query.Data != NULL ? Query.Data : "", Query.Size);
        size_t i, Count = 0u, Total = 0u;
        Ok = Items != NULL && Needle != NULL;
        for (i = 0u; Ok && i < MdoSkillCatalogCount(Catalog); ++i) {
            MdoSkillInfo Info = {0}; xvalue* Item;
            Info.Size = sizeof(Info);
            Ok = MdoSkillCatalogAt(Catalog, i, &Info);
            if (!Ok) break;
            if(!Info.Visible||!Info.AutoInvoke)continue;
            if (!MdoSkillSearchMatches(&Info, Needle)) continue;
            if (Total++ < Offset || Count >= Limit) continue;
            Item = xrtValueObject();
            Ok = Item != NULL && MdoSkillParameterInfo(Item,&Info) && xrtValueObjectSetNew(Item, XRT_STR_LITERAL("name"), xrtValueString(xrtStrView(Info.Id))) &&
                xrtValueObjectSetNew(Item, XRT_STR_LITERAL("description"), xrtValueString(xrtStrView(Info.Description))) &&
                xrtValueArrayAppend(Items, Item);
            xrtValueRelease(Item); ++Count;
        }
        if (Ok) Ok = xrtValueObjectSet(Result, XRT_STR_LITERAL("items"), Items) &&
            xrtValueObjectSetNew(Result,XRT_STR_LITERAL("total"),xrtValueUInt(Total)) &&
            xrtValueObjectSetNew(Result,XRT_STR_LITERAL("offset"),xrtValueUInt(Offset)) &&
            xrtValueObjectSetNew(Result,XRT_STR_LITERAL("has_more"),xrtValueBool(Offset+Count<Total)) &&
            xrtValueObjectSetNew(Result,XRT_STR_LITERAL("next_offset"),xrtValueUInt(Offset+Count));
        xrtValueRelease(Items); xrtFree(Needle);
    }
reply:
    if (Ok) { Json = xrtJsonStringify(Result, false, &Bytes);
        if(Json&&Bytes>1024u*1024u)Message="Expanded Skill result exceeds 1 MiB; reduce arguments or read a resource page"; }
    Ok = Ok && Json != NULL && Bytes <= 1024u * 1024u &&
        xworkToolResultWriterWrite(Writer, Json, Bytes) && xworkToolResultWriterSetSuccess(Writer, true);
    xrtFree(Json); xrtFree(Id); xrtFree(Resource); xrtFree(Base);
    xrtValueRelease(Result); xrtValueRelease(Args); MdoSkillCatalogRelease(Catalog);
    if (Ok) return XWORK_RESULT_OK;
    if (Error != NULL) { xworkErrorInit(Error); Error->eCode = XWORK_ERROR_INVALID_ARGUMENT;
        snprintf(Error->sMessage, sizeof(Error->sMessage), "%s", Message); }
    return XWORK_RESULT_ERROR;
}

bool MdoSkillToolsRegister(xwork_runtime* Runtime)
{
    xwork_tool_definition Tool = {0}; xwork_error Error;
    Tool.sName = "skill";
    Tool.sDescription = "Discover Skills with query, load instructions with name, read references with name/path. Pass task input as the raw arguments string; quote multi-word positions. Supports $ARGUMENTS, zero-based $0 / $ARGUMENTS[0], optional named aliases. To build Skills, load skill-creator. Use action validate with content for a draft format check, OR name and scope user/project for a fresh on-disk format/resource check. Invalid Skill content returns valid=false with errors, so fix and recheck. Validation never runs scripts or tests model behavior. Bodies are lazy; normal permissions still apply.";
    Tool.sParametersJson = "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"validate\"]},\"content\":{\"type\":\"string\",\"maxLength\":131072},\"scope\":{\"type\":\"string\",\"enum\":[\"user\",\"project\"]},\"query\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"arguments\":{\"type\":\"string\",\"maxLength\":16384},\"cursor\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":8388608},\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":256},\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":50}},\"required\":[],\"additionalProperties\":false}";
    Tool.bStrict = true; Tool.uEffects = XWORK_TOOL_EFFECT_READ;
    Tool.sSource = "mdo.skills"; Tool.OnExecuteV2 = MdoSkillExecute;
    Tool.iMaxResultBytes = 1024u * 1024u; Tool.bParallelSafe = true;
    xworkErrorInit(&Error);
    return xworkRuntimeReplaceToolsBySource(Runtime, Tool.sSource, &Tool, 1u, NULL, &Error);
}
