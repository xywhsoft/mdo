"""Disposable full native/frontend auth recovery fixture; synthetic loopback only."""
import argparse,json,os,pathlib,shutil,socket,subprocess,sys,tempfile,threading,time
from http.server import ThreadingHTTPServer
ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root',type=pathlib.Path,default=ROOT,help='Prepared source checkout; use a clean checkout for release verification')
parser.add_argument('--host',type=pathlib.Path,help='xs compiled with the matching source host profile')
args=parser.parse_args();QA=args.source_root.resolve();HOST=args.host.resolve() if args.host else QA/'.build/host/xs.exe'
sys.path.insert(0,str(ROOT/'tests'))
from test_online_model_auth_account import Identity
(ROOT/'.build').mkdir(exist_ok=True)
BASE=pathlib.Path(tempfile.mkdtemp(prefix='online-auth-ui-',dir=ROOT/'.build'))
class Fixture(Identity):
    model_calls=0
    def do_GET(self):
        if self.path=='/api/v1/ai/catalog':
            return self.reply(200,{'code':0,'data':{'version':'c'*64,'models':[{
                'id':'ornith-1.5-35b','title':'Ornith fixture','available':True,'member_only':False,'free':True,
                'tool_calling':True,'vision':False,'context_window':240128,'max_output':16384,
                'reasoning_efforts':'low,medium,high','output_limit_field':'max_tokens',
                'default_protocol':'chat','protocols':['chat']}]}})
        return super().do_GET()
    def do_POST(self):
        if self.path!='/api/v1/ai/chat/completions':return super().do_POST()
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        type(self).model_calls+=1
        if self.model_calls==1:return self.reply(401,{'error':{'code':'invalid_token','message':'Fixture access expired'}})
        if self.model_calls==2:return self.reply(429,{'error':{'code':'rate_limit_exceeded','message':'Fixture retry'}})
        assert self.headers.get('Authorization')=='Bearer fresh-fixture-token'
        text='已完成令牌续期和限流重试，回复正常。' if self.model_calls==3 else '续聊正常，上一轮内容已保留。'
        if body.get('stream'):
            value={'id':'fixture','model':body['model'],'choices':[{'index':0,'delta':{'role':'assistant','content':text},'finish_reason':None}]}
            end={'id':'fixture','model':body['model'],'choices':[{'index':0,'delta':{},'finish_reason':'stop'}],'usage':{'prompt_tokens':10,'completion_tokens':10,'total_tokens':20}}
            raw=('data: '+json.dumps(value)+'\n\ndata: '+json.dumps(end)+'\n\ndata: [DONE]\n\n').encode()
            self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Content-Length',str(len(raw)))
            self.send_header('Connection','close');self.end_headers();self.wfile.write(raw)
        else:self.reply(200,{'id':'fixture','choices':[{'message':{'role':'assistant','content':text},'finish_reason':'stop'}]})
Fixture.calls.clear();Fixture.renewals.clear()
server=ThreadingHTTPServer(('127.0.0.1',0),Fixture)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
site=BASE/'site';shutil.copytree(QA/'app',site,dirs_exist_ok=True)
with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
config=json.loads((site/'xs.json').read_text());svc=config['services'][0];svc['port']=port;svc['class']='http';svc.pop('window',None)
(site/'xs.json').write_text(json.dumps(config))
unity=site/'generated/mdo_unity.c';unity.write_text('#define MDO_ACCOUNT_SERVICE_ORIGIN '+json.dumps(f'http://127.0.0.1:{server.server_port}')+'\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')
env=dict(os.environ);env['MDO_HOME']=str(BASE/'home')
log=(BASE/'native.log').open('wb')
process=subprocess.Popen([str(HOST),str(site/'xs.json')],cwd=site,env=env,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
stop=BASE/'stop';stop.unlink(missing_ok=True)
info={'url':f'http://127.0.0.1:{port}/','pid':process.pid,'origin':f'http://127.0.0.1:{server.server_port}','directory':str(BASE),'stop_file':str(stop)}
(BASE/'instance.json').write_text(json.dumps(info,indent=2))
try:
    print(json.dumps(info),flush=True)
    while not stop.exists():
        assert process.poll() is None,'Native process exited'
        (BASE/'report.json').write_text(json.dumps({'model_requests':Fixture.model_calls,'token_refreshes':dict(Fixture.renewals)},indent=2))
        time.sleep(.2)
finally:
    if process.poll() is None:process.terminate();process.wait(timeout=10)
    log.close();server.shutdown();server.server_close();thread.join(timeout=3)
