"""Opt-in real website/native PKCE check; one public search only with --search.

Password is read at runtime and never written to the fixture. No production
test hooks are added: browser suppression and the tool probe exist only in a
disposable copy of the app. Native login uses temporary RAM credentials.
"""
import argparse
import getpass
import http.cookiejar
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from urllib.parse import parse_qs, urlsplit
from test_account_runtime import request

ROOT = Path(__file__).resolve().parents[1]
HOOK = r'''
static xmutex* OnlineLock;
static xthread* OnlineThread;
static xvalue* OnlineOutput;
static xwork_permission_decision OnlinePermission(void* data,const xwork_permission_request* req)
{ (void)data; (void)req; return XWORK_PERMISSION_ALLOW; }
static int32 OnlineSearchWork(void* unused) {
    (void)unused;
    xwork_error error={0}; xwork_agent_definition_config dc; xwork_agent_options options;
    xllm_session_config sc; xllm_executor executor={0}; xllm_executor_ctx context={0};
    xllm_executor_result result={0}; xllm_tool_call call={0}; xvalue* output=NULL;
    xworkAgentDefinitionConfigInit(&dc); dc.sId="online.search.test";
    dc.bRegisterBuiltinTools=false; dc.bAutoSaveSession=false; dc.bRequireVerificationAfterWrite=false;
    xwork_agent_definition* definition=xworkAgentDefinitionCreate(&dc,&error);
    xllmSessionConfigInit(&sc); xllm_session* session=xllmSessionCreate(&sc,NULL);
    xworkAgentOptionsInit(&options); options.pSession=session; options.sWorkspaceRoot="."; options.OnPermission=OnlinePermission;
    xwork_agent* agent=xworkAgentCreateWithRuntime(MdoBootstrapRuntime(),definition,&options,&error);
    bool ok=definition && session && agent && xworkExecutorBind(&executor,agent,&error);
    context.uRound=1; context.uDeadline=xrtDeadlineAfter(30000000);
    call.sId="online-search"; call.sName="web_search";
    call.sArgumentsJson="{\"query\":\"Bocha Web Search API documentation\",\"count\":3}";
    if(ok) ok=executor.pExecute(executor.pUserData,&call,&context,&result) && result.bSuccess;
    if(ok && result.sContent) {
        /* xwork prefixes tool output with its status/name envelope. */
        const char* json=strchr(result.sContent,'{');
        if(json) output=xrtJsonParse(xrtStrView(json));
    }
    if(!output) {
        output=xrtValueObject(); MdoAccountSetString(output,"error",error.sMessage);
        MdoAccountSetString(output,"tool_message",result.sContent?result.sContent:"");
    }
    xworkExecutorUnbind(&executor); xworkAgentDestroy(agent);
    xllmSessionDestroy(session); xworkAgentDefinitionRelease(definition);
    xrtMutexLock(OnlineLock); OnlineOutput=output; xrtMutexUnlock(OnlineLock); return 0;
}
static bool OnlineSearch(MdoApiContext* c) {
    if(c->Request->head->MethodCode==XHTTP_METHOD_POST) {
        if(OnlineThread) return MdoApiReplyError(c,409,"already_started","This test submits only one search",NULL);
        OnlineLock=xrtMutexCreate(); OnlineThread=OnlineLock?xrtThreadCreate(OnlineSearchWork,NULL,0):NULL;
        if(!OnlineThread) return MdoApiReplyError(c,500,"start_failed","Cannot start test worker",NULL);
    }
    if(!OnlineThread) return MdoApiReplyError(c,409,"not_started","Start the search probe first",NULL);
    xrtMutexLock(OnlineLock); xvalue* out=OnlineOutput?xrtValueClone(OnlineOutput):xrtValueObject();
    if(!OnlineOutput) MdoAccountSetBool(out,"done",false);
    xrtMutexUnlock(OnlineLock); return MdoApiReplySuccessTake(c,200,out,NULL);
}
'''


def run(args):
    origin = args.origin.rstrip('/')
    parsed = urlsplit(origin)
    if parsed.scheme != 'https' or parsed.path or parsed.query or parsed.fragment or parsed.username:
        raise ValueError('an HTTPS website origin is required')
    password = sys.stdin.readline().rstrip('\r\n') if args.password_stdin else getpass.getpass('Website password: ')
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    tokens = {}; csrf = None; log = None

    def website(path, data=None, method=None, csrf=None):
        headers = {'User-Agent':'mdo-account-online/1','Origin':origin}
        if csrf: headers['X-CSRF-Token'] = csrf
        if data is not None: headers['Content-Type'] = 'application/json'
        req = urllib.request.Request(origin+path, None if data is None else json.dumps(data).encode(),headers,method=method)
        with opener.open(req,timeout=30) as response:
            raw = response.read(65537)
            if len(raw)>65536: raise ValueError('oversized website response')
            if 'application/json' not in response.headers.get('Content-Type',''):
                return response.geturl()
            return json.loads(raw)['data']

    site = Path(tempfile.mkdtemp(prefix='account-online-',dir=ROOT/'.build'))
    process = None
    try:
        tokens = website('/api/v1/login',{'identifier':args.username,'password':password}); password = ''
        session = website('/api/v1/session'); csrf = session['csrf_token']
        # Revoke only abandoned sessions created by this integration workflow.
        for row in website('/api/v1/sessions'):
            if not row.get('current') and row.get('user_agent') in ('mdo-account-integration/1','mdo-account-online/1'):
                website('/api/v1/sessions',{'session_id':row['id']},method='DELETE',csrf=csrf)
        usage = website('/api/v1/search/usage'); providers = website('/api/v1/search/providers')
        assert providers['default_provider']=='bocha' and usage['daily_limit']>0
        print('PASS authenticated online quota and providers; default Bocha')
        shutil.copytree(ROOT/'app',site,dirs_exist_ok=True)
        home=site/'mdo-home'; (home/'config').mkdir(parents=True)
        with socket.socket() as sock: sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
        config=json.loads((site/'xs.json').read_text(encoding='utf-8')); service=config['services'][0]
        service['class']='http'; service['port']=port; service.pop('window',None)
        (site/'xs.json').write_text(json.dumps(config))
        unity=site/'generated/mdo_unity.c'
        unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n#include <xsbase.h>\nstatic bool TestOpenUrl(cstr s){(void)s;return true;}\n#define xsOpenExternalUrl TestOpenUrl\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
        router=site/'src/api/router.c'; source=router.read_text(encoding='utf-8')
        source=source.replace('static const MdoApiRoute g_MdoApiRoutes[]',HOOK+'\nstatic const MdoApiRoute g_MdoApiRoutes[]')
        source=source.replace('static const MdoApiRoute g_MdoApiRoutes[] = {','static const MdoApiRoute g_MdoApiRoutes[] = {\n {"/api/v1/test-online-search",XHTTP_METHOD_GET|XHTTP_METHOD_POST,"GET, POST",OnlineSearch,false},')
        router.write_text(source,encoding='utf-8')
        env={**os.environ,'MDO_HOME':str(home)}; env.pop('MDO_SEARCH_ACCESS_TOKEN',None)
        log=(site/'test.log').open('wb')
        process=subprocess.Popen([str(args.host.resolve()),str(site/'xs.json')],cwd=site,env=env,stdout=log,stderr=subprocess.STDOUT)
        def local(path='/account',body=None):
            head={}
            if body is not None:
                _,headers,_=request(port,'GET','/api/v1/account'); head['X-Mdo-Write-Token']=headers['X-Mdo-Write-Token']
            status,_,raw=request(port,'POST' if body is not None else 'GET','/api/v1'+path,body,head)
            if status!=200: raise ValueError('native API failure: '+path+' '+str(status))
            return json.loads(raw)['data']
        for _ in range(100):
            if process.poll() is not None: raise RuntimeError('native host exited; fixture '+str(site))
            try: local(); break
            except OSError: time.sleep(.1)
        else: raise RuntimeError('native host readiness timeout')
        authorize=local('/account/login',{'remember':False})['authorization_url']
        url=urlsplit(authorize); assert url.scheme+'://'+url.netloc==origin
        landing=website(url.path+'?'+url.query)
        request_id=parse_qs(urlsplit(landing).query)['application'][0]
        decision=website('/api/v1/auth/authorize',{'request_id':request_id,'approve':True},csrf=csrf)
        callback=urlsplit(decision['redirect_uri']); assert callback.netloc=='127.0.0.1:'+str(port)
        assert request(port,'GET',callback.path+'?'+callback.query)[0]==200
        for _ in range(200):
            state=local()
            if state['state']=='signed_in' and not state['busy']: break
            time.sleep(.1)
        else: raise RuntimeError('native sign-in did not finish: '+state['message'])
        assert state['profile']['username']==args.username and state['usage']['daily_limit']>0
        assert not (home/'data/account/session.bin').exists()
        print('PASS real website approval, native PKCE exchange, profile and quota')
        if args.search:
            result=local('/test-online-search',{})
            for _ in range(400):
                if result.get('done') is not False: break
                time.sleep(.1); result=local('/test-online-search')
            assert result.get('source')=='bocha' and result.get('results'), result
            print('PASS real native web_search: Bocha results',len(result['results']))
        local('/account/logout',{})
        for _ in range(100):
            state=local()
            if state['state']=='signed_out' and not state['busy']: break
            time.sleep(.1)
        assert state['state']=='signed_out'
        assert website('/api/v1/profile')['username']==args.username
        print('PASS native logout; independent website session retained')
    finally:
        if process and process.poll() is None:
            try:
                if local()['state'] != 'signed_out': local('/account/logout',{})
                for _ in range(100):
                    if not local()['busy']: break
                    time.sleep(.1)
            except Exception: pass
            process.terminate()
            try: process.wait(timeout=20)
            except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=10)
        if log: log.close()
        if csrf: website('/api/v1/logout',{},csrf=csrf)
        tokens.clear(); password=''


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--origin',required=True); parser.add_argument('--username',required=True)
    parser.add_argument('--password-stdin',action='store_true'); parser.add_argument('--search',action='store_true')
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    run(parser.parse_args())
