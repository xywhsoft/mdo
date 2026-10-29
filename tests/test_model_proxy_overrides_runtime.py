"""Route real local TLS model requests through model/provider/global policies."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import ssl
import tempfile
import threading
from http.server import ThreadingHTTPServer

from test_model_proxy_runtime import ROOT, PROBE_SOURCE, ModelHandler, LocalProxy, ProxyHandler, run_probe, write_site


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='model-proxy-overrides-', dir=ROOT/'.build') as raw:
        base = Path(raw);site = base/'site';write_site(site)
        (site/'probe.c').write_text(PROBE_SOURCE, encoding='utf-8')
        model = ThreadingHTTPServer(('127.0.0.1', 0), ModelHandler)
        model.daemon_threads = True;model.hits = 0
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(ROOT/'tests/fixtures/proxy-localhost-cert.pem', ROOT/'tests/fixtures/proxy-localhost-key.pem')
        model.socket = tls.wrap_socket(model.socket, server_side=True)
        threading.Thread(target=model.serve_forever, daemon=True).start()
        proxies = {}
        try:
            for name, kind, auth in [('http', 'http-connect', True), ('socks', 'socks5', True), ('anonymous', 'http-connect', False)]:
                proxy = LocalProxy(('127.0.0.1', 0), ProxyHandler)
                proxy.kind = kind;proxy.auth = auth;proxy.target_port = model.server_port
                proxy.connections = 0;proxy.errors = [];proxies[name] = proxy
                threading.Thread(target=proxy.serve_forever, daemon=True).start()
            def policy(name, secret='env:MDO_TEST_PROXY_PASSWORD'):
                proxy = proxies[name]
                return {'kind': proxy.kind, 'host': '127.0.0.1', 'port': proxy.server_address[1],
                        **({'user': 'probe', 'credential': {'secret_ref': secret}} if proxy.auth else {})}
            cases = [
                ('default-global', {'kind':'none'}, policy('http'), None, None, 0),
                ('provider', {'kind':'none'}, policy('http'), {'kind':'provider'}, 'http', 0),
                ('model', policy('http'), policy('http'), policy('socks'), 'socks', 0),
                ('no-inherited-credentials', policy('http','env:MDO_UNAVAILABLE_GLOBAL_SECRET'), policy('socks'), policy('anonymous'), 'anonymous', 0),
                ('direct', policy('http','env:MDO_UNAVAILABLE_GLOBAL_SECRET'), policy('socks'), {'kind':'none'}, None, 0),
                ('explicit-global', policy('socks'), policy('http'), {'kind':'global'}, 'socks', 0),
                ('model-system', {'kind':'none'}, policy('socks'), {'kind':'system'}, 'http', 1),
                ('provider-system', {'kind':'none'}, {'kind':'system'}, {'kind':'provider'}, 'http', 1),
                ('global-system', {'kind':'system'}, policy('socks'), {'kind':'global'}, 'http', 1),
                ('provider-direct', policy('socks'), {'kind':'none'}, {'kind':'provider'}, None, 0),
                ('own-bypass', policy('http'), policy('http'), {**policy('socks'), 'bypass':'127.0.0.1'}, None, 0),
                ('own-unavailable-secret', {'kind':'none'}, policy('http'), policy('socks','env:MDO_UNAVAILABLE_OWN_SECRET'), 'error', 0),
            ]
            defaults = json.loads((ROOT/'app/default-home/config/defaults.json').read_text(encoding='utf-8'))
            for name, global_proxy, provider_proxy, model_proxy, expected, system in cases:
                home = base/name;(home/'config').mkdir(parents=True);(home/'certs').mkdir()
                shutil.copy2(ROOT/'tests/fixtures/proxy-localhost-ca.pem',home/'certs/local.pem')
                (home/'config/settings.json').write_text(json.dumps({'schema_version':1,'patch':{'transport':{
                    'ca_pem_path':'certs/local.pem','proxy':global_proxy}}}), encoding='utf-8')
                config = copy.deepcopy(defaults['models'])
                provider = {'id':'fixture','name':'Fixture','builtin':False,'editable':True,'removable':True,
                    'verify_peer':True,'timeout_ms':5000,'endpoints':{'responses':f'https://127.0.0.1:{model.server_port}/v1'},
                    'credential':{'secret_ref':'env:MDO_TEST_MODEL_KEY'},'proxy':provider_proxy}
                custom = copy.deepcopy(config['items'][0]);custom.update(id='fixture',name='Fixture',provider='fixture',
                    builtin=False,editable=True,removable=True,protocols=['openai-responses'],default_protocol='openai-responses')
                if model_proxy is not None:custom['proxy']=model_proxy
                config['providers'].append(provider);config['items'].append(custom)
                (home/'config/models.json').write_text(json.dumps({'schema_version':1,'patch':config}),encoding='utf-8')
                before = model.hits;counts = {k:p.connections for k,p in proxies.items()}
                output = run_probe(args.host.resolve(),site,home,{
                    'MDO_TEST_MODEL_ID':'fixture','MDO_TEST_MODEL_KEY':'local-fixture-model-key',
                    'MDO_TEST_PROXY_PASSWORD':'bounded-password',
                    'MDO_UNAVAILABLE_GLOBAL_SECRET':'','MDO_UNAVAILABLE_OWN_SECRET':'',
                    'MDO_TEST_SYSTEM_PROXY_KIND':'http-connect','MDO_TEST_SYSTEM_PROXY_PORT':str(proxies['http'].server_address[1])})
                assert f'proxy_system_mode={system}' in output,(name,output)
                if expected=='error':
                    assert 'proxy_client=0 kind=profile_proxy_unavailable' in output,(name,output)
                    assert model.hits==before,(name,output)
                else:
                    assert 'proxy_result=0 status=200' in output,(name,output)
                    assert model.hits==before+1,(name,output)
                for key,proxy in proxies.items():
                    assert proxy.connections-counts[key] == (1 if key==expected else 0),(name,key,proxy.errors,output)
                    assert not proxy.errors,(name,key,proxy.errors)
                print('PASS',name)
        finally:
            for proxy in proxies.values():proxy.shutdown();proxy.server_close()
            model.shutdown();model.server_close()
    print('model/provider/global proxy isolation and inheritance: PASS')


if __name__ == '__main__':main()
