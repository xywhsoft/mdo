"""Verify real /models/discover TLS routing through scoped provider proxies."""
import argparse
import json
import os
from pathlib import Path
import shutil
import ssl
import tempfile
import threading
from http.server import ThreadingHTTPServer

from test_api_runtime import request
from test_model_proxy_runtime import ModelHandler, LocalProxy, ProxyHandler
from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap, release_packed_copies


class CatalogHandler(ModelHandler):
    def do_GET(self):
        assert self.path == '/v1/models', self.path
        assert self.headers.get('Authorization') == 'Bearer local-discovery-fixture'
        self.server.hits += 1
        body = b'{"data":[{"id":"fixture-one"},{"id":"fixture-two"}]}'
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers();self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packed', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='proxy-discovery-packed-', dir=ROOT/'.build') as raw:
        base = Path(raw);folder,port = site(base,'site',args.packed.resolve())
        home = base/'mdo-home';(home/'config').mkdir(parents=True);(home/'certs').mkdir()
        shutil.copy2(ROOT/'tests/fixtures/proxy-localhost-ca.pem',home/'certs/local.pem')
        (home/'config/settings.json').write_text(json.dumps({'schema_version':1,'patch':{'transport':{
            'ca_pem_path':'certs/local.pem','proxy':{'kind':'http-connect','host':'127.0.0.1','port':1,
                'user':'unused','credential':{'secret_ref':'env:MDO_MISSING_GLOBAL_PROXY'}}}}}),encoding='utf-8')
        model = ThreadingHTTPServer(('127.0.0.1',0),CatalogHandler);model.daemon_threads=True;model.hits=0
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(ROOT/'tests/fixtures/proxy-localhost-cert.pem',ROOT/'tests/fixtures/proxy-localhost-key.pem')
        model.socket=tls.wrap_socket(model.socket,server_side=True)
        threading.Thread(target=model.serve_forever,daemon=True).start()
        proxies=[];process=None
        try:
            for kind in ('http-connect','socks5'):
                proxy=LocalProxy(('127.0.0.1',0),ProxyHandler);proxy.kind=kind
                proxy.target_port=model.server_port;proxy.connections=0;proxy.errors=[];proxies.append(proxy)
                threading.Thread(target=proxy.serve_forever,daemon=True).start()
            process=start(folder,args.packed.resolve(),home,{**os.environ,'USE_WEBVIEW':'0',
                'MDO_TEST_PROXY_PASSWORD':'bounded-password','MDO_MISSING_GLOBAL_PROXY':''})
            _,bootstrap=wait_bootstrap(process,port,folder/'packed.log');assert bootstrap['data']['ready'],bootstrap
            for choice in [*proxies,None,'global']:
                proxy={'kind':choice.kind,'host':'127.0.0.1','port':choice.server_address[1],
                    'user':'probe','credential':{'secret_ref':'env:MDO_TEST_PROXY_PASSWORD'}} if isinstance(choice,LocalProxy) else {'kind':'global' if choice=='global' else 'none'}
                before=model.hits;counts=[p.connections for p in proxies]
                status,_,body=request(port,'POST','/api/v1/models/discover',
                    body=json.dumps({'provider':{'endpoints':{'responses':f'https://127.0.0.1:{model.server_port}/v1/responses'},
                        'verify_peer':True,'proxy':proxy},'key':'local-discovery-fixture'}).encode(),headers={'Content-Type':'application/json'})
                value=json.loads(body)
                if choice=='global':
                    assert status==422 and value['error']['code']=='model_profile_proxy_unavailable',value
                    assert model.hits==before
                else:
                    assert status==200 and value['data']['items']==['fixture-one','fixture-two'],value
                    assert model.hits==before+1
                for i,p in enumerate(proxies):
                    assert p.connections-counts[i]==(1 if p is choice else 0),(proxy,p.kind,p.connections,counts[i],p is choice,p.errors,value)
                    assert not p.errors,p.errors
            print('packed provider catalog HTTPS: CONNECT, SOCKS5, direct and global credential failure: PASS')
        finally:
            if process:stop(process);release_packed_copies(folder/args.packed.name)
            for proxy in proxies:proxy.shutdown();proxy.server_close()
            model.shutdown();model.server_close()


if __name__=='__main__':main()
