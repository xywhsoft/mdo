#ifndef MDO_API_INTERNAL_H
#define MDO_API_INTERNAL_H

#include <xsbase.h>

#include "../../include/mdo/api.h"
#include "profile.h"

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
    bool WriteShared;
    bool WriteExclusive;
    /* Set only on a copied, owned request in the download executor. */
    xdeadline SendDeadline;
    xcancel* SendCancel;
    bool CloseResponse;
    bool Takeover;
} MdoApiContext;

struct MdoSessionEventInfo;
bool MdoApiLiveInit(void);
void MdoApiLiveUnit(void);
void MdoApiLiveRelease(void);
void MdoApiLiveChanged(void* Data);
bool MdoApiLiveRoute(MdoApiContext* Context);
bool MdoApiSessionEventValue(const struct MdoSessionEventInfo* Event,
    const char* ProjectId, const char* SessionId, bool FullText, xvalue** Value);

typedef bool (*MdoApiRouteProc)(MdoApiContext* pContext);
bool MdoApiUpdateRoute(MdoApiContext* Context);
bool MdoApiUpdateDownloadRoute(MdoApiContext* Context);
bool MdoApiUpdateInstallRoute(MdoApiContext* Context);

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
/* Takes optional structured facts even on failure (e.g. committed deletion
 * with pending cleanup). Clients must not infer rollback from HTTP status. */
bool MdoApiReplyErrorDetailsTake(MdoApiContext* pContext, uint16 Status,
    cstr Code, cstr Message, xvalue* Details);
bool MdoApiReplyOptions(MdoApiContext* pContext, cstr Allow);
MdoApiBodyStatus MdoApiJsonBodyRead(MdoApiContext* pContext,
    MdoApiJsonBody* pBody);
MdoApiBodyStatus MdoApiBinaryBodyRead(MdoApiContext* pContext,
    size_t Limit, char** pData, size_t* pSize);
void MdoApiJsonBodyUnit(MdoApiJsonBody* pBody);
bool MdoApiReplyBodyError(MdoApiContext* pContext, MdoApiBodyStatus Status);
bool MdoApiReplyDownload(MdoApiContext* pContext, const void* pBody,
    size_t BodySize, cstr ContentDisposition, cstr EntityTag);
bool MdoApiReplyBackupDownload(MdoApiContext* Context, const void* Body,
    size_t Bytes, cstr Disposition, cstr EntityTag);
bool MdoApiDownloadsInit(void);
void MdoApiDownloadsUnit(void);
bool MdoApiDownloadSend(MdoApiContext* Context, const void* Data, size_t Bytes);
bool MdoApiDownloadLive(const MdoApiContext* Context);
bool MdoApiImageDownloadsInit(void);
void MdoApiImageDownloadsUnit(void);
/* IDs are validated by the attachment route. Only copied IDs/method/request ID
 * and owned stream references escape; no body is allocated until execution. */
bool MdoApiImageDownloadStart(MdoApiContext* Context, cstr Project,
    cstr Session, cstr Id);
/* Always consumes Session. Success takes over only this connection; the
 * executor copies request fields and retains the stream before admission. */
bool MdoApiSessionBackupStart(MdoApiContext* Context, struct MdoSession* Session);
bool MdoApiSessionBackupRoute(MdoApiContext* Context);

bool MdoApiBootstrapRoute(MdoApiContext* pContext);
bool MdoApiAccountRoute(MdoApiContext* pContext);
bool MdoApiRemoteRoute(MdoApiContext* Context);
bool MdoApiReplySecretSuccessTake(MdoApiContext* Context, xvalue* Data);
bool MdoApiReplyAccountHtml(MdoApiContext* Context, uint16 Status, cstr Html);
bool MdoApiSettingsRoute(MdoApiContext* pContext);
bool MdoApiWorkspaceStateRoute(MdoApiContext* pContext);
bool MdoApiWorkspaceStateInit(void);
void MdoApiWorkspaceStateUnit(void);
bool MdoApiPaneLayoutRoute(MdoApiContext* pContext);
bool MdoApiModelsRoute(MdoApiContext* pContext);
bool MdoApiModelConfigRoute(MdoApiContext* pContext);
bool MdoApiAgentsRoute(MdoApiContext* pContext);
bool MdoApiModulesRoute(MdoApiContext* pContext);
bool MdoApiSkillsRoute(MdoApiContext* pContext);
bool MdoApiMcpRoute(MdoApiContext* pContext);
bool MdoApiProjectsRoute(MdoApiContext* pContext);
bool MdoApiProjectRoute(MdoApiContext* pContext);
bool MdoApiDirectoriesRoute(MdoApiContext* pContext);
bool MdoApiProjectPurgePreviewRoute(MdoApiContext* pContext);
int MdoApiProjectExpectedRevision(const MdoApiContext* Context,
    const char* Id, uint64* Revision, bool* MatchesProject);
bool MdoApiProjectPurgeRoute(MdoApiContext* pContext);
bool MdoApiProjectPurgeCancelRoute(MdoApiContext* pContext);
bool MdoApiProjectPurgeResultRoute(MdoApiContext* pContext);
bool MdoApiMemoryCollectionRoute(MdoApiContext* pContext);
bool MdoApiMemoryEntryRoute(MdoApiContext* pContext);
bool MdoApiMemoryOpenDirectoryRoute(MdoApiContext* pContext);
bool MdoApiSessionsRoute(MdoApiContext* pContext);
bool MdoApiRunsRoute(MdoApiContext* pContext);
bool MdoApiSchedulesRoute(MdoApiContext* pContext);
bool MdoApiPermissionsRoute(MdoApiContext* pContext);
bool MdoApiTasksRoute(MdoApiContext* pContext);
bool MdoApiApprovalsRoute(MdoApiContext* pContext);
bool MdoApiApprovalRoute(MdoApiContext* pContext);
bool MdoApiAsksRoute(MdoApiContext* pContext);
bool MdoApiTaskAsksRoute(MdoApiContext* pContext);
bool MdoApiTaskAskRoute(MdoApiContext* pContext);
bool MdoApiAskRoute(MdoApiContext* pContext);
bool MdoApiWorkspaceFilesRoute(MdoApiContext* pContext);
bool MdoApiProjectWorkspaceFilesRoute(MdoApiContext* pContext);
bool MdoApiTaskRoute(MdoApiContext* pContext);
bool MdoApiTaskOutputRoute(MdoApiContext* pContext);
bool MdoApiArtifactsRoute(MdoApiContext* pContext);
bool MdoApiArtifactRoute(MdoApiContext* pContext);
bool MdoApiSessionArtifactRoute(MdoApiContext* pContext);
bool MdoApiDiagnosticsRoute(MdoApiContext* pContext);
bool MdoApiStorageRoute(MdoApiContext* pContext);
bool MdoApiLegacyMigrationsRoute(MdoApiContext* pContext);
bool MdoApiSessionEventsRoute(MdoApiContext* pContext);
bool MdoApiSessionTurnsRoute(MdoApiContext* pContext);
bool MdoApiDraftRoute(MdoApiContext* pContext);
bool MdoApiDraftSubmissionAppendRoute(MdoApiContext* pContext);
bool MdoApiDraftSubmissionRoute(MdoApiContext* pContext);
bool MdoApiDraftInit(void);
void MdoApiDraftUnit(void);
bool MdoApiDraftAttachmentReferenced(const char* ProjectId,
    const char* SessionId, const char* Id, bool* Referenced);
bool MdoApiQueueRoute(MdoApiContext* pContext);
bool MdoApiQueueItemRoute(MdoApiContext* pContext);
bool MdoApiQueueDiscardRoute(MdoApiContext* pContext);
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
    MDO_API_QUEUE_RUN_UNAVAILABLE
} MdoApiQueueRunStatus;
MdoApiQueueRunStatus MdoApiQueueRunPrepare(const char* ProjectId,
    const char* SessionId, const char* Id, xstrview Prompt,
    const char Attachments[4][33], size_t AttachmentCount,
    MdoApiProfile* Profile);
MdoApiQueueRunStatus MdoApiQueueRunClaim(const char* ProjectId,
    const char* SessionId, const char* Id, xstrview Prompt,
    const char Attachments[4][33], size_t AttachmentCount,
    const MdoApiProfile* ExpectedProfile);
bool MdoApiQueueRunReleaseClaim(const char* ProjectId,
    const char* SessionId, const char* Id);
bool MdoApiQueueRunRecordPrepared(const char* ProjectId,
    const char* SessionId, const char* Id, const char* RunId,
    uint64 AgentRunId);
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
typedef struct MdoApiSessionCaptureGuard {
    bool Attachment;
    bool Draft;
    bool Queue;
} MdoApiSessionCaptureGuard;
/* All try-locks, fixed attachment -> draft -> queue order. Covers
 * GET-triggered repair and cleanup as well as background/direct API helpers.
 * Then call MdoSessionWithCapture/ExportJson; release before network output. */
bool MdoApiSessionCaptureAcquire(MdoApiSessionCaptureGuard* Guard);
void MdoApiSessionCaptureRelease(MdoApiSessionCaptureGuard* Guard);
bool MdoApiAttachmentCaptureTryLock(void);
bool MdoApiDraftCaptureTryLock(void);
void MdoApiDraftCaptureUnlock(void);
bool MdoApiQueueCaptureTryLock(void);
void MdoApiQueueCaptureUnlock(void);
bool MdoApiRunStartRoute(MdoApiContext* pContext);
bool MdoApiAttachmentsRoute(MdoApiContext* pContext);
bool MdoApiAttachmentRoute(MdoApiContext* pContext);
bool MdoApiAttachmentInfoRoute(MdoApiContext* pContext);
bool MdoApiAttachmentsInit(void);
void MdoApiAttachmentsUnit(void);
bool MdoApiAttachmentLock(void);
void MdoApiAttachmentUnlock(void);
/* Best-effort cleanup for old, unreferenced uploads when a session reopens.
 * The caller must not hold the draft or queue lock. */
bool MdoApiAttachmentSweepExpired(const char* ProjectId,
    const char* SessionId);
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
