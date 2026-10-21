"""Real native OAuth/PKCE/refresh flow against a deterministic local peer."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode, urlsplit
from test_api_runtime import free_port, request, wait_ready
from test_mcp_runtime import server_document

ROOT = Path(__file__).resolve().parents[1]


class Provider(BaseHTTPRequestHandler):
    exchanges = 0
    refreshes = 0
    accepted = 'fixture-access-1'
    challenge = ''
    token_failure = 0
    reject_once = False
    revoked = 0

    def log_message(self, *_): pass

    def reply(self, status, body, **headers):
        raw = json.dumps(body).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(raw)))
        for key, value in headers.items(): self.send_header(key, value)
        self.end_headers(); self.wfile.write(raw)

    @property
    def origin(self): return f'http://127.0.0.1:{self.server.server_port}'

    def do_GET(self):
        if self.path == '/mcp':
            self.reply(401, {}, **{'WWW-Authenticate': f'Bearer resource_metadata="{self.origin}/resource", scope="read write"'})
        elif self.path == '/resource':
            self.reply(200, {'resource': self.origin+'/mcp', 'authorization_servers': [self.origin]})
        elif self.path == '/.well-known/oauth-authorization-server':
            self.reply(200, {'issuer': self.origin, 'authorization_endpoint': self.origin+'/authorize',
                'token_endpoint': self.origin+'/token', 'revocation_endpoint': self.origin+'/revoke', 'code_challenge_methods_supported': ['S256']})
        else: self.reply(404, {})

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        if self.path == '/token':
            form = parse_qs(body.decode())
            assert form['resource'] == [self.origin+'/mcp'], form
            assert form['client_id'] == ['mdo-fixture'], form
            if form['grant_type'] == ['authorization_code']:
                type(self).exchanges += 1
                assert form['code'] == ['fixture-code'], form
                digest = base64.urlsafe_b64encode(hashlib.sha256(form['code_verifier'][0].encode()).digest()).rstrip(b'=').decode()
                assert digest == type(self).challenge, (digest, type(self).challenge)
            else:
                assert form['refresh_token'] == ['fixture-refresh'], form
                type(self).refreshes += 1
            if type(self).token_failure:
                self.reply(type(self).token_failure, {'error': 'temporarily_unavailable'}); return
            type(self).accepted = f'fixture-access-{type(self).refreshes+1}'
            # No expires_in means unknown, never an invented short lifetime.
            self.reply(200, {'access_token': type(self).accepted, 'token_type': 'Bearer',
                **({'refresh_token': 'fixture-refresh'} if form['grant_type'] == ['authorization_code'] else {})})
        elif self.path == '/revoke':
            assert parse_qs(body.decode())['token'] == ['fixture-refresh']
            type(self).revoked += 1; self.reply(200, {})
        elif self.path == '/mcp':
            if self.headers.get('Authorization') != 'Bearer '+type(self).accepted or type(self).reject_once:
                type(self).reject_once = False; self.reply(401, {}); return
            rpc = json.loads(body)
            if rpc['method'] == 'notifications/initialized': self.reply(202, {}); return
            result = {'protocolVersion': '2025-11-25', 'serverInfo': {'name': 'fixture', 'version':'1'}, 'capabilities': {'tools': {}, 'resources': {}, 'prompts': {}, 'completions': {}}, 'instructions':'REMOTE_CONTEXT_INSTRUCTIONS'}
            if rpc['method'] == 'tools/list': result = {'tools': []}
            elif rpc['method']=='resources/list': result={'resources':[{'uri':'fixture:///note','name':'note'}],**({} if rpc['params'].get('cursor') else {'nextCursor':'second'})}
            elif rpc['method']=='resources/templates/list': result={'resourceTemplates':[{'uriTemplate':'fixture:///{name}','name':'notes'}]}
            elif rpc['method']=='resources/read': result={'contents':[{'uri':rpc['params']['uri'],'text':'RESOURCE_FIXTURE_CONTENT'}]}
            elif rpc['method']=='prompts/list': result={'prompts':[{'name':'greet','arguments':[{'name':'name','required':True}]}]}
            elif rpc['method']=='prompts/get': result={'messages':[{'role':'user','content':{'type':'text','text':'Hello '+rpc['params']['arguments']['name']}}]}
            elif rpc['method']=='completion/complete': result={'completion':{'values':['fixture'],'total':1,'hasMore':False}}
            self.reply(200, {'jsonrpc':'2.0', 'id':rpc['id'], 'result':result})
        else: self.reply(404, {})


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--host', type=Path, required=True); args = parser.parse_args()
    provider = ThreadingHTTPServer(('127.0.0.1', 0), Provider)
    thread = threading.Thread(target=provider.serve_forever, daemon=True); thread.start()
    with tempfile.TemporaryDirectory(prefix='mcp-oauth-', dir=ROOT/'.build') as raw:
        base = Path(raw); site = base/'site'; shutil.copytree(ROOT/'app', site)
        home = base/'home'; port = free_port()
        config = {'engine':{'workers':1},'services':[{'enabled':True,'class':'http','name':'oauth-fixture','ip':'127.0.0.1','port':port,
            'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}
        (site/'xs.json').write_text(json.dumps(config), encoding='utf-8')
        env = dict(os.environ, MDO_HOME=str(home), USE_WEBVIEW='0')
        process = None
        with (base/'log').open('wb') as log:
            def start():
                nonlocal process
                process = subprocess.Popen([str(args.host.resolve()),str(site/'xs.json')],cwd=site,env=env,stdout=log,stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                wait_ready(port, process)
            def stop():
                process.terminate(); process.wait(timeout=10)
            def call(method,path,body=None,revision=None):
                headers={'Content-Type':'application/json'}
                if revision: headers['If-Match']=f'"{revision}"'
                status,_,body=request(port,method,'/api/v1/'+path,body=json.dumps(body).encode() if body is not None else None,headers=headers)
                return status,json.loads(body)
            def status():
                code,body=call('GET','mcp/oauth-fixture/oauth'); assert code==200,body; return body['data']
            def settled():
                for _ in range(200):
                    data=status()
                    if not data['busy']: return data
                    time.sleep(.025)
                raise AssertionError('OAuth worker did not settle')
            def refresh():
                code,data=call('POST','mcp/oauth-fixture/refresh'); assert code==202,data
                operation=data['data']['id']
                for _ in range(200):
                    _,data=call('GET','operations/'+operation)
                    if data['data']['terminal']: return data['data']
                    time.sleep(.025)
                raise AssertionError('Refresh did not settle')
            try:
                start()
                document=json.loads(server_document('unused','unused','unused',server_id='oauth-fixture'))
                document['protocol_version']='2025-11-25'
                document['transport']={'type':'streamable-http','endpoint':f'http://127.0.0.1:{provider.server_port}/mcp','headers':[]}
                document['oauth']={'client_id':'mdo-fixture'}
                code,result=call('PUT','extensions/mcp/oauth-fixture',{'content':json.dumps(document)},'new'); assert code==200,result
                assert not status()['authorized']
                code,result=call('POST','mcp/oauth-fixture/oauth/start'); assert code==202,result
                data=settled(); assert data['state']=='ready',data
                params=parse_qs(urlsplit(data['authorization_url']).query)
                assert params['scope']==['read write'],params
                assert params['code_challenge_method']==['S256'],params
                assert params['resource']==[document['transport']['endpoint']],params
                assert params['redirect_uri']==[f'http://127.0.0.1:{port}/api/v1/mcp/oauth/callback'],params
                Provider.challenge=params['code_challenge'][0]
                # Native restart must preserve verifier/state and pending index.
                stop(); start()
                callback='state='+params['state'][0]+'&code=fixture-code&iss='+urlencode({'i':f'http://127.0.0.1:{provider.server_port}'})[2:]
                code,headers,_=request(port,'GET','/api/v1/mcp/oauth/callback?'+callback)
                assert code==200,code
                assert headers['cache-control']=='no-store'
                data=settled(); assert data['authorized'] and data['expires_at']==0,data
                assert Provider.exchanges==1
                assert request(port,'GET','/api/v1/mcp/oauth/callback?'+callback)[0]==400
                assert refresh()['state']=='succeeded'
                stop();start()
                assert status()['authorized']
                Provider.reject_once=True
                assert refresh()['state']=='succeeded'
                assert Provider.refreshes==1
                Provider.reject_once=True;Provider.token_failure=503
                assert refresh()['state']=='failed'
                assert status()['authorized'],status()
                Provider.token_failure=0
                assert refresh()['state']=='succeeded'
                Provider.reject_once=True
                assert refresh()['state']=='succeeded'
                assert Provider.refreshes==3 # original refresh token survived omission and 503
                def context(method,params={}):
                    code,data=call('POST','mcp/oauth-fixture/context',{'method':method,'params':params});assert code==202,data
                    for _ in range(200):
                        _,operation=call('GET','operations/'+data['data']['id'])
                        if operation['data']['terminal']:
                            assert operation['data']['state']=='succeeded',operation
                            return operation['data']['result']['content']
                        time.sleep(.025)
                    raise AssertionError('Context read timed out')
                assert context('resources/list')['nextCursor']=='second'
                assert 'nextCursor' not in context('resources/list',{'cursor':'second'})
                assert context('resources/templates/list')['resourceTemplates'][0]['uriTemplate']=='fixture:///{name}'
                assert context('resources/read',{'uri':'fixture:///note'})['contents'][0]['text']=='RESOURCE_FIXTURE_CONTENT'
                assert context('prompts/list')['prompts'][0]['name']=='greet'
                assert context('prompts/get',{'name':'greet','arguments':{'name':'world'}})['messages'][0]['content']['text']=='Hello world'
                assert context('completion/complete',{'ref':{'type':'ref/prompt','name':'greet'},'argument':{'name':'name','value':'f'}})['completion']['values']==['fixture']
                assert call('POST','mcp/oauth-fixture/context',{'method':'tools/call','params':{'name':'delete'}})[0]==400
                code,diag=call('GET','mcp/oauth-fixture/diagnostics');assert code==200 and diag['data']['resources'] and diag['data']['prompts'],diag
                assert diag['data']['instructions']=='REMOTE_CONTEXT_INSTRUCTIONS',diag
                code,result=call('DELETE','mcp/oauth-fixture/oauth');assert code==202,result
                assert not settled()['authorized']
                assert Provider.revoked==1
                stop()
                for path in home.rglob('*'):
                    if path.is_file():
                        content=path.read_bytes()
                        assert b'fixture-access-' not in content and b'fixture-refresh' not in content and b'fixture-code' not in content,path
                print('MCP OAuth PKCE, restart, refresh, 401, transient failure and logout: PASS')
            except BaseException:
                log.flush(); print((base/'log').read_text(encoding='utf-8',errors='replace')[-4500:]);raise
            finally:
                if process and process.poll() is None: stop()
    provider.shutdown();provider.server_close();thread.join(timeout=2)

if __name__=='__main__':main()
