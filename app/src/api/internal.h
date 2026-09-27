#ifndef MDO_API_INTERNAL_H
#define MDO_API_INTERNAL_H

#include <xsbase.h>

#include "../../include/mdo/api.h"

#define MDO_API_RESPONSE_MAX_BYTES (256u * 1024u)
#define MDO_API_REQUEST_MAX_BYTES (256u * 1024u)
#define MDO_API_IMAGE_MAX_BYTES (8u * 1024u * 1024u)
#define MDO_API_DOWNLOAD_MAX_BYTES (33u * 1024u * 1024u)
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
MdoApiBodyStatus MdoApiBinaryBodyRead(MdoApiContext* pContext,
    size_t Limit, char** pData, size_t* pSize);
void MdoApiJsonBodyUnit(MdoApiJsonBody* pBody);
bool MdoApiReplyBodyError(MdoApiContext* pContext, MdoApiBodyStatus Status);
bool MdoApiReplyDownload(MdoApiContext* pContext, const void* pBody,
    size_t BodySize, cstr ContentDisposition, cstr EntityTag);

bool MdoApiBootstrapRoute(MdoApiContext* pContext);
bool MdoApiSettingsRoute(MdoApiContext* pContext);
bool MdoApiWorkspaceStateRoute(MdoApiContext* pContext);
bool MdoApiPaneLayoutRoute(MdoApiContext* pContext);
bool MdoApiModelsRoute(MdoApiContext* pContext);
bool MdoApiModelConfigRoute(MdoApiContext* pContext);
bool MdoApiAgentsRoute(MdoApiContext* pContext);
bool MdoApiModulesRoute(MdoApiContext* pContext);
bool MdoApiSkillsRoute(MdoApiContext* pContext);
bool MdoApiMcpRoute(MdoApiContext* pContext);
bool MdoApiProjectsRoute(MdoApiContext* pContext);
bool MdoApiProjectRoute(MdoApiContext* pContext);
bool MdoApiProjectPurgePreviewRoute(MdoApiContext* pContext);
bool MdoApiMemoryCollectionRoute(MdoApiContext* pContext);
bool MdoApiMemoryEntryRoute(MdoApiContext* pContext);
bool MdoApiMemoryOpenDirectoryRoute(MdoApiContext* pContext);
bool MdoApiSessionsRoute(MdoApiContext* pContext);
bool MdoApiFeedbackListRoute(MdoApiContext* pContext);
bool MdoApiRunsRoute(MdoApiContext* pContext);
bool MdoApiSchedulesRoute(MdoApiContext* pContext);
bool MdoApiPermissionsRoute(MdoApiContext* pContext);
bool MdoApiTasksRoute(MdoApiContext* pContext);
bool MdoApiApprovalsRoute(MdoApiContext* pContext);
bool MdoApiApprovalRoute(MdoApiContext* pContext);
bool MdoApiAsksRoute(MdoApiContext* pContext);
bool MdoApiAskRoute(MdoApiContext* pContext);
bool MdoApiWorkspaceFilesRoute(MdoApiContext* pContext);
bool MdoApiProjectWorkspaceFilesRoute(MdoApiContext* pContext);
bool MdoApiTaskRoute(MdoApiContext* pContext);
bool MdoApiTaskOutputRoute(MdoApiContext* pContext);
bool MdoApiTaskEventsRoute(MdoApiContext* pContext);
bool MdoApiArtifactsRoute(MdoApiContext* pContext);
bool MdoApiArtifactRoute(MdoApiContext* pContext);
bool MdoApiSessionArtifactRoute(MdoApiContext* pContext);
bool MdoApiDiagnosticsRoute(MdoApiContext* pContext);
bool MdoApiStorageRoute(MdoApiContext* pContext);
bool MdoApiLegacyMigrationsRoute(MdoApiContext* pContext);
bool MdoApiEventsRoute(MdoApiContext* pContext);
bool MdoApiSessionEventsRoute(MdoApiContext* pContext);
bool MdoApiFeedbackRoute(MdoApiContext* pContext);
bool MdoApiFeedbackInit(void);
void MdoApiFeedbackUnit(void);
bool MdoApiFeedbackReconcile(const char* ProjectId, const char* SessionId);
bool MdoApiDraftRoute(MdoApiContext* pContext);
bool MdoApiDraftSubmissionAppendRoute(MdoApiContext* pContext);
bool MdoApiDraftSubmissionRoute(MdoApiContext* pContext);
bool MdoApiDraftInit(void);
void MdoApiDraftUnit(void);
bool MdoApiDraftAttachmentReferenced(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Referenced);
bool MdoApiQueueRoute(MdoApiContext* pContext);
bool MdoApiQueueItemRoute(MdoApiContext* pContext);
bool MdoApiQueueInit(void);
void MdoApiQueueUnit(void);
bool MdoApiQueueAttachmentReferenced(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Referenced);
bool MdoApiQueueDiscardAcknowledged(const char* ProjectId,
    const char* SessionId, const char* Id);
typedef enum MdoApiQueueRunStatus {
    MDO_API_QUEUE_RUN_READY,
    MDO_API_QUEUE_RUN_CONFLICT,
    MDO_API_QUEUE_RUN_ACCEPTED,
    MDO_API_QUEUE_RUN_STARTING,
    MDO_API_QUEUE_RUN_PROFILE_PENDING,
    MDO_API_QUEUE_RUN_UNAVAILABLE
} MdoApiQueueRunStatus;
MdoApiQueueRunStatus MdoApiQueueRunPrepare(const char* ProjectId,
    const char* SessionId, const char* Id, xstrview Prompt,
    const char Attachments[4][33], size_t AttachmentCount);
MdoApiQueueRunStatus MdoApiQueueRunClaim(const char* ProjectId,
    const char* SessionId, const char* Id, xstrview Prompt,
    const char Attachments[4][33], size_t AttachmentCount);
bool MdoApiQueueRunReleaseClaim(const char* ProjectId,
    const char* SessionId, const char* Id);
bool MdoApiQueueRunBind(const char* ProjectId, const char* SessionId,
    const char* Id, xstrview Prompt, const char Attachments[4][33],
    size_t AttachmentCount, const char* RunId);
bool MdoApiTodoRoute(MdoApiContext* pContext);
bool MdoApiOperationsRoute(MdoApiContext* pContext);
bool MdoApiOperationRoute(MdoApiContext* pContext);
bool MdoApiSettingsPreviewRoute(MdoApiContext* pContext);
bool MdoApiSettingsMutationRoute(MdoApiContext* pContext);
bool MdoApiSessionCreateRoute(MdoApiContext* pContext);
bool MdoApiSessionsInit(void);
void MdoApiSessionsUnit(void);
bool MdoApiSessionProfileRoute(MdoApiContext* pContext);
bool MdoApiSessionRoute(MdoApiContext* pContext);
bool MdoApiSessionRestoreRoute(MdoApiContext* pContext);
bool MdoApiSessionHistoryRoute(MdoApiContext* pContext);
bool MdoApiSessionRecoveryRoute(MdoApiContext* pContext);
bool MdoApiSessionResumeRoute(MdoApiContext* pContext);
bool MdoApiSessionAbandonRoute(MdoApiContext* pContext);
bool MdoApiSessionForkRoute(MdoApiContext* pContext);
bool MdoApiSessionTruncateRoute(MdoApiContext* pContext);
bool MdoApiSessionClearRoute(MdoApiContext* pContext);
bool MdoApiSessionExportRoute(MdoApiContext* pContext);
bool MdoApiRunStartRoute(MdoApiContext* pContext);
bool MdoApiAttachmentsRoute(MdoApiContext* pContext);
bool MdoApiAttachmentRoute(MdoApiContext* pContext);
bool MdoApiAttachmentsInit(void);
void MdoApiAttachmentsUnit(void);
bool MdoApiAttachmentLock(void);
void MdoApiAttachmentUnlock(void);
bool MdoApiReplyImage(MdoApiContext* pContext, const void* pBody,
    size_t BodySize, cstr ContentType);
bool MdoAttachmentReadForRun(const char* Project, const char* Session,
    const char* Id, char** pData, size_t* pSize, cstr* pMime);
bool MdoAttachmentIdsRead(const xvalue* Array, char Ids[4][33],
    size_t* pCount);
bool MdoAttachmentIdsExist(const char* Project, const char* Session,
    const char Ids[4][33], size_t Count);
bool MdoAttachmentIdsWriteValue(xvalue* Object, const char Ids[4][33],
    size_t Count);
bool MdoApiRunRoute(MdoApiContext* pContext);
bool MdoApiScheduleRoute(MdoApiContext* pContext);
bool MdoApiScheduleEnabledRoute(MdoApiContext* pContext);
bool MdoApiScheduleHistoryRoute(MdoApiContext* pContext);
bool MdoApiScheduleRunRoute(MdoApiContext* pContext);
bool MdoApiModelsReloadRoute(MdoApiContext* pContext);
bool MdoApiSkillsReloadRoute(MdoApiContext* pContext);
bool MdoApiModulesReloadRoute(MdoApiContext* pContext);
bool MdoApiMcpReloadRoute(MdoApiContext* pContext);
bool MdoApiMcpEnabledRoute(MdoApiContext* pContext);
bool MdoApiMcpDisconnectRoute(MdoApiContext* pContext);
bool MdoApiMcpRefreshRoute(MdoApiContext* pContext);

#endif
