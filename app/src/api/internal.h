#ifndef MDO_API_INTERNAL_H
#define MDO_API_INTERNAL_H

#include <xsbase.h>

#include "../../include/mdo/api.h"

#define MDO_API_RESPONSE_MAX_BYTES (256u * 1024u)
#define MDO_API_REQUEST_MAX_BYTES (256u * 1024u)
#define MDO_API_REQUEST_ID_CAPACITY 40u
#define MDO_API_ROUTE_PARAM_MAX 4u

typedef struct MdoApiContext {
    XS_HttpReq* Request;
    xhttptarget Target;
    char RequestId[MDO_API_REQUEST_ID_CAPACITY];
    xstrview Params[MDO_API_ROUTE_PARAM_MAX];
    size_t ParamCount;
} MdoApiContext;

typedef bool (*MdoApiRouteProc)(MdoApiContext* pContext);

typedef enum MdoApiBodyStatus {
    MDO_API_BODY_OK = 0,
    MDO_API_BODY_MISSING,
    MDO_API_BODY_UNSUPPORTED_TYPE,
    MDO_API_BODY_TOO_LARGE,
    MDO_API_BODY_INVALID,
    MDO_API_BODY_READ_FAILED
} MdoApiBodyStatus;

typedef struct MdoApiJsonBody {
    char* Document;
    size_t Size;
    xvalue* Value;
} MdoApiJsonBody;

bool MdoApiValueSetString(xvalue* Object, cstr Key, cstr Value);
bool MdoApiValueSetStringView(xvalue* Object, cstr Key, xstrview Value);
bool MdoApiValueSetUInt(xvalue* Object, cstr Key, uint64 Value);
bool MdoApiValueSetInt(xvalue* Object, cstr Key, int64 Value);
bool MdoApiValueSetBool(xvalue* Object, cstr Key, bool Value);
bool MdoApiValueSetTake(xvalue* Object, cstr Key, xvalue** pChild);
bool MdoApiValueAppendTake(xvalue* Array, xvalue** pItem);
bool MdoApiValueAppendString(xvalue* Array, cstr Value);
bool MdoApiValueSetStrings(xvalue* Object, cstr Key,
    const char* const* Values, size_t Count);

bool MdoApiReplySuccessTake(MdoApiContext* pContext, uint16 Status,
    xvalue* pData, cstr Allow);
bool MdoApiReplySuccessTakeEntityTag(MdoApiContext* pContext, uint16 Status,
    xvalue* pData, cstr EntityTag);
bool MdoApiReplySuccessTakeRevision(MdoApiContext* pContext, uint16 Status,
    xvalue* pData, uint64 Revision);
bool MdoApiReplyError(MdoApiContext* pContext, uint16 Status, cstr Code,
    cstr Message, cstr Allow);
bool MdoApiReplyOptions(MdoApiContext* pContext, cstr Allow);
MdoApiBodyStatus MdoApiJsonBodyRead(MdoApiContext* pContext,
    MdoApiJsonBody* pBody);
void MdoApiJsonBodyUnit(MdoApiJsonBody* pBody);
bool MdoApiReplyBodyError(MdoApiContext* pContext, MdoApiBodyStatus Status);

bool MdoApiBootstrapRoute(MdoApiContext* pContext);
bool MdoApiSettingsRoute(MdoApiContext* pContext);
bool MdoApiModelsRoute(MdoApiContext* pContext);
bool MdoApiAgentsRoute(MdoApiContext* pContext);
bool MdoApiModulesRoute(MdoApiContext* pContext);
bool MdoApiSkillsRoute(MdoApiContext* pContext);
bool MdoApiMcpRoute(MdoApiContext* pContext);
bool MdoApiProjectsRoute(MdoApiContext* pContext);
bool MdoApiSessionsRoute(MdoApiContext* pContext);
bool MdoApiRunsRoute(MdoApiContext* pContext);
bool MdoApiSchedulesRoute(MdoApiContext* pContext);
bool MdoApiPermissionsRoute(MdoApiContext* pContext);
bool MdoApiTasksRoute(MdoApiContext* pContext);
bool MdoApiArtifactsRoute(MdoApiContext* pContext);
bool MdoApiDiagnosticsRoute(MdoApiContext* pContext);
bool MdoApiStorageRoute(MdoApiContext* pContext);
bool MdoApiEventsRoute(MdoApiContext* pContext);
bool MdoApiSessionEventsRoute(MdoApiContext* pContext);
bool MdoApiOperationsRoute(MdoApiContext* pContext);
bool MdoApiOperationRoute(MdoApiContext* pContext);
bool MdoApiSettingsPreviewRoute(MdoApiContext* pContext);
bool MdoApiSettingsMutationRoute(MdoApiContext* pContext);
bool MdoApiSessionCreateRoute(MdoApiContext* pContext);
bool MdoApiSessionRoute(MdoApiContext* pContext);
bool MdoApiModelsReloadRoute(MdoApiContext* pContext);
bool MdoApiSkillsReloadRoute(MdoApiContext* pContext);
bool MdoApiModulesReloadRoute(MdoApiContext* pContext);
bool MdoApiMcpReloadRoute(MdoApiContext* pContext);
bool MdoApiMcpEnabledRoute(MdoApiContext* pContext);
bool MdoApiMcpDisconnectRoute(MdoApiContext* pContext);
bool MdoApiMcpRefreshRoute(MdoApiContext* pContext);

#endif
