"""Native unit cases for bounded remote write admission (no network load)."""
from __future__ import annotations
import argparse
import http.client
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from test_remote_loopback import ROOT, free_port

HOOK = r'''
static void ReceiptTestId(uint64 value, char id[33]) {
    snprintf(id,33u,"%032llx",(unsigned long long)value);
}
static bool ReceiptTestRoute(MdoApiContext* c) {
    MdoRemoteReceiptStore store, next; MdoRemoteReceipt* record = NULL;
    const char* client = "11111111111111111111111111111111";
    const char* id = "22222222222222222222222222222222";
    uint8 hash[32] = {1u}, other[32] = {2u}; xvalue* out = xrtValueObject();
    bool ok = MdoRemoteReceiptsInit(&store) && MdoRemoteReceiptsInit(&next) && strcmp(store.Runtime,next.Runtime);
    MdoAccountSetBool(out,"fresh_runtime",ok);
    ok = MdoRemoteReceiptClaim(&store,store.Runtime,client,1u,id,hash,true,&record) == MDO_REMOTE_RECEIPT_READ_ONLY && !record && !store.Order;
    ok = ok && MdoRemoteReceiptClaim(&store,next.Runtime,client,1u,id,hash,false,&record) == MDO_REMOTE_RECEIPT_RUNTIME_CHANGED && !record && !store.Order;
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,client,0u,id,hash,false,&record) == MDO_REMOTE_RECEIPT_INVALID;
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,client,MDO_REMOTE_SEQUENCE_MAX+1u,id,hash,false,&record) == MDO_REMOTE_RECEIPT_INVALID;
    ok = ok && !MdoRemoteIdValid("ABCDEFABCDEFABCDEFABCDEFABCDEFABCD");
    MdoAccountSetBool(out,"admission_guard",ok);
    ok = MdoRemoteReceiptClaim(&store,store.Runtime,client,1u,id,hash,false,&record) == MDO_REMOTE_RECEIPT_ADMITTED;
    MdoRemoteReceipt* first = record;
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,client,1u,id,hash,false,&record) == MDO_REMOTE_RECEIPT_DUPLICATE && record == first;
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,client,1u,id,other,false,&record) == MDO_REMOTE_RECEIPT_CONFLICT && !record;
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,client,2u,id,hash,false,&record) == MDO_REMOTE_RECEIPT_CONFLICT;
    MdoAccountSetBool(out,"duplicate_and_conflict",ok);
    ok = !MdoRemoteReceiptFinish(first,true,200u) && MdoRemoteReceiptRun(first) && !MdoRemoteReceiptRun(first);
    ok = ok && MdoRemoteReceiptFinish(first,true,201u) && !MdoRemoteReceiptFinish(first,false,0u);
    ok = ok && first->Phase == MDO_REMOTE_RECEIPT_DONE && first->Status == 201u;
    MdoAccountSetBool(out,"terminal_not_reexecuted",ok);
    ok = true;
    for (uint64 seq=2u; seq<=65u; seq++) {
        char key[33]; ReceiptTestId(seq,key);
        if (MdoRemoteReceiptClaim(&store,store.Runtime,client,seq,key,hash,false,&record) != MDO_REMOTE_RECEIPT_ADMITTED ||
            !MdoRemoteReceiptRun(record) || !MdoRemoteReceiptFinish(record,true,200u)) ok = false;
    }
    ok = ok && MdoRemoteReceiptQuery(&store,store.Runtime,client,1u,id,&record) == MDO_REMOTE_RECEIPT_EXPIRED && !record;
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,client,1u,id,hash,false,&record) == MDO_REMOTE_RECEIPT_EXPIRED;
    MdoAccountSetBool(out,"watermark_survives_eviction",ok);
    MdoRemoteReceiptsInit(&store); ok = true;
    for (uint64 seq=1u; seq<=64u; seq++) {
        char key[33]; ReceiptTestId(seq,key);
        if (MdoRemoteReceiptClaim(&store,store.Runtime,client,seq,key,hash,false,&record) != MDO_REMOTE_RECEIPT_ADMITTED) ok = false;
    }
    char key[33]; ReceiptTestId(65u,key);
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,client,65u,key,hash,false,&record) == MDO_REMOTE_RECEIPT_FULL;
    ok = ok && store.Clients[0].Sequence == 64u && MdoRemoteReceiptFinish(&store.Records[0],false,0u);
    ok = ok && store.Records[0].Phase == MDO_REMOTE_RECEIPT_NOT_STARTED;
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,client,65u,key,hash,false,&record) == MDO_REMOTE_RECEIPT_ADMITTED;
    ok = ok && MdoRemoteReceiptRun(record) && MdoRemoteReceiptFinish(record,false,200u) && record->Phase == MDO_REMOTE_RECEIPT_UNCERTAIN;
    MdoAccountSetBool(out,"capacity_keeps_live_receipts",ok);
    MdoRemoteReceiptsInit(&store); ok = true;
    for (uint64 i=1u; i<=128u; i++) {
        char owner[33], request_id[33]; ReceiptTestId(i,owner); ReceiptTestId(i+1024u,request_id);
        if (MdoRemoteReceiptClaim(&store,store.Runtime,owner,1u,request_id,hash,false,&record) != MDO_REMOTE_RECEIPT_ADMITTED ||
            !MdoRemoteReceiptFinish(record,false,0u)) ok = false;
    }
    char owner[33]; ReceiptTestId(129u,owner); ReceiptTestId(129u+1024u,key);
    ok = ok && MdoRemoteReceiptClaim(&store,store.Runtime,owner,1u,key,hash,false,&record) == MDO_REMOTE_RECEIPT_FULL;
    ReceiptTestId(1u,owner); ReceiptTestId(1025u,key);
    ok = ok && MdoRemoteReceiptQuery(&store,store.Runtime,owner,1u,key,&record) == MDO_REMOTE_RECEIPT_EXPIRED;
    MdoAccountSetBool(out,"client_watermarks_not_evicted",ok);
    xrtSecureZero(&store,sizeof(store)); xrtSecureZero(&next,sizeof(next));
    return MdoApiReplySuccessTake(c,200u,out,NULL);
}
'''


def run(host):
    (ROOT/'.build').mkdir(exist_ok=True)
    site = Path(tempfile.mkdtemp(prefix='remote-receipts-',dir=ROOT/'.build'))
    shutil.copytree(ROOT/'app',site,dirs_exist_ok=True)
    port = free_port(); config = json.loads((site/'xs.json').read_text()); service = config['services'][0]
    service.update({'class':'http','port':port}); service.pop('window',None)
    (site/'xs.json').write_text(json.dumps(config))
    router = site/'src/api/router.c'; source = router.read_text()
    source = source.replace('static const MdoApiRoute g_MdoApiRoutes[]',HOOK+'\nstatic const MdoApiRoute g_MdoApiRoutes[]')
    source = source.replace('static const MdoApiRoute g_MdoApiRoutes[] = {',
        'static const MdoApiRoute g_MdoApiRoutes[] = {\n'
        ' {"/api/v1/test-remote-receipts",XHTTP_METHOD_GET,"GET",ReceiptTestRoute,false},')
    router.write_text(source)
    env = dict(os.environ); env['MDO_HOME'] = str(site/'mdo-home')
    with (site/'test.log').open('wb') as log:
        process = subprocess.Popen([str(host),str(site/'xs.json')],cwd=site,env=env,stdout=log,stderr=log)
        try:
            for _ in range(150):
                if process.poll() is not None: raise AssertionError(f'host exited: {site}/test.log')
                connection = http.client.HTTPConnection('127.0.0.1',port,timeout=5)
                try:
                    connection.request('GET','/api/v1/test-remote-receipts'); response = connection.getresponse()
                    if response.status == 200:
                        value = json.loads(response.read())['data']; break
                except OSError: pass
                finally: connection.close()
                time.sleep(.05)
            else: raise AssertionError('test did not become ready')
            assert all(value.values()),value
            print('PASS runtime/readonly guards, fingerprint conflicts, duplicate writes, terminal phases, record eviction watermark, pending capacity and retained client watermarks')
        finally:
            if process.poll() is None: process.terminate(); process.wait(timeout=20)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    run(parser.parse_args().host.resolve())
