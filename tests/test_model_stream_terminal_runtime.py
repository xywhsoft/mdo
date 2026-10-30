"""Protocol-terminal and connection-isolation regression through the native host.

Only loopback synthetic models; no real accounts or model traffic.
"""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import re
import ssl
from pathlib import Path
import tempfile
import threading
import time

from test_model_runtime import ROOT, run_probe, write_site

PROBE = r'''
#include <xsbase.h>
#include <xllm.h>
#include <xllm-time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void ServiceInit(XS_HostInfo* H) {
    (void)H;
    xllm_client_config C; xllmClientConfigInit(&C);
    C.sBaseUrl=getenv("TERMINAL_URL"); C.sModel="terminal-fixture";
    C.eProvider=(xllm_provider)atoi(getenv("TERMINAL_PROVIDER"));
    C.sCaPem=getenv("TERMINAL_CA_PEM");
    C.uTimeoutMs=6000; C.uMaxAttempts=1;
    xllm_error E; xllm_client* Client=xllmClientCreate(&C,&E);
    if(!Client){printf("init_failed=%s\nprobe_done=1\n",E.sMessage);return;}
    for(int I=0;I<2;I++) {
        xllm_request Q; xllmRequestInit(&Q);
        xllmRequestAddTextMessage(&Q,XLLM_ROLE_USER,I?"second":"first");
        xllm_response* R=NULL; uint64 Begin=xllmClockUs(); xllm_result S;
        if(!I && !strcmp(getenv("TERMINAL_MODE"),"cancel")) {
            xllm_call* Call=xllmClientStart(Client,&Q,NULL,&E);
            if(!Call) {printf("start_failed=1\n");break;}
            xrtSleep(150); xllmCallCancel(Call);
            S=xllmCallWait(Call,&R,&E);xllmCallDestroy(Call);
        } else S=xllmClientComplete(Client,&Q,NULL,&R,&E);
        printf("call%d=result:%d code:%s ms:%llu input:%llu output:%llu finish:%d reused:%d text:%s\n",I,
            (int)S,xllmErrorCodeName(E.eCode),(unsigned long long)((xllmClockUs()-Begin)/1000),
            R?(unsigned long long)R->tUsage.uInputTokens:0,
            R?(unsigned long long)R->tUsage.uOutputTokens:0,R?(int)R->eFinish:-1,
            R?R->tStats.bReusedConnection:0,R&&R->sContent?R->sContent:"");
        if(R)xllmResponseDestroy(R);xllmRequestUnit(&Q);
    }
    xllmClientDestroy(Client);printf("probe_done=1\n");
}
'''


def events(mode, provider):
    if provider == 0:
        first = {"id":"p","choices":[{"index":0,"delta":{"content":"first"},"finish_reason":None}]}
        finish = {"id":"p","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}
        usage = {"id":"p","choices":[],"usage":{"prompt_tokens":20,"completion_tokens":3,"total_tokens":23}}
        return [first, finish, usage, "[DONE]"]
    if provider == 2:
        return [
            {"type":"response.output_text.delta","delta":"first"},
            {"type":"response.incomplete" if mode=="incomplete" else "response.completed",
             "response":{"incomplete_details":{"reason":"max_output_tokens"},
                         "usage":{"input_tokens":20,"output_tokens":3,"total_tokens":23}}},
        ]
    return [
        {"type":"message_start","message":{"id":"p","usage":{"input_tokens":20}}},
        {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}},
        {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"first"}},
        {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":3}},
        {"type":"message_stop"},
    ]


def check(host, record=None, modes=None):
    results = []
    for mode, provider in (("chat",0),("responses",2),("anthropic",3),("incomplete",2),
                           ("failed",2),("anthropic-error",3),("truncated",0),
                           ("malformed",0),("late-usage",0),("cancel",0),("reuse",0),("tls-tail",0)):
        if modes and mode not in modes: continue
        calls = []; released = threading.Event()
        class Model(BaseHTTPRequestHandler):
            protocol_version="HTTP/1.1"
            def log_message(self,*_): pass
            def handle(self):
                try: super().handle()
                except (ConnectionError,OSError): pass
            def do_POST(self):
                self.rfile.read(int(self.headers["Content-Length"]))
                calls.append(self.client_address[1]); first=len(calls)==1
                self.send_response(200);self.send_header("Content-Type","text/event-stream")
                self.send_header("Transfer-Encoding","chunked");self.end_headers()
                buffered=bytearray()
                def chunk(value):
                    data=("data: "+(json.dumps(value) if isinstance(value,dict) else value)+"\n\n").encode()
                    framed=f"{len(data):x}\r\n".encode()+data+b"\r\n"
                    if mode=="reuse": buffered.extend(framed)
                    else: self.wfile.write(framed);self.wfile.flush()
                try:
                    if first and mode in ("failed","anthropic-error"):
                        chunk({"type":"response.failed","response":{"error":{"message":"fixture failure","code":"fixture"}}}
                              if provider==2 else {"type":"error","error":{"type":"overloaded_error","message":"fixture failure"}})
                    elif first and mode=="malformed": chunk("{invalid json")
                    else:
                        stream=events(mode,provider)
                        if first and mode in ("truncated","cancel"):stream=stream[:1]
                        for index, item in enumerate(stream):
                            if first and mode=="late-usage" and index==2:time.sleep(.3)
                            if not first:
                                item=json.loads(json.dumps(item).replace('"first"','"second"')) if isinstance(item,dict) else item
                            chunk(item)
                    if first and mode not in ("truncated","reuse"):released.wait(3)
                    self.wfile.write(bytes(buffered)+b"0\r\n\r\n");self.wfile.flush()
                except (ConnectionError,OSError): pass
        server=ThreadingHTTPServer(("127.0.0.1",0),Model)
        tls=mode=="tls-tail"
        if tls:
            context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(ROOT/"tests/fixtures/proxy-localhost-cert.pem",ROOT/"tests/fixtures/proxy-localhost-key.pem")
            server.socket=context.wrap_socket(server.socket,server_side=True)
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            with tempfile.TemporaryDirectory(prefix="model-terminal-",dir=ROOT/".build") as raw:
                base=Path(raw);write_site(base/"site")
                (base/"site/probe.c").write_text(PROBE,encoding="utf-8")
                output=run_probe(host,base/"site",base/"home",{
                    "TERMINAL_URL":f'{"https" if tls else "http"}://127.0.0.1:{server.server_port}/v1',
                    "TERMINAL_PROVIDER":str(provider),"TERMINAL_MODE":mode,
                    **({"TERMINAL_CA_PEM":(ROOT/"tests/fixtures/proxy-localhost-ca.pem").read_text()} if tls else {})},timeout_seconds=12)
                rows=re.findall(r"call(\d)=result:(-?\d+) code:(\S+) ms:(\d+) input:(\d+) output:(\d+) finish:(-?\d+) reused:(\d+) text:(.*)",output)
                assert len(rows)==2 and "probe_done=1" in output,(mode,output)
                a,b=rows
                assert b[1]=="0" and b[-1]=="second",(mode,output)
                assert int(a[3])<2000,(mode,"waited for HTTP tail",output)
                if mode in ("failed","anthropic-error","truncated","malformed","cancel"):
                    assert a[1]!="0",(mode,output)
                    if mode=="cancel":assert "cancel" in a[2],output
                    if mode=="truncated":assert a[2]=="incomplete_response",output
                else:
                    assert a[1]=="0" and a[-1]=="first",(mode,output)
                    assert a[4:6]==("20","3"),(mode,output)
                    if mode=="incomplete":assert a[6]=="1",output
                if mode not in ("truncated","reuse"):
                    assert len(calls)==2 and calls[0]!=calls[1],(mode,"undrained connection reused",calls)
                if mode=="late-usage":assert int(a[3])>=250,output
                if mode=="reuse":assert calls[0]==calls[1] and b[7]=="1",(mode,output,calls)
                results.append({"mode":mode,"first_ms":int(a[3]),"first_code":a[2],"second_ms":int(b[3]),"connection_ports":calls})
                print(f"{mode}: PASS ({a[3]}ms, {a[2]})",flush=True)
        finally:
            released.set();server.shutdown();server.server_close()
    if record:record.write_text(json.dumps(results,indent=2)+"\n",encoding="utf-8")
    return results


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--host",type=Path,required=True)
    parser.add_argument("--record",type=Path);parser.add_argument("--case",action="append")
    args=parser.parse_args();check(args.host.resolve(),args.record,args.case)
