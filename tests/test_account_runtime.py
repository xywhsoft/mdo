"""Real xs/xadmin native-login regression; isolated data, no public search.

Uses the website's actual member login and authorization handlers. Test-only
hooks in the disposable unity file suppress launching browsers and inspect
lease cancellation; production code contains no test endpoints or bypasses.
"""
from pathlib import Path
import argparse
from contextlib import nullcontext
import http.client
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from urllib.parse import parse_qs, urlencode, urlsplit

ROOT = Path(__file__).resolve().parents[1]
XADMIN = Path(os.environ.get('MDO_XADMIN_ROOT',str(ROOT.parent / 'x-admin'))).resolve()
sys.path.insert(0, str(XADMIN / 'tests'))
from smoke import fixture, PASSWORD


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); return sock.getsockname()[1]


def request(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=8)
    try:
        raw = None if body is None else json.dumps(body).encode()
        head = {'Content-Type': 'application/json', **(headers or {})}
        connection.request(method, path, raw, head)
        response = connection.getresponse()
        head = dict(response.getheaders())
        cookies = [value.split(';')[0] for key,value in response.getheaders() if key.lower() == 'set-cookie']
        if cookies: head['Cookies'] = '; '.join(cookies)
        return response.status, head, response.read()
    finally:
        connection.close()


HOOK = r'''
static MdoAccountLease AccountTestLease;
static xthread* AccountTestWorker;
static bool AccountTestTaken;
static int32 AccountTestSearch(void* data) {
    (void)data;
    xwork_tool_context context = {0}; context.uDeadline = XRT_DEADLINE_NEVER;
    AccountTestTaken = MdoAccountAcquire("local lease probe",&context,&AccountTestLease);
    return 0;
}
static bool AccountTestRoute(MdoApiContext* c) {
    xvalue* out = xrtValueObject();
    if (c->Target.Query.Size == 4 && !memcmp(c->Target.Query.Data,"poll",4)) {
        bool done = !AccountTestWorker || xrtThreadState(AccountTestWorker) == XTHREAD_FINISHED;
        if (done && AccountTestWorker) {
            xrtThreadWait(AccountTestWorker); xrtThreadDestroy(AccountTestWorker); AccountTestWorker = NULL;
        }
        MdoAccountSetBool(out,"done",done);
        MdoAccountSetBool(out,"taken",done && AccountTestTaken);
    } else if (AccountTestWorker) {
        xrtValueRelease(out);
        return MdoApiReplyError(c,409,"test_search_pending","Poll the background search first",NULL);
    } else if (c->Target.Query.Size == 7 && !memcmp(c->Target.Query.Data,"service",7)) {
        MdoAccountRelease(&AccountTestLease);
        bool ok = MdoAccountAcquireService(NULL,&AccountTestLease);
        MdoAccountSetBool(out,"taken",ok);
        MdoAccountSetUInt(out,"member_id",AccountTestLease.MemberId);
    } else if (c->Target.Query.Size == 5 && !memcmp(c->Target.Query.Data,"tools",5)) {
        xwork_tool_catalog* catalog = xworkRuntimeToolCatalogSnapshot(MdoBootstrapRuntime());
        xwork_tool_info info; unsigned count = 0;
        for (size_t i=0;i<xworkToolCatalogCount(catalog);i++) {
            if (xworkToolCatalogToolAt(catalog,i,&info) && info.sSource && !strcmp(info.sSource,"mdo.web")) count++;
        }
        MdoAccountSetUInt(out,"web_count",count);
        MdoAccountSetUInt(out,"generation",xworkToolCatalogGeneration(catalog));
        xworkToolCatalogRelease(catalog);
    } else if (c->Target.Query.Size == 4 && !memcmp(c->Target.Query.Data,"take",4)) {
        MdoAccountRelease(&AccountTestLease);
        AccountTestTaken = false;
        AccountTestWorker = xrtThreadCreate(AccountTestSearch,NULL,0);
        MdoAccountSetBool(out,"started",AccountTestWorker != NULL);
    } else if (c->Target.Query.Size == 6 && !memcmp(c->Target.Query.Data,"expire",6)) {
        xrtMutexLock(g_MdoAccount.Lock); g_MdoAccount.Tokens.Expires = 0; xrtMutexUnlock(g_MdoAccount.Lock);
        MdoAccountSetBool(out,"queued",MdoAccountRefresh());
    } else {
        MdoAccountSetBool(out,"cancelled",AccountTestLease.Cancel && xrtCancelRequested(AccountTestLease.Cancel));
        MdoAccountRelease(&AccountTestLease);
    }
    return MdoApiReplySuccessTake(c,200,out,NULL);
}
'''


def run(host):
    (ROOT / '.build').mkdir(exist_ok=True)
    website_port, app_port = free_port(), free_port()
    origin = f'http://127.0.0.1:{website_port}'
    website = fixture(website_port, register_interval=0)
    config = json.loads((website / 'xs.json').read_text())
    # The website fixture owns its source tree; keep compilation inside AppRoot.
    config['services'][0]['host_default']['devfile'] = 'main.c'
    (website / 'xs.json').write_text(json.dumps(config))
    (website / 'db/identity.json').write_text(json.dumps({'public_origin': origin, 'applications': [{
        'client_id': 'mdo-desktop', 'name': 'mdo test',
        'redirect_uris': ['http://127.0.0.1:{port}/api/v1/account/callback']}]}))
    processes, logs = [], []

    def launch(directory, config, home=None, executable=None):
        log = (directory / 'account-test.log').open('ab'); logs.append(log)
        env = dict(os.environ); env['MDO_SEARCH_ACCESS_TOKEN'] = 'obsolete-token-must-not-bypass-login'
        if home: env['MDO_HOME'] = str(home)
        process = subprocess.Popen([str(executable or host), str(config)], cwd=directory, env=env,
            stdout=log, stderr=subprocess.STDOUT)
        processes.append(process); return process

    def ready(port, process, path):
        for _ in range(150):
            if process.poll() is not None: raise RuntimeError('host exited; inspect ' + str(website))
            try:
                if request(port, 'GET', path)[0] == 200: return
            except OSError: pass
            time.sleep(.1)
        raise RuntimeError('host did not start')

    def call(port, method, path, body=None, headers=None, status=200):
        actual, head, raw = request(port, method, path, body, headers)
        assert actual == status, (path, actual, raw)
        return json.loads(raw).get('data') if raw and raw.startswith(b'{') else raw, head

    def app(method, path='/account', body=None, status=200):
        head = {}
        if method != 'GET':
            _, headers, _ = request(app_port, 'GET', '/api/v1/account')
            head['X-Mdo-Write-Token'] = headers['X-Mdo-Write-Token']
            if path.startswith('/settings/'):
                _, metadata, _ = request(app_port,'GET','/api/v1/settings')
                head['If-Match'] = metadata['ETag']
        return call(app_port, method, '/api/v1'+path, body, head, status)[0]

    def wait_state(state):
        for _ in range(150):
            value = app('GET')
            if value['state'] == state and not value['busy']: return value
            time.sleep(.1)
        raise AssertionError(value)

    def search_start():
        assert app('GET', '/test-account?take')['started']

    def search_result():
        for _ in range(100):
            value = app('GET', '/test-account?poll')
            if value['done']: return value['taken']
            time.sleep(.05)
        raise AssertionError('background search did not finish')

    def search_take():
        search_start()
        return search_result()

    def browser(user):
        tokens, headers = call(website_port, 'POST', '/api/v1/login', {'identifier': user, 'password': PASSWORD})
        cookie = headers['Cookies']
        # Website /session supplies its public CSRF field for cookie writes.
        session, headers = call(website_port, 'GET', '/api/v1/session', headers={'Cookie': cookie, 'Origin': origin})
        if 'Cookies' in headers: cookie = headers['Cookies']
        return cookie, session['csrf_token'], tokens

    def authorize(user, remember=True, denied=False):
        cookie, csrf, browser_tokens = browser(user)
        value = app('POST', '/account/login', {'remember': remember})
        url = urlsplit(value['authorization_url'])
        parameters = parse_qs(url.query)
        assert parameters['client_id'] == ['mdo-desktop'] and parameters['code_challenge_method'] == ['S256']
        assert 'code_verifier' not in value['authorization_url']
        _, headers = call(website_port, 'GET', url.path + '?' + url.query, status=303)
        request_id = parse_qs(urlsplit(headers['Location']).query)['application'][0]
        application_cookie = headers['Cookies']
        decision, _ = call(website_port, 'POST', '/api/v1/auth/authorize',
            {'request_id': request_id, 'approve': not denied}, {'Cookie': cookie+'; '+application_cookie,
             'Origin': origin, 'X-CSRF-Token': csrf})
        callback = urlsplit(decision['redirect_uri'])
        actual, _, _ = request(app_port, 'GET', callback.path+'?'+callback.query)
        assert actual == 200
        assert request(app_port, 'GET', callback.path+'?'+callback.query)[0] == 400
        return browser_tokens, callback

    try:
        web_process = launch(website, website/'xs.json', executable=XADMIN/'xs.exe'); ready(website_port, web_process, '/api/v1/auth/providers')
        for user in ('mdo_login_test', 'mdo_other_test'):
            call(website_port, 'POST', '/api/v1/register', {'username': user, 'password': PASSWORD}, status=201)
        with nullcontext(tempfile.mkdtemp(prefix='account-', dir=ROOT/'.build')) as raw:
            site = Path(raw); shutil.copytree(ROOT/'app', site, dirs_exist_ok=True)
            home = site/'mdo-home'; (home/'config').mkdir(parents=True)
            (home/'config/settings.json').write_text(json.dumps({'schema_version': 1, 'patch': {'web': {
                'allow_http': True, 'allow_private_networks': True, 'search': {'endpoint': 'https://obsolete.example/api/v1/search'}}}}))
            config = json.loads((site/'xs.json').read_text()); service = config['services'][0]
            service['class'] = 'http'; service['port'] = app_port; service.pop('window', None)
            (site/'xs.json').write_text(json.dumps(config))
            unity = site/'generated/mdo_unity.c'
            unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(origin)+'\n#include <xsbase.h>\nstatic bool TestOpenUrl(cstr s){(void)s;return true;}\n#define xsOpenExternalUrl TestOpenUrl\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
            router = site/'src/api/router.c'; source = router.read_text(); source = source.replace('static const MdoApiRoute g_MdoApiRoutes[]',HOOK+'\nstatic const MdoApiRoute g_MdoApiRoutes[]')
            source = source.replace('static const MdoApiRoute g_MdoApiRoutes[] = {','static const MdoApiRoute g_MdoApiRoutes[] = {\n {"/api/v1/test-account", XHTTP_METHOD_GET, "GET", AccountTestRoute, false},')
            router.write_text(source)
            running = launch(site, site/'xs.json', home); ready(app_port, running, '/api/v1/account')
            state = app('GET'); assert state['state'] == 'signed_out' and state['origin'] == origin, state
            assert not app('GET','/test-account?service')['taken']
            assert not app('GET')['pending_searches']
            assert app('GET','/test-account?tools')['web_count'] == 0
            assert request(app_port, 'POST', '/api/v1/account/logout', {})[0] == 428
            # Forged callbacks cannot create a session.
            assert request(app_port, 'GET', '/api/v1/account/callback?'+urlencode({'state':'0'*64,'code':'1'*64}))[0] == 400
            # A search has not submitted anything while waiting for login.
            # Skip wakes that waiter; another waiter resumes after login.
            search_start()
            for _ in range(50):
                state = app('GET')
                if state['pending_searches']: break
                time.sleep(.05)
            else: raise AssertionError('search waiter was not published')
            app('POST', '/account/search/skip', {'id':state['pending_searches'][0]['id']})
            assert not search_result()
            assert not app('GET')['pending_searches']
            search_start()
            browser_tokens, callback = authorize('mdo_login_test')
            assert search_result()
            state = wait_state('signed_in'); assert state['profile']['username'] == 'mdo_login_test', state
            published = app('GET','/test-account?tools'); assert published['web_count'] == 3
            app('GET'); assert app('GET','/test-account?tools')['generation'] == published['generation']
            app('PATCH','/settings/settings',{'schema_version':1,'patch':{'agent':{'web_search':False}}})
            assert app('GET','/test-account?tools')['web_count'] == 0
            app('PATCH','/settings/settings',{'schema_version':1,'patch':{'agent':{'web_search':True},'web':{'enabled':False}}})
            assert app('GET','/test-account?tools')['web_count'] == 3
            persisted = json.loads((home/'config/settings.json').read_text())
            assert 'enabled' not in persisted['patch'].get('web',{})
            assert state['remembered'] and state['persistence_available'], state
            assert 'access_token' not in json.dumps(state) and 'refresh_token' not in json.dumps(state)
            saved = home/'data/account/session.bin'; encrypted = saved.read_bytes()
            assert browser_tokens['refresh_token'].encode() not in encrypted and b'"refresh_token"' not in encrypted
            assert search_take()
            # Multiple refresh requests coalesce into a single rotation.
            before = state['revision']; app('POST','/account/refresh',{}); app('POST','/account/refresh',{})
            state = wait_state('signed_in'); assert state['revision'] > before and saved.read_bytes() != encrypted
            # Restart preserves the refresh, rotates once and restores profile.
            running.terminate(); running.wait(timeout=15)
            running = launch(site, site/'xs.json', home); ready(app_port, running, '/api/v1/account')
            state = wait_state('signed_in'); assert state['profile']['username'] == 'mdo_login_test', state
            assert search_take()
            app('POST','/account/logout',{}); wait_state('signed_out')
            assert app('GET','/test-account')['cancelled'] and not saved.exists()
            # Native logout does not revoke the originating browser session.
            call(website_port, 'GET', '/api/v1/profile', headers={'Authorization':'Bearer '+browser_tokens['access_token']})
            authorize('mdo_other_test', remember=False)
            state = wait_state('signed_in'); assert state['profile']['username'] == 'mdo_other_test' and not saved.exists()
            app('POST','/account/logout',{}); wait_state('signed_out')
            authorize('mdo_login_test', denied=True); state = wait_state('signed_out')
            assert state['message'] == 'authorization_cancelled', state
            app('POST','/account/login',{'remember':False}); app('POST','/account/cancel',{}); wait_state('signed_out')
            assert request(app_port,'GET',callback.path+'?'+callback.query)[0] == 400
            # First-party password login never launches or returns a browser URL.
            direct = app('POST','/account/login',{'identifier':'mdo_login_test','password':PASSWORD,'remember':True})
            assert 'authorization_url' not in direct and 'password' not in json.dumps(direct)
            state = wait_state('signed_in'); assert state['profile']['username'] == 'mdo_login_test'
            service = app('GET','/test-account?service')
            assert service['taken'] and service['member_id'] == state['profile']['id'], service
            app('POST','/account/refresh',{}); state = wait_state('signed_in')
            assert not app('GET','/test-account')['cancelled'], 'refresh must preserve service leases'
            assert app('GET','/test-account?service')['taken']
            assert state['remembered'] and PASSWORD.encode() not in saved.read_bytes()
            app('POST','/account/login',{'identifier':'mdo_other_test','password':'wrong-password','remember':True})
            state = wait_state('signed_in')
            assert state['profile']['username'] == 'mdo_login_test' and state['message'] == 'invalid_credentials', state
            app('POST','/account/login',{'identifier':'mdo_other_test','password':PASSWORD,'remember':False})
            state = wait_state('signed_in'); assert state['profile']['username'] == 'mdo_other_test' and not saved.exists()
            assert app('GET','/test-account')['cancelled'], 'account switch must cancel service leases'
            assert app('GET','/test-account?service')['taken']
            app('POST','/account/login',{'identifier':'mdo_other_test','password':PASSWORD,'remember':False,'endpoint':'https://evil.test'},status=409)
            app('POST','/account/logout',{}); wait_state('signed_out')
            assert app('GET','/test-account')['cancelled'], 'logout must cancel service leases'
            assert app('GET','/test-account?tools')['web_count'] == 0
            app('POST','/account/login',{'identifier':'mdo_login_test','password':'wrong-password','remember':False})
            state = wait_state('signed_out'); assert state['message'] == 'invalid_credentials'
            assert not saved.exists()
            print('PASS direct password login, private snapshots, encrypted/temporary credentials, failed switch preservation and strict input')
            print('PASS native browser login/PKCE/strict callback/CSRF/encrypted persistence/restart/refresh/switch/independent logout/cancel/search waiting and skip')
    finally:
        for process in reversed(processes):
            if process.poll() is None: process.terminate(); process.wait(timeout=20)
        for log in logs: log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT/'.build/host/xs.exe')
    run(parser.parse_args().host.resolve())
