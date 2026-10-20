"""Bounded media projection and interrupted image-tool recovery, offline."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import tempfile

from test_agent_runtime import ROOT, PROBE_SOURCE, run_probe, write_site


PROBE = PROBE_SOURCE.split("typedef struct ProbeOwner")[0] + r'''
static unsigned Failures, Calls;
static bool ExpectImage;
#define CHECK(expr, name) do { bool ok=!!(expr); printf("%s=%u\n",name,ok); if(!ok)++Failures; } while(0)

static xllm_result Complete(void* data, const xllm_request* request,
    const xllm_stream_callbacks* callbacks, xllm_response** response,
    xllm_error* error)
{
    xllm_model_profile profile;
    size_t i,j,images=0,notices=0;
    bool paired=false;
    (void)data; (void)callbacks;
    ++Calls;
    for(i=0;i<request->iMessageCount;i++) {
        const xllm_message* m=&request->pMessages[i];
        for(j=0;j<m->iPartCount;j++) if(m->pParts[j].eKind==XLLM_PART_IMAGE)++images;
        if(m->sContent && strstr(m->sContent,"This model cannot view images")) {
            ++notices; paired=m->eRole==XLLM_ROLE_TOOL && m->sToolCallId;
        }
    }
    CHECK(ExpectImage ? images>0 && notices==0 : images==0 && notices>0 && paired,
        "callback_media");
    xllmModelProfileInit(&profile);
    profile.sId=request->sModel; profile.sModel=request->sModel;
    profile.eProvider=XLLM_PROVIDER_OPENAI_RESPONSES;
    profile.uCapabilities=511u|(ExpectImage?XLLM_CAP_IMAGE_IN:0u);
    profile.eWindowMode=XLLM_WINDOW_SHARED_CONTEXT;
    profile.uContextWindowTokens=1000000u;profile.uMaxInputTokens=1000000u;
    profile.uMaxOutputTokens=131072u;
    CHECK(xllmModelProfileValidateRequest(&profile,request,error),"callback_valid");
    *response=(xllm_response*)calloc(1,sizeof(**response));
    if(!*response)return XLLM_RESULT_ERROR;
    (*response)->sContent=(char*)malloc(9u);
    if(!(*response)->sContent){xllmResponseDestroy(*response);*response=NULL;return XLLM_RESULT_ERROR;}
    memcpy((*response)->sContent,"media-ok",9u);
    (*response)->eFinish=XLLM_FINISH_STOP;(*response)->uHttpStatus=200u;
    return XLLM_RESULT_OK;
}

static xllm_session* Fixture(void)
{
    static const unsigned char bytes[]={0,255,65,0};
    xllm_error error; xllm_response response={0}; xllm_tool_call call={0};
    xllm_session* session=xllmSessionCreate(NULL,&error);
    uint64_t turn;
    if(!session)return NULL;
    turn=xllmSessionBeginTurn(session);
    call.sId="image-call";call.sName="read";call.sArgumentsJson="{\"path\":\"image.png\"}";
    response.pToolCalls=&call;response.iToolCallCount=1u;
    if(!xllmSessionAddText(session,turn,XLLM_ROLE_USER,"inspect",0u) ||
       !xllmSessionBeginModelCall(session,&error) ||
       !xllmSessionAddAssistantResponse(session,turn,&response) ||
       !xllmSessionAddToolResultWithImage(session,turn,call.sId,
           "image.png: PNG result",bytes,sizeof(bytes),"image/png")) {
        xllmSessionDestroy(session);return NULL;
    }
    return session;
}

static void Projection(xllm_session* session, const char* label)
{
    xllm_request request,storage,view;const xllm_request* projected;
    xllm_model_profile profile; xllm_error error;
    xllm_response* response=NULL;
    MdoAgentOwner owner={0}; MdoAgentRoute routes[2]={{0}};
    const xllm_message* last;
    xcancel* cancel=xrtCancelCreate();
    xllmRequestInit(&request);
    CHECK(xllmSessionBuildRequest(session,&request,&error),"render");
    if(!request.iMessageCount)return;
    last=NULL;
    {size_t i;for(i=0;i<request.iMessageCount;i++)
        if(MdoAgentsMessageHasImage(&request.pMessages[i]))last=&request.pMessages[i];}
    CHECK(last!=NULL,"image_present");
    if(!last){xllmRequestUnit(&request);xrtCancelDestroy(cancel);return;}
    owner.Models=MdoModelCatalogSnapshot();owner.Routes=routes;owner.RouteCount=2u;
    owner.ExternalComplete=Complete;
    routes[0].ModelId="ornith-1.5-35b";routes[0].WireModel="ornith-1.5-35b";
    routes[1].ModelId="media-vision";routes[1].WireModel="media-vision";
    routes[0].Protocol=routes[1].Protocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
    routes[0].ReasoningEffort=routes[1].ReasoningEffort="medium";
    request.pCancel=cancel;request.uDeadline=MdoDeadlineAfterUs(5000000u);
    request.pStablePrefixOwner=session;request.uStablePrefixStamp=7u;
    request.iStableMessages=request.iMessageCount;
    CHECK(MdoModelCatalogProfile(owner.Models,routes[0].ModelId,routes[0].Protocol,&profile,&error),"profile");
    CHECK(MdoAgentsProjectImages(&request,&profile,&storage,&view,&projected,&error) &&
        projected!=&request && projected->pCancel==cancel &&
        projected->uDeadline==request.uDeadline && !projected->pStablePrefixOwner &&
        !projected->iStableMessages && projected->iMessageCount==request.iMessageCount &&
        projected->pMessages[0].sContent==request.pMessages[0].sContent &&
        projected->pTools==request.pTools,"borrowed_scope_and_cache");
    if(projected!=&request) {
        const xllm_message* text=&projected->pMessages[last-request.pMessages];
        CHECK(text->sContent && strstr(text->sContent,"image/png") &&
            !text->iPartCount && text->sToolCallId && last->sToolCallId &&
            !strcmp(text->sToolCallId,last->sToolCallId),"tool_pair_and_text_shape");
        if(!strcmp(label,"fixture"))CHECK(strstr(text->sContent,"image.png: PNG result"),"tool_description_retained");
    }
    xllmRequestUnit(&storage);
    CHECK(MdoAgentsMessageHasImage(last),"ledger_untouched");
    CHECK(xllmRequestSetModel(&request,"ornith-1.5-35b"),"text_model");
    ExpectImage=false;
    CHECK(MdoAgentsComplete(&owner,&request,NULL,&response,&error)==XLLM_RESULT_OK,"text_route");
    xllmResponseDestroy(response);response=NULL;
    CHECK(xllmRequestSetModel(&request,"media-vision"),"vision_model");
    ExpectImage=true;
    CHECK(MdoAgentsComplete(&owner,&request,NULL,&response,&error)==XLLM_RESULT_OK,"vision_route");
    xllmResponseDestroy(response);response=NULL;
    CHECK(MdoAgentsMessageHasImage(last),"switch_preserves_image");
    CHECK(xllmRequestSetModel(&request,"ornith-1.5-35b"),"reject_model");
    {
        xllm_message message; xllm_part part={0};unsigned before=Calls;
        xllmMessageInit(&message,XLLM_ROLE_USER);
        part.eKind=XLLM_PART_IMAGE;part.sSourceUrl="https://example.invalid/user.png";
        CHECK(xllmMessageAddPart(&message,&part) && xllmRequestAddMessage(&request,&message),"user_fixture");
        xllmMessageUnit(&message);
        CHECK(MdoAgentsComplete(&owner,&request,NULL,&response,&error)==XLLM_RESULT_ERROR &&
            Calls==before && !response && !strcmp(MdoModelErrorKind(&error),"profile_image_unsupported"),"user_image_rejected");
    }
    MdoModelCatalogRelease(owner.Models);xllmRequestUnit(&request);xrtCancelDestroy(cancel);
}

static void Resume(xwork_runtime* runtime, const char* path, const char* label)
{
    MdoAgentSessionOptions options;MdoAgentRunOptions run_options;
    MdoAgentSession* session;MdoAgentRun* run=NULL;
    xwork_error error;xwork_run_result result={0};unsigned before=Calls;
    xllm_session* loaded;
    xllm_error model_error;
    char journal[128];
    MdoAgentSessionOptionsInit(&options);options.WorkspaceRoot=".";
    options.SessionPath=path;options.Recover=true;options.OnModelComplete=Complete;
    snprintf(journal,sizeof(journal),"%s.journal",path);options.JournalPath=journal;
    options.Protocol=MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS;
    session=MdoAgentSessionCreateWithRuntime(runtime,&options,&error);
    CHECK(session!=NULL,"resume_session");
    if(!session){printf("resume_error=%s\n",error.sMessage);return;}
    ExpectImage=false;MdoAgentRunOptionsInit(&run_options);run_options.Resume=true;
    run=MdoAgentRunCreate(session,&run_options,&error);
    CHECK(run && MdoAgentRunStart(run,&error) &&
        MdoAgentRunWait(run,MdoDeadlineAfterUs(5000000u),&result,&error)==XWORK_RESULT_OK &&
        result.sFinalText && !strcmp(result.sFinalText,"media-ok") &&
        Calls==before+1u,"resume_without_tool_replay");
    if(Failures)printf("run_error=%s\n",error.sMessage);
    xworkRunResultUnit(&result);
    MdoAgentRunDestroy(run);MdoAgentSessionRelease(session);
    loaded=xllmSessionLoad(path,&model_error);
    CHECK(loaded!=NULL,"checkpoint_reload");
    if(loaded){
        xllm_request request;size_t i,images=0;
        xllmRequestInit(&request);
        CHECK(xllmSessionBuildRequest(loaded,&request,&model_error),"checkpoint_render");
        for(i=0;i<request.iMessageCount;i++)if(MdoAgentsMessageHasImage(&request.pMessages[i]))++images;
        CHECK(images>0,"checkpoint_keeps_original_image");
        xllmRequestUnit(&request);xllmSessionDestroy(loaded);
    }
    printf("resume_label=%s\n",label);
}

void ServiceInit(XS_HostInfo* host)
{
    xwork_runtime_config config;xwork_runtime* runtime=NULL;
    xwork_error error;xllm_error model_error;xllm_session* session=NULL;
    (void)host;
    CHECK(MdoHomeInit() && MdoProjectLifecycleInit() && MdoConfigInit() &&
        MdoModelManagerInit() && MdoSkillManagerInit(),"init");
    xworkRuntimeConfigInit(&config);runtime=xworkRuntimeCreate(&config,&error);
    CHECK(runtime && MdoMemoryManagerInit(runtime) && MdoModuleManagerInit(runtime),"runtime");
    session=Fixture();CHECK(session!=NULL,"fixture");
    if(session){
        Projection(session,"fixture");
        CHECK(xllmSessionSave(session,"fixture.json",&model_error),"fixture_save");
        xllmSessionDestroy(session);session=NULL;
        Resume(runtime,"fixture.json","fixture");
    }
    if(xrtFileExists("snapshot-copy.json")) {
        session=xllmSessionLoad("snapshot-copy.json",&model_error);
        CHECK(session!=NULL,"actual_load");
        if(session && ACTUAL_THROUGH != 0u) {
            xllm_session* failed=xllmSessionForkAt(session,ACTUAL_THROUGH,&model_error);
            CHECK(failed && xllmSessionSave(failed,"snapshot-copy.json",&model_error),"failed_prefix_copy");
            xllmSessionDestroy(session);session=failed;
        }
        if(session){Projection(session,"actual");xllmSessionDestroy(session);session=NULL;}
        Resume(runtime,"snapshot-copy.json","actual");
    }
    MdoModuleManagerUnit();MdoMemoryManagerUnit();MdoSkillManagerUnit();
    MdoModelManagerUnit();MdoConfigUnit();MdoProjectLifecycleUnit();MdoHomeUnit();
    xworkRuntimeRelease(runtime);
    printf("failures=%u\nprobe_done=1\n",Failures);fflush(stdout);
}
void ServiceUnit(XS_HostInfo* host){(void)host;}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    parser.add_argument("--snapshot", type=Path, help="Read only; test a temporary copy")
    parser.add_argument("--through-sequence", type=int, default=0,
                        help="Reconstruct the interrupted boundary on the temporary copy")
    args = parser.parse_args()
    original = args.snapshot.read_bytes() if args.snapshot else None
    with tempfile.TemporaryDirectory(prefix="agent-media-", dir=ROOT / ".build") as raw:
        base = Path(raw);site = base / "site"
        write_site(site)
        path = site / "default-home/config/defaults.json"
        defaults = json.loads(path.read_text(encoding="utf-8"))
        vision = copy.deepcopy(defaults["models"]["items"][0])
        vision.update(id="media-vision", name="Media Vision", wire_model="media-vision",
                      builtin=False, editable=True, removable=True)
        defaults["models"]["items"].append(vision)
        text = defaults["models"]["items"][0]
        text["capabilities"].remove("media-input");text["attachments"] = []
        # The real failed GLM history uses a million-token profile.
        for item in defaults["models"]["items"]:
            item["window"].update(context_tokens=1000000, max_input_tokens=1000000,
                                  max_output_tokens=131072, output_reserve_tokens=131072)
        path.write_text(json.dumps(defaults), encoding="utf-8")
        (site / "probe.c").write_text(PROBE.replace("ACTUAL_THROUGH", str(args.through_sequence) + "u"), encoding="utf-8")
        if original is not None:
            (site / "snapshot-copy.json").write_bytes(original)
        output = run_probe(args.host.resolve(), site, base / "home")
        assert "failures=0" in output and "probe_done=1" in output, output
        assert "resume_label=fixture" in output, output
        if original is not None:
            assert "resume_label=actual" in output, output
    if original is not None:
        assert hashlib.sha256(args.snapshot.read_bytes()).digest() == hashlib.sha256(original).digest()
    print("Agent media projection, model switch, explicit image rejection and offline resume: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
