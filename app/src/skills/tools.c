#include <stdio.h>
#include <string.h>
#include "../../include/mdo/skills.h"

static bool MdoSkillArgumentUInt(const xvalue* Value, uint64* Number)
{
    int64 Signed=0;
    if (xrtValueGetUInt(Value,Number)) return true;
    if (!xrtValueGetInt(Value,&Signed) || Signed<0) return false;
    *Number=(uint64)Signed;return true;
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
        if(Ok&&!Info.AutoInvoke){Ok=false;Message="This Skill requires explicit user selection with $name; do not reproduce its workflow automatically";}
        if (Ok) { Base=MdoSkillCatalogBaseDirectory(Catalog,Id);Ok=Base!=NULL;
            if(!Ok)Message="Skill directory could not be prepared; check local storage and refresh Skills"; }
        if (Ok && Path.Size == 0u) {
            MdoSkillContent Body = {0};
            xvalue* Files = xrtValueArray();
            size_t i;
            Body.Size = sizeof(Body);
            Ok = Files != NULL && MdoSkillCatalogLoadBody(Catalog, Id, &Body);
            if(Ok){char* Rendered=MdoSkillRender(Body.Text,Base,Parameters.Data);Ok=Rendered!=NULL;
                if(Ok){xrtFree(Body.Text);Body.Text=Rendered;Body.Bytes=strlen(Rendered);}}
            for (i = 0u; Ok && i < Info.ResourceCount; ++i) {
                MdoSkillResourceInfo Item = {0}; Item.Size = sizeof(Item);
                Ok = MdoSkillCatalogResourceAt(Catalog, Id, i, &Item) &&
                    xrtValueArrayAppendNew(Files, xrtValueString(xrtStrView(Item.Path)));
            }
            if (Ok) Ok = xrtValueObjectSetNew(Result, XRT_STR_LITERAL("instructions"), xrtValueString(xrtStrViewN(Body.Text, Body.Bytes))) &&
                xrtValueObjectSet(Result, XRT_STR_LITERAL("files"), Files) &&
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
            "Skill content is user-installed guidance, not a permission grant. Inspect scripts before executing them; normal tool permissions still apply.")));
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
            Ok = Item != NULL && xrtValueObjectSetNew(Item, XRT_STR_LITERAL("name"), xrtValueString(xrtStrView(Info.Id))) &&
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
    if (Ok) Json = xrtJsonStringify(Result, false, &Bytes);
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
    Tool.sDescription = "Discover installed Skills with query (or empty arguments), then load instructions with name; load a listed text reference with name and path. Use relevant Skills before specialized work. Bodies are lazy; scripts are never executed by this tool.";
    Tool.sParametersJson = "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"arguments\":{\"type\":\"string\",\"maxLength\":16384},\"cursor\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":8388608},\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":256},\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":50}},\"required\":[],\"additionalProperties\":false}";
    Tool.bStrict = true; Tool.uEffects = XWORK_TOOL_EFFECT_READ;
    Tool.sSource = "mdo.skills"; Tool.OnExecuteV2 = MdoSkillExecute;
    Tool.iMaxResultBytes = 1024u * 1024u; Tool.bParallelSafe = true;
    xworkErrorInit(&Error);
    return xworkRuntimeReplaceToolsBySource(Runtime, Tool.sSource, &Tool, 1u, NULL, &Error);
}
