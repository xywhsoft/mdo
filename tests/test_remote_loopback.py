"""Reuse actual mdo routes through bounded native loopback HTTP.

Functional cases only: write gate, read-only admission, binary/HEAD/chunked
decoding, malformed responses, cancellation and scope restrictions.
"""
from __future__ import annotations
import argparse
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
HOOK = r'''
typedef struct LoopTestReply {
    uint16 Status;
    unsigned Heads;
    char Token[80], Tag[80];
    unsigned char Bytes[65536];
    size_t Size, Chunk;
    bool Abort;
} LoopTestReply;
static xmutex* LoopTestLock;
static xthread* LoopTestThread;
static xcancel* LoopTestCancel;
static xvalue* LoopTestResult;
static bool LoopTestHead(const xhttp1head* head, void* data) {
    LoopTestReply* reply = data; reply->Status = head->Status; reply->Heads++;
    const xhttpfield* token = xrtHttp1Field(head,XRT_STR_LITERAL("X-Mdo-Write-Token"));
    const xhttpfield* tag = xrtHttp1Field(head,XRT_STR_LITERAL("ETag"));
    if (token && token->Value.Size < sizeof(reply->Token)) {
        memcpy(reply->Token,token->Value.Data,token->Value.Size); reply->Token[token->Value.Size] = 0;
    }
    if (tag && tag->Value.Size < sizeof(reply->Tag)) {
        memcpy(reply->Tag,tag->Value.Data,tag->Value.Size); reply->Tag[tag->Value.Size] = 0;
    }
    return true;
}
static bool LoopTestBody(xbytesview bytes, void* data) {
    LoopTestReply* reply = data;
    if (bytes.Size > sizeof(reply->Bytes)-reply->Size) return false;
    memcpy(reply->Bytes+reply->Size,bytes.Data,bytes.Size); reply->Size += bytes.Size;
    if (bytes.Size > reply->Chunk) reply->Chunk = bytes.Size;
    return !reply->Abort;
}
static bool LoopTestResponseRoute(MdoApiContext* c) {
    if (xrtStrEqual(c->Target.Query,XRT_STR_LITERAL("chunked"))) {
        const char text[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n3\r\nABC\r\n2\r\nDE\r\n0\r\n\r\n";
        return MdoApiConnectionSend(c,text,sizeof(text)-1u);
    }
    if (xrtStrEqual(c->Target.Query,XRT_STR_LITERAL("invalid"))) {
        const char text[] = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\nConnection: close\r\n\r\nxx";
        return MdoApiConnectionSend(c,text,sizeof(text)-1u);
    }
    if (xrtStrEqual(c->Target.Query,XRT_STR_LITERAL("truncated"))) {
        const char text[] = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nshort";
        bool ok = MdoApiConnectionSend(c,text,sizeof(text)-1u); xrtNetStreamClose(c->Request->tcp); return ok;
    }
    if (xrtStrEqual(c->Target.Query,XRT_STR_LITERAL("oversize"))) {
        const char text[] = "HTTP/1.1 200 OK\r\nContent-Length: 34603009\r\nConnection: close\r\n\r\n";
        return MdoApiConnectionSend(c,text,sizeof(text)-1u);
    }
    unsigned char bytes[20001]; for (size_t i=0u; i<sizeof(bytes); i++) bytes[i] = (uint8)i;
    return MdoApiReplyRaw(c,200u,bytes,sizeof(bytes),NULL,"\"binary\"","application/octet-stream","attachment; filename=test.bin");
}
static int32 LoopTestRun(void* data) {
    XS_ServerInfo* server = data; xvalue* out = xrtValueObject();
    LoopTestReply reply = {0}; MdoRemoteHttpSink sink = {LoopTestHead,LoopTestBody,&reply};
    MdoRemoteHttpRequest req = {"GET","/api/v1/settings",NULL,0u,{0},false};
    bool ok = MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && reply.Status == 200u && reply.Token[0] && reply.Tag[0];
    MdoAccountSetBool(out,"get",ok);
    char token[80],tag[80]; strcpy(token,reply.Token); strcpy(tag,reply.Tag);
    const char patch[] = "{\"schema_version\":1,\"patch\":{\"agent\":{\"web_search\":false}}}";
    XS_FetchHeader fields[] = {{"Content-Type","application/json"},{"X-Mdo-Write-Token","invalid-token"},{"If-Match",tag}};
    req.Method = "PATCH"; req.Target = "/api/v1/settings/settings"; req.Body = (xbytesview){(const uint8*)patch,sizeof(patch)-1u};
    req.Headers = fields; req.HeaderCount = 1u; memset(&reply,0,sizeof(reply));
    ok = MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && reply.Status == 428u;
    MdoAccountSetBool(out,"missing_nonce",ok);
    req.HeaderCount = 3u; memset(&reply,0,sizeof(reply));
    ok = MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && reply.Status == 412u;
    MdoAccountSetBool(out,"wrong_nonce",ok);
    fields[1].Value = token; req.ReadOnly = true; memset(&reply,0,sizeof(reply));
    ok = !MdoRemoteHttpRequestValid(&req) && !MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && !reply.Heads;
    MdoAccountSetBool(out,"readonly_write",ok);
    req.ReadOnly = false;
    ok = MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && reply.Status == 200u;
    MdoAccountSetBool(out,"write",ok);
    req.Method = "GET"; req.Body = (xbytesview){0}; req.Headers = NULL; req.HeaderCount = 0u;
    const char* invalid[] = {"https://other.test/api/v1/settings","//other.test/api/v1/settings",
        "/admin","/api/v1/../admin","/api/v1/%2e%2e/admin","/api/v1/x\\y","/api/v1/live",
        "/api/v1/connector","/api/v1/connector/ticket","/api/v1/%63onnector/ticket"};
    ok = true;
    for (size_t i=0u; i<sizeof(invalid)/sizeof(invalid[0]); i++) { req.Target = invalid[i]; if (MdoRemoteHttpRequestValid(&req)) ok = false; }
    req.Target = "/api/v1/account/callback?state=fake&code=fake"; req.ReadOnly = true;
    ok = ok && !MdoRemoteHttpRequestValid(&req);
    req.Target = "/api/v1/account/%63allback?state=fake&code=fake"; ok = ok && !MdoRemoteHttpRequestValid(&req);
    req.ReadOnly = false; req.Target = "/api/v1/skills/%E4%B8%AD%20name"; ok = ok && MdoRemoteHttpRequestValid(&req);
    req.Target = "/api/v1/bootstrap";
    XS_FetchHeader denied[] = {{"Host","other.test"},{"Origin","http://other.test"},{"Authorization","Bearer forbidden"},
        {"Content-Length","3"},{"Connection","upgrade"},{"Accept","text/plain\r\nInjected: true"}};
    req.HeaderCount = 1u;
    for (size_t i=0u; i<sizeof(denied)/sizeof(denied[0]); i++) { req.Headers = &denied[i]; if (MdoRemoteHttpRequestValid(&req)) ok = false; }
    XS_FetchHeader duplicate[] = {{"X-Mdo-Write-Token",token},{"x-mdo-write-token",token}};
    req.Headers = duplicate; req.HeaderCount = 2u; ok = ok && !MdoRemoteHttpRequestValid(&req);
    req.Headers = NULL; req.HeaderCount = 0u;
    MdoAccountSetBool(out,"scope_and_headers",ok);
    ok = MdoRemoteHttpResponseField(XRT_STR_LITERAL("X-Mdo-Write-Token")) &&
        MdoRemoteHttpResponseField(XRT_STR_LITERAL("Content-Disposition")) &&
        !MdoRemoteHttpResponseField(XRT_STR_LITERAL("Set-Cookie"));
    MdoAccountSetBool(out,"response_fields",ok);
    req.Target = "/api/v1/test-remote-response"; memset(&reply,0,sizeof(reply));
    ok = MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && reply.Status == 200u && reply.Size == 20001u && reply.Chunk <= 16384u;
    for (size_t i=0u; ok && i<reply.Size; i++) if (reply.Bytes[i] != (uint8)i) ok = false;
    MdoAccountSetBool(out,"binary",ok);
    req.Method = "HEAD"; memset(&reply,0,sizeof(reply));
    ok = MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && reply.Status == 200u && !reply.Size;
    MdoAccountSetBool(out,"head",ok);
    req.Method = "GET"; req.Target = "/api/v1/test-remote-response?chunked"; memset(&reply,0,sizeof(reply));
    ok = MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && reply.Size == 5u && !memcmp(reply.Bytes,"ABCDE",5u);
    MdoAccountSetBool(out,"chunked",ok);
    req.Target = "/api/v1/test-remote-response?invalid"; memset(&reply,0,sizeof(reply));
    ok = !MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && !reply.Heads;
    MdoAccountSetBool(out,"bad_headers",ok);
    req.Target = "/api/v1/test-remote-response?truncated"; memset(&reply,0,sizeof(reply));
    ok = !MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink);
    MdoAccountSetBool(out,"truncated",ok);
    req.Target = "/api/v1/test-remote-response?oversize"; memset(&reply,0,sizeof(reply));
    ok = !MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && !reply.Heads;
    MdoAccountSetBool(out,"body_limit",ok);
    req.Target = "/api/v1/test-remote-response"; memset(&reply,0,sizeof(reply)); reply.Abort = true;
    ok = !MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink);
    MdoAccountSetBool(out,"consumer_abort",ok);
    XS_ServerInfo stale = *server; memset(&reply,0,sizeof(reply));
    ok = !MdoRemoteLoopbackCall(&stale,&req,LoopTestCancel,&sink) && !reply.Heads;
    MdoAccountSetBool(out,"generation",ok);
    xrtCancelRequest(LoopTestCancel); memset(&reply,0,sizeof(reply));
    ok = !MdoRemoteLoopbackCall(server,&req,LoopTestCancel,&sink) && !reply.Heads;
    MdoAccountSetBool(out,"cancel",ok);
    xsServerRelease(server);
    xrtMutexLock(LoopTestLock); LoopTestResult = out; xrtMutexUnlock(LoopTestLock); return 0;
}
static bool LoopTestRoute(MdoApiContext* c) {
    if (!LoopTestLock) LoopTestLock = xrtMutexCreate();
    if (!LoopTestThread) {
        LoopTestCancel = xrtCancelCreate(); XS_ServerInfo* server = xsServerRetain(c->Request->server);
        LoopTestThread = xrtThreadCreate(LoopTestRun,server,0);
        if (!LoopTestThread) xsServerRelease(server);
    }
    xrtMutexLock(LoopTestLock); xvalue* out = xrtValueClone(LoopTestResult); xrtMutexUnlock(LoopTestLock);
    return MdoApiReplySuccessTake(c,200u,out,NULL);
}
static void LoopTestUnit(void) {
    xrtCancelRequest(LoopTestCancel);
    if (LoopTestThread) { xrtThreadWait(LoopTestThread); xrtThreadDestroy(LoopTestThread); }
    xrtCancelDestroy(LoopTestCancel); xrtValueRelease(LoopTestResult); xrtMutexDestroy(LoopTestLock);
}
'''


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); return sock.getsockname()[1]


def run(host):
    (ROOT/'.build').mkdir(exist_ok=True)
    site = Path(tempfile.mkdtemp(prefix='remote-loopback-',dir=ROOT/'.build'))
    shutil.copytree(ROOT/'app',site,dirs_exist_ok=True)
    port = free_port(); config = json.loads((site/'xs.json').read_text()); service = config['services'][0]
    service.update({'class':'http','port':port}); service.pop('window',None)
    (site/'xs.json').write_text(json.dumps(config))
    router = site/'src/api/router.c'; source = router.read_text()
    source = source.replace('static const MdoApiRoute g_MdoApiRoutes[]',HOOK+'\nstatic const MdoApiRoute g_MdoApiRoutes[]')
    source = source.replace('static const MdoApiRoute g_MdoApiRoutes[] = {',
        'static const MdoApiRoute g_MdoApiRoutes[] = {\n'
        ' {"/api/v1/test-loopback",XHTTP_METHOD_GET,"GET",LoopTestRoute,false},\n'
        ' {"/api/v1/test-remote-response",XHTTP_METHOD_GET|XHTTP_METHOD_HEAD,"GET,HEAD",LoopTestResponseRoute,false},')
    router.write_text(source)
    bootstrap = site/'src/bootstrap/service.c'; source = bootstrap.read_text()
    source = source.replace('MdoApiUnit();','LoopTestUnit();\n    MdoApiUnit();'); bootstrap.write_text(source)
    env = dict(os.environ); env['MDO_HOME'] = str(site/'mdo-home')
    with (site/'test.log').open('wb') as log:
        process = subprocess.Popen([str(host),str(site/'xs.json')],cwd=site,env=env,stdout=log,stderr=log)
        try:
            for _ in range(200):
                if process.poll() is not None: raise AssertionError(f'host exited: {site}/test.log')
                connection = http.client.HTTPConnection('127.0.0.1',port,timeout=5)
                try:
                    connection.request('GET','/api/v1/test-loopback'); response = connection.getresponse()
                    if response.status == 200:
                        value = json.loads(response.read())['data']
                        if value: break
                except OSError: pass
                finally: connection.close()
                time.sleep(.05)
            else: raise AssertionError(f'loopback worker did not finish: {site}/test.log')
            assert value and all(value.values()),value
            saved = json.loads((site/'mdo-home/config/settings.json').read_text())
            assert not saved['patch']['agent']['web_search'],saved
            print('PASS actual route reuse, target nonce/preconditions, readonly/scope/headers, streamed binary, HEAD/chunked, malformed/truncated response and cancellation')
        finally:
            if process.poll() is None: process.terminate(); process.wait(timeout=20)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    run(parser.parse_args().host.resolve())
