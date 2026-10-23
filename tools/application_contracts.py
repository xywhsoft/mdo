"""Product wire contracts used by SDK discovery (business checks stay in C).

Path identities may be supplied by the immutable invocation context. Provider,
tool and plugin documents deliberately use JsonValue, never an empty schema.
Every router handler must be classified here: new handlers cannot silently
acquire an opaque generic envelope at build time.
"""
from copy import deepcopy
import re

S={'type':'string'}
U={'type':'integer','minimum':0}
I={'type':'integer'}
B={'type':'boolean'}
def ref(name):return {'$ref':'#/$defs/'+name}
def array(item):return {'type':'array','items':item}
def obj(properties=None,required=(),closed=False):
    return {'type':'object','properties':properties or {},'required':list(required),
            'additionalProperties':False if closed else ref('JsonValue')}
def fields(spec):
    types={'s':S,'u':U,'i':I,'b':B,'j':ref('JsonValue'),'o':ref('Document'),'a':array(ref('JsonValue')),'ss':array(S)}
    return {name:deepcopy(types[kind] if kind in types else ref(kind)) for name,kind in (word.split(':') for word in spec.split())}
def document(spec,required=(),closed=False):return obj(fields(spec),required,closed)

DEFS={
 'JsonValue':{'type':['null','boolean','integer','number','string','array','object']},
 'Document':{'type':'object','additionalProperties':ref('JsonValue')},
 'Profile':document('model_id:s reasoning_effort:s permission_profile:s',('model_id','reasoning_effort','permission_profile'),True),
 'Project':document('id:s name:s workspace_root:s default_model_id:s managed:b revision:u created_at:i updated_at:i session_count:u schedule_count:u'),
 'Session':document('id:s project_id:s parent_session_id:s title:s agent_id:s model_id:s protocol:s reasoning_effort:s permission_profile:s workspace_root:s revision:u created_at:i updated_at:i last_sequence:u status:s archived:b deleted:b pinned:b runtime_open:b max_output_tokens:u'),
 'Run':document('id:s project_id:s session_id:s agent_id:s model_id:s state:s terminal:b created_at:i started_at:i ended_at:i cancel_requested:b result:o error_code:u'),
 'Operation':document('id:s state:s terminal:b method:s kind:s phase:s result:j error:j cancel_requested:b created_at:i started_at:i ended_at:i'),
 'Schedule':document('id:s label:s project_id:s agent_id:s model_id:s protocol:s reasoning_effort:s workspace_root:s input:s frequency:s interval:u start_at:i weekday_mask:u timezone:s utc_offset_seconds:i fold_policy:s misfire_policy:s misfire_grace_seconds:u max_catch_up:u overlap_policy:s max_concurrent_runs:u enabled:b notify:s max_output_tokens:u revision:u'),
 'MemoryEntry':document('id:s title:s content:s tags:ss pinned:b revision:u created_at:i updated_at:i'),
 'ExtensionFile':document('path:s content:s text:s base64:s'),
 'Draft':document('schema_version:u project_id:s session_id:s text:s attachments:ss composer_profile:j new_task:j submission:j submissions:a run_admission_uncertain:b revision:u'),
 'PromptHistory':document('schema_version:u items:a'),
 'QueueItem':document('id:s text:s attachments:ss profile:Profile state:s first:b priority:b revision:u run_id:s stage:b start_claimed:b'),
 'Page':document('items:a total:u count:u limit:u next_cursor:u latest_event_id:u has_more:b truncated:b revision:u generation:u'),
 'Snapshot':document('id:s state:s ready:b enabled:b generation:u revision:u items:a diagnostics:a message:s removed:b accepted:b opened:b'),
 'Remote':document('state:s enabled:b allow_remote:b name:s device_id:s devices:a job_id:s connector:o direct:o'),
 'Transfer':document('id:s state:s terminal:b direction:s project_id:s session_id:s device_id:s mode:s progress:o error:j items:a capabilities:o policy:o'),
 'Account':document('state:s logged_in:b user:j providers:a quota:j error:j pending:j'),
 'Upload':document('id:s bytes:u received_bytes:u chunk_max_bytes:u expires_in_ms:u sha256:s state:s validation:s restore_ready:b'),
 'Restore':document('id:s session_id:s project_id:s preview_id:s state:s phase:s accepted:b committed:b terminal:b cancel_requested:b restart_required:b cleanup_pending:b workspace_root:s error_code:u message:s'),
 'Preview':document('id:s upload_id:s state:s terminal:b phase:s restore_ready:b result_available:b file_count:u total_bytes:u validation:s message:s'),
 'Recovery':document('project_id:s session_id:s recovery_token:s history_epoch:s revision:u last_sequence:u resume_required:b automatic_resume:b automatic_retry_safe:b items:a'),
 'Binary':document('base64:s content_type:s bytes:u filename:s'),
 'Config':document('schema_version:u patch:o effective:o revision:u runtime_override:b items:a providers:a keys:o'),
 'Ecosystem':document('items:a types:a installed:a catalogs:a operation:j draft:j state:s id:s'),
 'Directory':document('path:s parent:s separator:s directories:ss shortcuts:a truncated:b'),
 'Attachment':document('id:s mime_type:s file_name:s size:u url:s created_at:i'),
 'SkillInvocation':document('id:s text:s',('id','text')),
 'PaneLayout':document('schema_version:u sidebar_width:u sidebar_open:b'),
 'SidebarGroup':obj({'id':{'type':'string','pattern':'^[0-9a-f]{32}$'},'name':{'type':'string','minLength':1,'maxLength':256,'description':'Nonblank name, at most 256 UTF-8 bytes.'}},('id','name'),True),
 'SidebarPlacement':obj({'key':{'type':'string','pattern':r'^(?:p:[A-Za-z0-9_-][A-Za-z0-9._-]{0,127}|s:[A-Za-z0-9_-][A-Za-z0-9._-]{0,127}/[A-Za-z0-9_-][A-Za-z0-9._-]{0,127})$'},
    'group':{'type':'string','pattern':'^(?:[0-9a-f]{32})?$','description':'Existing custom group ID, or empty for the original project location.'}},('key','group'),True),
 'SidebarLayout':obj({'schema_version':{'type':'integer','const':1},'revision':{'type':'integer','minimum':0,'maximum':9007199254740991},
    'view':{'type':'string','enum':['workspace','timeline']},'project_sort':{'type':'string','enum':['name','recent']},
    'groups':{**array(ref('SidebarGroup')),'maxItems':32},'placements':{**array(ref('SidebarPlacement')),'maxItems':2048},
    'collapsed':{**array({'type':'string','minLength':1,'maxLength':180}),'maxItems':256,'uniqueItems':True}},
    ('schema_version','revision','view','project_sort','groups','placements','collapsed'),True),
}
for name in ('Project','Session','Run','Schedule','MemoryEntry','Operation','QueueItem'):
    DEFS[name+'Page']=obj({**fields('total:u truncated:b revision:u generation:u next_cursor:u has_more:b'),'items':array(ref(name))})
DEFS['QueueItemPage']['properties'].update(fields('managed:b paused:b error:s cancel_run_id:s last_run_id:s discard_images:ss actions:a'))

INPUTS={
 'ProjectCreate':document('id:s name:s workspace_root:s default_model_id:s',('id','name'),True),
 'ProjectUpdate':document('name:s workspace_root:s default_model_id:s',('name','workspace_root','default_model_id'),True),
 'SessionCreate':document('project_id:s title:s agent_id:s model_id:s protocol:s reasoning_effort:s permission_profile:s workspace_root:s client_session_id:s max_output_tokens:u',('project_id',),True),
 'SessionUpdate':document('title:s archived:b deleted:b pinned:b',closed=True),
 'ProfileUpdate':document('model_id:s reasoning_effort:s permission_profile:s',('model_id','reasoning_effort','permission_profile'),True),
 'Permission':document('permission_profile:s',('permission_profile',),True),
 'RunStart':document('prompt:s attachments:ss automatic:b resume:b client_resume_id:s queue_item_id:s'),
 'Resume':document('recovery_token:s client_resume_id:s decisions:a resume:b automatic:b'),
 'Abandon':document('recovery_token:s'),
 'Fork':document('through_sequence:u title:s client_session_id:s'),
 'Truncate':document('through_sequence:u source_event_id:u client_edit_id:s',('through_sequence',),True),
 'Schedule':deepcopy(DEFS['Schedule']),
 'Enabled':document('enabled:b',('enabled',),True),
 'Ask':document('answer:s',('answer',),True),
 'Approval':document('decision:s',('decision',),True),
 'Memory':document('id:s title:s content:s tags:ss pinned:b',('id','title','content','tags','pinned'),True),
 'Draft':deepcopy(DEFS['Draft']),
 'PromptHistoryEntry':document('id:s text:s',('id','text'),True),
 'Submission':document('id:s text:s attachments:ss profile:Profile state:s stage:s interrupt:b revision:u'),
 'Queue':document('id:s text:s attachments:ss profile:Profile first:b priority:b stage:b managed:b',('id','text','first'),True),
 'QueueAction':document('id:s kind:s item:s before:s revision:u draft_revision:u',('id','kind','item','before','revision','draft_revision'),True),
 'QueueState':document('state:s',('state',),True),
 'Config':document('schema_version:u patch:o',('schema_version','patch'),True),
 'Extension':document('content:s disabled:b enabled:b revision:s files:ExtensionFiles'),
 'Command':document('invocation_id:s command_id:s raw_arguments:s session_id:s run_id:s revision:s',('invocation_id','command_id'),True),
 'SkillInvoke':document('id:s arguments:s',('id',),True),
 'SkillValidate':document('content:s'),
 'Migrate':document('source:s source_path:s destination:s project_id:s approved:b dry_run:b'),
 'ModelProbe':document('provider:o key:s model_id:s tools:b'),
 'ModelSetup':document('schema_version:u patch:o providers:a keys:o credential:o secret_ref:s'),
 'AccountLogin':document('identifier:s password:s remember:b',('remember',),True),
 'AccountCallback':document('callback_url:s',('callback_url',),True),
 'AccountSkip':document('id:u',('id',),True),
 'Empty':obj(closed=True),
 'Remote':document('allow_remote:b name:s action:s device_id:s mode:s job_id:s'),
 'Update':document('edition:s',closed=True),
 'Transfer':document('action:s mode:s kind:s project_id:s session_id:s device_id:s device_name:s title:s target_project_id:s target_directory:s source_device:s manifest_sha256:s manifest_bytes:u manifest:o id:s offset:u files:a allow_receive:b'),
 'Ecosystem':document('action:s id:s kind:s source:s version:s trust:b approved:b replace:b revision:s files:ExtensionFiles draft_id:s'),
 'Purge':document('purge_request_id:s project_id:s revision:u created_at:i confirmation:s'),
 'BackupUpload':document('id:s bytes:u sha256:s',('id','bytes'),True),
 'BackupPreview':document('upload_id:s id:s'),
 'BackupReview':document('project_id:s session_id:s title:s preview_id:s'),
 'SharedFiles':document('share_id:s file_id:s',('share_id','file_id'),True),
 'WorkspaceState':document('schema_version:u project_id:s session_id:s draft:j selection:j revision:u'),
 'PaneLayout':document('sidebar_width:u sidebar_open:b',('sidebar_width','sidebar_open'),True),
 'SidebarLayout':deepcopy(DEFS['SidebarLayout']),
 'McpContext':document('session_id:s project_id:s action:s prompt:s arguments:j context:j'),
 'McpAuth':document('client_id:s client_secret:s redirect_uri:s scopes:ss state:s code:s error:s'),
 'Application':document('method:s params:j context:o',('method',),True),
}
DEFS['ExtensionFiles']=array(ref('ExtensionFile'))
# Schedule create/replace share one complete definition; id is optional at
# create time and supplied by the path on replace.
INPUTS['Schedule']['required']=['label','input','start_at']
DEFS.update({name+'Input':schema for name,schema in INPUTS.items()})

# Handler classifications are intentional, not a generic unknown-handler
# fallback. GET/HEAD have no JSON body, while raw-byte routes are below.
BODY_GROUPS={
 'Application':'ApplicationRoute', 'ProjectCreate':'ProjectsRoute', 'ProjectUpdate':'ProjectRoute',
 'SessionCreate':'SessionsRoute','SessionUpdate':'SessionRoute','ProfileUpdate':'SessionProfileRoute',
 'Permission':'SessionPermissionRoute','RunStart':'RunStartRoute','Resume':'SessionResumeRoute','Abandon':'SessionAbandonRoute',
 'Fork':'SessionForkRoute','Truncate':'SessionTruncateRoute','Schedule':'SchedulesRoute ScheduleRoute',
 'Enabled':'ScheduleEnabledRoute McpEnabledRoute','Ask':'AskRoute TaskAskRoute','Approval':'ApprovalRoute',
 'Memory':'MemoryCollectionRoute','Draft':'DraftRoute','Submission':'DraftSubmissionAppendRoute DraftSubmissionRoute',
 'PromptHistoryEntry':'PromptHistoryRoute',
 'Queue':'QueueRoute QueueItemRoute','QueueAction':'QueueActionsRoute','Config':'SettingsPreviewRoute SettingsMutationRoute',
 'Extension':'ExtensionsRoute CommandProjectRoute SkillProjectRoute McpProjectRoute',
 'Command':'CommandInvocationRoute','SkillInvoke':'SkillInvokeRoute','SkillValidate':'SkillValidateRoute',
 'Migrate':'LegacyMigrationsRoute SkillMigrateRoute McpMigrateRoute','ModelProbe':'ModelDiscoverRoute ModelTestRoute',
 'ModelSetup':'ModelSetupRoute','AccountLogin':'AccountRoute','Remote':'RemoteRoute',
 'Update':'UpdateRoute','Transfer':'DeviceTransfersRoute',
 'Ecosystem':'EcosystemRoute','Purge':'ProjectPurgeRoute ProjectPurgeCancelRoute ProjectPurgeIntentPrepareRoute ProjectPurgeIntentRoute',
 'BackupUpload':'BackupUploadsRoute','BackupReview':'BackupRestoreReviewRoute',
 'SharedFiles':'SharedFilesRoute','WorkspaceState':'WorkspaceStateRoute','PaneLayout':'PaneLayoutRoute','SidebarLayout':'SidebarLayoutRoute',
 'McpContext':'McpContextRoute','McpAuth':'McpAuthRoute',
}
BODY={handler:name for name,handlers in BODY_GROUPS.items() for handler in handlers.split()}
NO_BODY=set('''CommandsRoute DistributionRoute CliToolsRoute LiveRoute DirectRoute UpdateExitRoute BootstrapRoute SettingsRoute
ModelsRoute ModelConfigRoute AgentsRoute BackupUploadRoute BackupUploadSealRoute BackupPreviewsRoute BackupPreviewRoute UpdateDownloadRoute UpdateInstallRoute
BackupRestoreApplyRoute BackupRestoresRoute BackupRestoreRoute BackupPreviewStartRoute ModulesRoute ToolsRoute SkillsRoute McpRoute DirectoriesRoute
ProjectPurgePreviewRoute ProjectPurgeResultRoute ProjectWorkspaceFilesRoute MemoryOpenDirectoryRoute MemoryEntryRoute
RunsRoute TasksRoute ApprovalsRoute AsksRoute WorkspaceFilesRoute ArtifactsRoute PermissionsRoute DiagnosticsRoute StorageRoute
OperationsRoute ModelsReloadRoute SkillsReloadRoute SkillSourcesRoute ModulesReloadRoute McpReloadRoute McpSourcesRoute
McpDisconnectRoute McpRefreshRoute OperationRoute SessionRestoreRoute SessionPurgeRoute SessionHistoryRoute AttachmentsRoute
AttachmentInfoRoute AttachmentRoute SessionArtifactRoute SessionRecoveryRoute SessionClearRoute SessionExportRoute SessionBackupRoute
RunRoute ScheduleHistoryRoute ScheduleRunRoute TaskOutputRoute TaskAsksRoute ArtifactRoute TaskRoute SessionTurnsRoute
SessionConversationRoute SessionEventsRoute QueueDiscardRoute TodoRoute'''.split())

RESULT_GROUPS={
 'ProjectPage':'ProjectsRoute','Project':'ProjectRoute', 'SessionPage':'SessionsRoute',
 'Session':'SessionRoute SessionRestoreRoute SessionPermissionRoute SessionProfileRoute SessionForkRoute SessionTruncateRoute SessionClearRoute',
 'RunPage':'RunsRoute','Run':'RunStartRoute RunRoute','SchedulePage':'SchedulesRoute','Schedule':'ScheduleRoute ScheduleEnabledRoute',
 'MemoryEntryPage':'MemoryCollectionRoute','MemoryEntry':'MemoryEntryRoute','Draft':'DraftRoute',
 'PromptHistory':'PromptHistoryRoute',
 'QueueItemPage':'QueueRoute QueueActionsRoute','QueueItem':'QueueItemRoute','Recovery':'SessionRecoveryRoute',
 'OperationPage':'OperationsRoute','Operation':'OperationRoute CommandInvocationRoute ModulesReloadRoute McpRefreshRoute ModelDiscoverRoute ModelTestRoute',
 'Remote':'RemoteRoute','Transfer':'DeviceTransfersRoute','Account':'AccountRoute','Upload':'BackupUploadsRoute BackupUploadRoute BackupUploadChunkRoute BackupUploadSealRoute',
 'Preview':'BackupPreviewStartRoute BackupPreviewRoute','Restore':'BackupRestoreReviewRoute BackupRestoreApplyRoute BackupRestoreRoute',
 'Config':'SettingsRoute ModelConfigRoute SettingsPreviewRoute SettingsMutationRoute ModelSetupRoute','Ecosystem':'EcosystemRoute',
 'Directory':'DirectoriesRoute',
 'Attachment':'AttachmentsRoute AttachmentInfoRoute',
 'SkillInvocation':'SkillInvokeRoute',
 'PaneLayout':'PaneLayoutRoute',
 'SidebarLayout':'SidebarLayoutRoute',
}
RESULT={handler:name for name,handlers in RESULT_GROUPS.items() for handler in handlers.split()}

QUERY_GROUPS={
 'project:s':'CommandsRoute CommandProjectRoute CommandInvocationRoute SkillsRoute SkillSourcesRoute SkillProjectRoute SkillInvokeRoute McpRoute McpSourcesRoute McpProjectRoute',
 'q:s limit:u include_directories:u':'WorkspaceFilesRoute ProjectWorkspaceFilesRoute',
 'path:s':'DirectoriesRoute',
 'after:u limit:u':'SessionEventsRoute SessionConversationRoute SessionTurnsRoute SessionHistoryRoute ScheduleHistoryRoute TaskOutputRoute',
 'project:s session:s status:s limit:u':'SessionsRoute RunsRoute TasksRoute ApprovalsRoute ArtifactsRoute',
 'after:u topics:s project:s session:s':'LiveRoute',
 'after:u':'ApplicationRoute',
 'state:s code:s error:s':'McpAuthRoute',
 'project:s path:s scope:s':'ExtensionsRoute',
}
QUERY={handler:spec for spec,handlers in QUERY_GROUPS.items() for handler in handlers.split()}

def attach_defs(schema):
    """Include only referenced definitions; each describe result stands alone."""
    needed={}
    def visit(value):
        if isinstance(value,dict):
            name=value.get('$ref','').removeprefix('#/$defs/')
            if name:
                if name not in DEFS:raise ValueError('Missing product contract definition: '+name)
                if name not in needed:needed[name]=deepcopy(DEFS[name]);visit(needed[name])
            for key,child in value.items():
                if key!='$ref':visit(child)
        elif isinstance(value,list):
            for child in value:visit(child)
    visit(schema)
    return {**schema,'$defs':needed} if needed else schema

def contract(row):
    handler=row['handler'].removeprefix('MdoApi');method=row['method'];path=row['path']
    if handler not in BODY and handler not in NO_BODY and handler!='BackupUploadChunkRoute':
        raise ValueError('Product handler has no SDK contract: '+row['handler'])
    captures=re.findall(r'\{(\w+)\}',path)
    path_schema=obj({key:{**S,'minLength':1,'description':'Resource identity'+(' (may inherit invocation context)' if key in ('project','session','run') else '')} for key in captures},[key for key in captures if key not in ('project','session','run')],True)
    properties={'path':path_schema,'query':obj(fields(QUERY.get(handler,'')),closed=True),
        'revision':{**S,'description':'Strong ETag returned by the previous read; required by revisioned mutations'},
        'headers':{'type':'object','additionalProperties':S,'description':'Optional product headers; write token, Host and Origin are host-owned'}}
    required=[];body_name=BODY.get(handler)
    if handler=='AccountRoute':body_name='AccountCallback' if path.endswith('/callback') else 'AccountLogin' if path.endswith('/login') else None
    if handler=='AccountRoute' and method=='POST' and not body_name:body_name='AccountSkip' if path.endswith('/search/skip') else 'Empty'
    if handler=='DraftSubmissionRoute':body_name='QueueState'
    if path.endswith('/enabled'):body_name='Enabled'
    if handler=='QueueItemRoute':body_name='QueueState'
    if handler=='RemoteRoute' and path.endswith('/ticket'):body_name=None;properties['body']=document('job_id:s',('job_id',),True)
    if handler=='CliToolsRoute' and method=='POST':properties['body']=document('action:s id:s tool:o enabled:b revision:u store_id:u query:s before:u name:s')
    if handler=='DistributionRoute' and method=='POST':properties['body']=document('action:s channel:s product:s')
    raw=handler in ('BackupUploadChunkRoute','AttachmentRoute') and method=='PUT'
    raw=raw or handler=='AttachmentsRoute' and method=='POST'
    raw=raw or handler=='DeviceTransfersRoute' and method=='PUT'
    if raw:properties['body_base64']={**S,'contentEncoding':'base64','description':'Raw request bytes encoded once as base64'};required.append('body_base64')
    elif body_name and method not in ('GET','DELETE'):
        properties['body']=ref(body_name+'Input')
        if handler!='UpdateRoute':required.append('body')
    if raw and handler in ('AttachmentsRoute','AttachmentRoute'):
        properties['headers']['properties']={'Content-Type':{**S,'description':'Image media type, e.g. image/png'},'X-Mdo-File-Name':{**S,'description':'Optional original filename'}}
    result_name=RESULT.get(handler,'Snapshot')
    if handler not in RESULT and handler.endswith(('ConversationRoute','TurnsRoute','EventsRoute','HistoryRoute','SourcesRoute','PreviewsRoute','RestoresRoute')):result_name='Page'
    output=obj({'status':{**U,'minimum':100,'maximum':599},'data':ref(result_name),'revision':S,'content_type':S,
        'transport':{'type':'string','enum':['native','http-stream']},'base64':{**S,'contentEncoding':'base64'}},('status','revision','content_type','transport'))
    if handler in ('SessionExportRoute','SessionBackupRoute','SessionArtifactRoute','ArtifactRoute','AttachmentRoute') and method=='GET':
        output['properties']['data']=ref('Binary')
    if handler=='ApplicationRoute':
        output['properties']['data']=array(ref('Document')) if path.endswith('/services') else ref('JsonValue')
    if handler=='ExtensionsRoute' and path.endswith('/bundle'):output['properties']['data']=ref('Document')
    return attach_defs(obj(properties,required,True)),attach_defs(output)
