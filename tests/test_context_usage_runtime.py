"""Native usage-anchored governance, recovery and actual message growth."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
PROBE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>
#include <xllm.h>
#include <xllm-session.h>
#define CHECK(x) do { if(!(x)){printf("FAIL line=%u %s\n",(unsigned)__LINE__,#x);goto done;} } while(0)
void ServiceInit(XS_HostInfo* host) {
    xllm_model_profile profile; xllmModelProfileInit(&profile);
    xllm_session_config config;
    xllm_error error; xllmErrorInit(&error);
    xllm_session *session=NULL,*loaded=NULL,*recovered=NULL,*restored=NULL,*tools=NULL,*fork=NULL;
    xllm_session_stats stats;
    xllm_response response={0};
    char *text=NULL,*snapshot=NULL,*journal=NULL;
    size_t snapshot_size=0,journal_size=0;
    uint64_t turn;
    (void)host;
    profile.sId="context-fixture";profile.sModel="context-fixture";
    profile.eWindowMode=XLLM_WINDOW_SHARED_CONTEXT;
    profile.uCapabilities=XLLM_CAP_TEXT_IN|XLLM_CAP_TEXT_OUT|XLLM_CAP_STREAM|XLLM_CAP_TOOL_CALL_OUT|XLLM_CAP_TOOL_RESULT_IN;
    profile.uContextWindowTokens=100000;profile.uMaxInputTokens=100000;profile.uMaxOutputTokens=10000;
    CHECK(xllmSessionConfigInitFromProfile(&config,&profile,&error));
    config.uOutputReserveTokens=10000;config.uSafetyReserveTokens=3000;
    config.uUserMessageCapBytes=0;config.uToolResultCapBytes=16384;config.uToolResultTotalCapBytes=65536;
    session=xllmSessionCreateForProfile(&config,&profile,&error);CHECK(session);
    CHECK(xllmSessionEnableJournal(session,"journal.jsonl",&error));
    CHECK(xllmSessionSave(session,"before.json",&error));
    CHECK(xllmSessionSetSystemPrompt(session,"System fixture",&error));
    turn=xllmSessionBeginTurn(session);CHECK(turn);
    CHECK(xllmSessionAddText(session,turn,XLLM_ROLE_USER,"seed",0));
    response.sContent="seed answer";response.tUsage.uInputTokens=60000;response.tUsage.uOutputTokens=1000;
    CHECK(xllmSessionAddAssistantResponse(session,turn,&response));
    CHECK(xllmSessionGetStats(session,&stats));
    CHECK(stats.bFillExactValid && stats.uFillExact==61000 && stats.uIncrementMax==0);
    CHECK(stats.ePressure<XLLM_SESSION_PRESSURE_COMPACT);
    { xllm_usage incomplete={0};incomplete.uTotalTokens=500;
      CHECK(xllmSessionRecordUsage(session,&incomplete));
      CHECK(xllmSessionGetStats(session,&stats));CHECK(stats.uFillExact==61000); }
    text=xrtMalloc(120001);CHECK(text);
    for(size_t i=0;i<120000;i++)text[i]="word "[i%5];text[120000]=0;
    turn=xllmSessionBeginTurn(session);CHECK(turn);
    CHECK(xllmSessionAddText(session,turn,XLLM_ROLE_USER,text,0));
    CHECK(xllmSessionGetStats(session,&stats));
    CHECK(stats.uFillExact==61000 && stats.uIncrementMax==30012);
    CHECK(stats.ePressure==XLLM_SESSION_PRESSURE_OVERFLOW && stats.uInputBudgetTokens==87000);
    CHECK(stats.uNextMaxOutputTokens==5988);
    CHECK(xllmSessionSave(session,"after.json",&error));
    loaded=xllmSessionLoad("after.json",&error);CHECK(loaded);
    CHECK(xllmSessionGetStats(loaded,&stats));
    CHECK(stats.bFillExactValid && stats.uFillExact==61000 && stats.uIncrementMax==30012);
    CHECK(xllmSessionApplyProfile(loaded,&config,&profile,&error));
    CHECK(xllmSessionGetStats(loaded,&stats));
    CHECK(stats.bFillExactValid && stats.ePressure==XLLM_SESSION_PRESSURE_OVERFLOW);
    recovered=xllmSessionRecover("before.json","journal.jsonl",&config,&error);CHECK(recovered);
    CHECK(xllmSessionApplyProfile(recovered,&config,&profile,&error));
    CHECK(xllmSessionGetStats(recovered,&stats));
    CHECK(stats.bFillExactValid && stats.uIncrementMax==30012 && stats.ePressure==XLLM_SESSION_PRESSURE_OVERFLOW);
    snapshot=xrtFileReadAll("before.json",&snapshot_size);journal=xrtFileReadAll("journal.jsonl",&journal_size);
    CHECK(snapshot && journal);
    restored=xllmSessionRestore(snapshot,snapshot_size,journal,journal_size,NULL,&error);CHECK(restored);
    CHECK(xllmSessionGetStats(restored,&stats));
    CHECK(stats.bFillExactValid && stats.ePressure==XLLM_SESSION_PRESSURE_OVERFLOW);
    fork=xllmSessionForkAt(restored,xllmSessionLastSequence(restored),&error);CHECK(fork);
    CHECK(xllmSessionGetStats(fork,&stats));CHECK(stats.bFillExactValid && stats.uIncrementMax==30012);
    profile.sId="other-tokenizer";config.uContextWindowTokens=40000;config.uMaxInputTokens=40000;
    CHECK(xllmSessionApplyProfile(loaded,&config,&profile,&error));
    CHECK(xllmSessionGetStats(loaded,&stats));
    CHECK(!stats.bFillExactValid && stats.ePressure==XLLM_SESSION_PRESSURE_OVERFLOW);
    CHECK(xllmSessionClear(loaded,&error));
    CHECK(xllmSessionGetStats(loaded,&stats));
    CHECK(!stats.bFillExactValid && stats.uRenderedActiveTokens==0 && stats.ePressure==XLLM_SESSION_PRESSURE_NONE);

    profile.sId="context-fixture";config.uContextWindowTokens=100000;config.uMaxInputTokens=100000;
    tools=xllmSessionCreateForProfile(&config,&profile,&error);CHECK(tools);
    turn=xllmSessionBeginTurn(tools);CHECK(xllmSessionAddText(tools,turn,XLLM_ROLE_USER,"read",0));
    xllm_tool_call call={0};call.sId="tool1";call.sName="read";call.sArgumentsJson="{}";
    response.sContent="";response.pToolCalls=&call;response.iToolCallCount=1;
    response.tUsage.uInputTokens=80000;
    CHECK(xllmSessionAddAssistantResponse(tools,turn,&response));
    text[16000]=0;
    CHECK(xllmSessionAddToolResult(tools,turn,"tool1",text));
    CHECK(xllmSessionGetStats(tools,&stats));
    CHECK(stats.uIncrementMax>=4012 && stats.ePressure>=XLLM_SESSION_PRESSURE_COMPACT);
    response.sContent="done";response.pToolCalls=NULL;response.iToolCallCount=0;
    response.tUsage.uInputTokens=65000;response.tUsage.uOutputTokens=50;
    CHECK(xllmSessionAddAssistantResponse(tools,turn,&response));
    CHECK(xllmSessionGetStats(tools,&stats));CHECK(stats.uFillExact==65050 && stats.uIncrementMax==0);
    CHECK(xllmSessionSetSystemPrompt(tools,"Changed system context",&error));
    CHECK(xllmSessionGetStats(tools,&stats));CHECK(stats.uIncrementMax>0);
    xllm_part parts[2]={{0},{0}};
    parts[0].eKind=XLLM_PART_TEXT;parts[0].sText="中文🙂";parts[1].eKind=XLLM_PART_IMAGE;
    xllm_message message={0};message.eRole=XLLM_ROLE_USER;message.pParts=parts;message.iPartCount=2;
    CHECK(xllmEstimateMessageTokens(&message)==4111);
    printf("PASS usage anchor, actual user/tool growth, output reserve, checkpoint/journal/byte recovery, profile switch, fork, clear and structured input\n");
done:
    xrtFree(text);xrtFree(snapshot);xrtFree(journal);
    xllmSessionDestroy(fork);xllmSessionDestroy(tools);xllmSessionDestroy(restored);
    xllmSessionDestroy(recovered);xllmSessionDestroy(loaded);xllmSessionDestroy(session);
    printf("probe_done=1\n");fflush(stdout);
}
void ServiceUnit(XS_HostInfo* host){(void)host;}
'''

def run(host):
    (ROOT / '.build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='context-usage-', dir=ROOT / '.build') as raw:
        site = Path(raw)
        (site / 'web').mkdir()
        (site / 'probe.c').write_text(PROBE, encoding='utf-8')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        config = {'services': [dict(enabled=True, name='context-usage', **{'class': 'http'},
            ip='127.0.0.1', port=port, host_default=dict(enabled=True, name='fixture',
            path='web', devlang='c', devfile='probe.c'))]}
        (site / 'xs.json').write_text(json.dumps(config), encoding='utf-8')
        process = subprocess.Popen([str(host.resolve()), 'xs.json'], cwd=site,
            env=dict(os.environ, USE_WEBVIEW='0'), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding='utf-8', errors='replace',
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        lines = []
        done = threading.Event()
        def read():
            for line in process.stdout:
                lines.append(line)
                if 'probe_done=1' in line or 'error:' in line: done.set()
        reader = threading.Thread(target=read, daemon=True)
        reader.start()
        try:
            assert done.wait(25), 'context probe timed out'
        finally:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
            reader.join(timeout=3)
        output = ''.join(lines)
        assert 'FAIL ' not in output and 'PASS usage anchor' in output, output[-6000:]
        print(next(line.strip() for line in lines if 'PASS usage anchor' in line))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host' / ('xs.exe' if os.name == 'nt' else 'xs'))
    run(parser.parse_args().host)
