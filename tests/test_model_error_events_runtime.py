"""Model error metadata replay, legacy records and malformed record rejection."""
import argparse
from pathlib import Path
import tempfile

from test_session_runtime import ROOT, PROBE_SOURCE, run_probe, write_site

PROBE = PROBE_SOURCE.split('typedef struct Probe')[0] + r'''
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;
    MdoSessionEventBridge Bridge={0};
    snprintf(Bridge.ProjectId,sizeof(Bridge.ProjectId),"default");
    snprintf(Bridge.SessionId,sizeof(Bridge.SessionId),"0123456789abcdef0123456789abcdef");
    xwork_event Event={0};
    Event.eKind=XWORK_EVENT_ERROR; Event.uEventId=1; Event.uRunId=2;
    Event.eModelErrorCode=XLLM_ERROR_RATE_LIMIT; Event.uHttpStatus=429;
    Event.sProviderCode="daily_token_limit"; Event.sText="specific final error";
    Event.iTextLength=strlen(Event.sText); Event.tDiagnostics.uAttemptCount=1;
    size_t Size=0; char* Json=MdoEventsRecord(&Bridge,1,&Event,"",0,&Size);
    MdoSessionEventOwned Parsed={0};
    bool Good=Json && MdoEventsParse(Bridge.ProjectId,Bridge.SessionId,xrtStrViewN(Json,Size),&Parsed);
    printf("current=%u schema=%u kind=%s status=%u attempts=%u\n",Good,
        Parsed.Info.SchemaVersion,Parsed.Info.ModelErrorKind,
        Parsed.Info.ModelHttpStatus,Parsed.Info.ModelAttempts);
    MdoEventsOwnedUnit(&Parsed);
    xvalue* Root=xrtJsonParse(xrtStrViewN(Json,Size)); xrtFree(Json);
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("schema_version"),xrtValueUInt(5));
    xrtValueObjectRemove(Root,XRT_STR_LITERAL("model_error_kind"));
    xrtValueObjectRemove(Root,XRT_STR_LITERAL("model_http_status"));
    xrtValueObjectRemove(Root,XRT_STR_LITERAL("model_attempts"));
    Json=xrtJsonStringify(Root,false,&Size);
    Good=MdoEventsParse(Bridge.ProjectId,Bridge.SessionId,xrtStrViewN(Json,Size),&Parsed);
    printf("legacy=%u schema=%u empty_kind=%u\n",Good,Parsed.Info.SchemaVersion,
        Parsed.Info.ModelErrorKind[0]==0);
    MdoEventsOwnedUnit(&Parsed); xrtFree(Json);
    xrtValueObjectSetNew(Root,XRT_STR_LITERAL("schema_version"),xrtValueUInt(6));
    Json=xrtJsonStringify(Root,false,&Size);
    printf("missing_error_metadata_rejected=%u\n",!MdoEventsParse(Bridge.ProjectId,Bridge.SessionId,xrtStrViewN(Json,Size),&Parsed));
    MdoEventsOwnedUnit(&Parsed); xrtFree(Json); xrtValueRelease(Root);
    Event.eKind=XWORK_EVENT_MODEL_START; Event.eModelErrorCode=XLLM_ERROR_NONE;
    Json=MdoEventsRecord(&Bridge,2,&Event,"",0,&Size);
    Good=MdoEventsParse(Bridge.ProjectId,Bridge.SessionId,xrtStrViewN(Json,Size),&Parsed);
    printf("ordinary=%u empty_kind=%u\n",Good,Parsed.Info.ModelErrorKind[0]==0);
    MdoEventsOwnedUnit(&Parsed); xrtFree(Json);
    printf("probe_done=1\n");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    host = parser.parse_args().host.resolve()
    with tempfile.TemporaryDirectory(prefix='model-error-events-', dir=ROOT / '.build') as raw:
        base = Path(raw)
        site = base / 'site'
        write_site(site)
        (site / 'probe.c').write_text(PROBE, encoding='utf-8')
        output = run_probe(host, site, base / 'home')
        for expected in ('current=1 schema=6 kind=daily_token_limit status=429 attempts=1',
                         'legacy=1 schema=5 empty_kind=1',
                         'missing_error_metadata_rejected=1', 'ordinary=1 empty_kind=1'):
            assert expected in output, output
    print('Structured model error replay, old event compatibility and strict validation: PASS')


if __name__ == '__main__':
    main()
