#include <string.h>

#include "internal.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/modules.h"
#include "../../include/mdo/schedules.h"
#include "../../include/mdo/sessions.h"
#include "../../include/mdo/skills.h"

#define MDO_API_DIAGNOSTIC_LIMIT 256u

static cstr MdoApiModuleDiagnosticStage(MdoModuleDiagnosticStage Stage)
{
    switch ( Stage ) {
    case MDO_MODULE_DIAGNOSTIC_DISCOVERY: return "discovery";
    case MDO_MODULE_DIAGNOSTIC_READ: return "read";
    case MDO_MODULE_DIAGNOSTIC_COMPILE: return "compile";
    case MDO_MODULE_DIAGNOSTIC_ENTRY: return "entry";
    case MDO_MODULE_DIAGNOSTIC_REGISTER: return "register";
    case MDO_MODULE_DIAGNOSTIC_VALIDATE: return "validate";
    case MDO_MODULE_DIAGNOSTIC_PUBLISH: return "publish";
    default: return "unknown";
    }
}

static cstr MdoApiSkillDiagnosticStage(MdoSkillDiagnosticStage Stage)
{
    switch ( Stage ) {
    case MDO_SKILL_DIAGNOSTIC_DISCOVERY: return "discovery";
    case MDO_SKILL_DIAGNOSTIC_OPEN: return "open";
    case MDO_SKILL_DIAGNOSTIC_FRONTMATTER: return "frontmatter";
    case MDO_SKILL_DIAGNOSTIC_RESOURCE: return "resource";
    case MDO_SKILL_DIAGNOSTIC_PUBLISH: return "publish";
    default: return "unknown";
    }
}

static cstr MdoApiMcpDiagnosticStage(MdoMcpDiagnosticStage Stage)
{
    switch ( Stage ) {
    case MDO_MCP_DIAGNOSTIC_DISCOVERY: return "discovery";
    case MDO_MCP_DIAGNOSTIC_READ: return "read";
    case MDO_MCP_DIAGNOSTIC_PARSE: return "parse";
    case MDO_MCP_DIAGNOSTIC_SECRET: return "secret";
    case MDO_MCP_DIAGNOSTIC_RUNTIME: return "runtime";
    case MDO_MCP_DIAGNOSTIC_PUBLISH: return "publish";
    default: return "unknown";
    }
}

static bool MdoApiDiagnosticAppend(xvalue* Items, cstr Domain, cstr Stage,
    uint64 StageCode, cstr SubjectId, cstr Path, cstr Hash, cstr Message)
{
    xvalue* Item = xrtValueObject();
    bool Ok = Item != NULL &&
        MdoApiValueSetString(Item, "domain", Domain) &&
        MdoApiValueSetString(Item, "stage", Stage) &&
        MdoApiValueSetUInt(Item, "stage_code", StageCode) &&
        MdoApiValueSetString(Item, "subject_id", SubjectId) &&
        MdoApiValueSetString(Item, "path", Path) &&
        MdoApiValueSetString(Item, "hash", Hash) &&
        MdoApiValueSetString(Item, "message", Message) &&
        MdoApiValueAppendTake(Items, &Item);
    xrtValueRelease(Item);
    return Ok;
}

bool MdoApiDiagnosticsRoute(MdoApiContext* Context)
{
    MdoModuleDiagnostics* Modules = MdoModuleDiagnosticsSnapshot();
    MdoSkillDiagnostics* Skills = MdoSkillDiagnosticsSnapshot();
    MdoMcpDiagnostics* Mcp = MdoMcpDiagnosticsSnapshot();
    xwork_error Error;
    MdoSessionCatalog* Sessions;
    MdoScheduleCatalog* Schedules;
    xvalue* Data = xrtValueObject();
    xvalue* Items = xrtValueArray();
    size_t Total = 0u;
    size_t Added = 0u;
    size_t Index;
    bool Ok;

    memset(&Error, 0, sizeof(Error));
    Sessions = MdoSessionCatalogSnapshot(&Error);
    memset(&Error, 0, sizeof(Error));
    Schedules = MdoScheduleCatalogSnapshot(&Error);
    Ok = Modules != NULL && Skills != NULL && Mcp != NULL &&
        Sessions != NULL && Schedules != NULL && Data != NULL && Items != NULL;
    if ( Ok ) Total = MdoModuleDiagnosticsCount(Modules) +
        MdoSkillDiagnosticsCount(Skills) + MdoMcpDiagnosticsCount(Mcp) +
        MdoSessionCatalogDiagnosticCount(Sessions) +
        MdoScheduleCatalogDiagnosticCount(Schedules);

    for ( Index = 0u; Ok && Added < MDO_API_DIAGNOSTIC_LIMIT &&
          Index < MdoModuleDiagnosticsCount(Modules); Index++, Added++ ) {
        MdoModuleDiagnosticInfo Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoModuleDiagnosticsAt(Modules, Index, &Info) &&
            MdoApiDiagnosticAppend(Items, "modules",
                MdoApiModuleDiagnosticStage(Info.Stage), Info.Stage, "",
                Info.SourcePath, Info.SourceHash, Info.Message);
    }
    for ( Index = 0u; Ok && Added < MDO_API_DIAGNOSTIC_LIMIT &&
          Index < MdoSkillDiagnosticsCount(Skills); Index++, Added++ ) {
        MdoSkillDiagnosticInfo Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoSkillDiagnosticsAt(Skills, Index, &Info) &&
            MdoApiDiagnosticAppend(Items, "skills",
                MdoApiSkillDiagnosticStage(Info.Stage), Info.Stage,
                Info.SkillId, Info.SourcePath, "", Info.Message);
    }
    for ( Index = 0u; Ok && Added < MDO_API_DIAGNOSTIC_LIMIT &&
          Index < MdoMcpDiagnosticsCount(Mcp); Index++, Added++ ) {
        MdoMcpDiagnosticInfo Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoMcpDiagnosticsAt(Mcp, Index, &Info) &&
            MdoApiDiagnosticAppend(Items, "mcp",
                MdoApiMcpDiagnosticStage(Info.Stage), Info.Stage,
                Info.ServerId, Info.SourcePath, Info.SourceHash, Info.Message);
    }
    for ( Index = 0u; Ok && Added < MDO_API_DIAGNOSTIC_LIMIT &&
          Index < MdoSessionCatalogDiagnosticCount(Sessions);
          Index++, Added++ ) {
        MdoSessionDiagnostic Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoSessionCatalogDiagnosticAt(Sessions, Index, &Info) &&
            MdoApiDiagnosticAppend(Items, "sessions", "load", 0u, "",
                Info.Path, "", Info.Message);
    }
    for ( Index = 0u; Ok && Added < MDO_API_DIAGNOSTIC_LIMIT &&
          Index < MdoScheduleCatalogDiagnosticCount(Schedules);
          Index++, Added++ ) {
        MdoScheduleDiagnostic Info;
        memset(&Info, 0, sizeof(Info)); Info.Size = sizeof(Info);
        Ok = MdoScheduleCatalogDiagnosticAt(Schedules, Index, &Info) &&
            MdoApiDiagnosticAppend(Items, "schedules", "load", 0u, "",
                Info.Path, "", Info.Message);
    }
    if ( Ok ) Ok =
        MdoApiValueSetUInt(Data, "total", Total) &&
        MdoApiValueSetBool(Data, "truncated", Total > Added) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    MdoScheduleCatalogRelease(Schedules);
    MdoSessionCatalogRelease(Sessions);
    MdoMcpDiagnosticsRelease(Mcp);
    MdoSkillDiagnosticsRelease(Skills);
    MdoModuleDiagnosticsRelease(Modules);
    if ( !Ok ) { xrtValueRelease(Data); Data = NULL; }
    if ( Data == NULL ) return MdoApiReplyError(Context, 500u,
        "diagnostics_unavailable", "Diagnostics could not be created", NULL);
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
