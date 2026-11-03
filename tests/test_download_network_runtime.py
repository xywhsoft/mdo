"""In-place download TLS/proxy acceptance; localhost and tiny fixtures only."""
import argparse, base64, hashlib, http.client, json, os, socket, ssl, subprocess, sys, tempfile, threading, time, uuid, zipfile
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from test_api_runtime import ROOT, free_port, request, wait_ready
from test_model_proxy_runtime import LocalProxy, ProxyHandler, read_exact, tunnel

class DownloadProxy(ProxyHandler):
    def handle(self):
        if self.server.kind!='http-connect' or self.request.recv(4,socket.MSG_PEEK) not in (b'GET ',b'HEAD'):
            return super().handle()
        client=self.request;header=bytearray()
        while not header.endswith(b'\r\n\r\n') and len(header)<8192:header.extend(read_exact(client,1))
        from urllib.parse import urlsplit
        lines=header.decode('ascii').split('\r\n');method,url,version=lines[0].split();target=urlsplit(url)
        assert target.hostname=='127.0.0.1' and target.port==self.server.target_port
        token=base64.b64encode(b'probe:bounded-password').decode()
        assert any(line.lower()==('proxy-authorization: basic '+token).lower() for line in lines),lines
        forwarded=[line for line in lines[1:] if not line.lower().startswith('proxy-authorization:')]
        data=(f'{method} {target.path or "/"}'+('?' + target.query if target.query else '')+f' {version}\r\n'+'\r\n'.join(forwarded)).encode()
        with socket.create_connection(('127.0.0.1',target.port),5) as upstream:
            self.server.connections+=1;upstream.sendall(data);tunnel(client,upstream)

PAYLOAD=b'download-network-fixture\n'*8192
HASH=hashlib.sha256(PAYLOAD).hexdigest()
class Publisher(BaseHTTPRequestHandler):
    hits=0
    def log_message(self,*_):pass
    def do_GET(self):
        if self.path=='/short':
            self.send_response(200);self.send_header('Content-Length','11');self.end_headers();self.wfile.write(b'{}');return
        redirects={'/redirect':'/file','/forbidden':'https://blocked.invalid/file','/downgrade':'http://127.0.0.1/file','/loop':'/loop'}
        if self.path in redirects:
            self.send_response(302);self.send_header('Location',redirects[self.path]);self.send_header('Content-Length','0');self.end_headers();return
        if self.path not in ('/file','/meta'):return self.send_error(404)
        type(self).hits+=1
        data=PAYLOAD if self.path=='/file' else b'{"ok":true}'
        self.send_response(200);self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);parser.add_argument('--source-only',action='store_true');args=parser.parse_args()
    sys.path.insert(0,str(ROOT));from tools import build_mdo
    build_mdo.write_if_changed(build_mdo.UNITY_PATH,build_mdo.generated_unity(build_mdo.load_object(build_mdo.LOCK_PATH),build_mdo.source_list()).encode())
    with ThreadingHTTPServer(('127.0.0.1',0),Publisher) as publisher,ThreadingHTTPServer(('127.0.0.1',0),Publisher) as plain,tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='download-network-') as raw:
        tls=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);tls.load_cert_chain(ROOT/'tests/fixtures/proxy-localhost-cert.pem',ROOT/'tests/fixtures/proxy-localhost-key.pem')
        publisher.socket=tls.wrap_socket(publisher.socket,server_side=True);threading.Thread(target=publisher.serve_forever,daemon=True).start()
        threading.Thread(target=plain.serve_forever,daemon=True).start()
        base=Path(raw);home=base/'home';(home/'config').mkdir(parents=True);(home/'certs').mkdir()
        (home/'certs/ca.pem').write_bytes((ROOT/'tests/fixtures/proxy-localhost-ca.pem').read_bytes())
        aria=ROOT/'tools/runtime/windows-x86_64/aria2/aria2c.exe'
        if os.name=='nt' and aria.is_file():
            (home/'data').mkdir()
            with zipfile.ZipFile(home/'data/aria.zip','w',zipfile.ZIP_DEFLATED) as z:
                z.writestr('extension.json',json.dumps(dict(kind='external-tools',id='external-tool:mdo.aria2',alias='aria2',name='aria2',entry='tool.json',platforms=['windows-x86_64'])))
                z.writestr('tool.json',json.dumps(dict(program='aria2c.exe',commands=[dict(name='aria2c',entry='aria2c.exe')],prompt='QA')));z.write(aria,'aria2c.exe')
        stem='.network-qa-'+uuid.uuid4().hex;source=ROOT/'app'/(stem+'.c');config=ROOT/'app'/(stem+'.json');port=free_port()
        source.write_text(f'''#include <xsbase.h>
static bool QaSecure=true;static uint16 QaPort={publisher.server_port};
#define MDO_DISTRIBUTION_HOST "127.0.0.1"
#define MDO_DISTRIBUTION_PORT QaPort
#define MDO_DISTRIBUTION_SECURE QaSecure
#define MDO_ACCOUNT_SERVICE_ORIGIN "https://127.0.0.1:{publisher.server_port}"
#define ServiceInit QaInit
#define RequestProc QaRequest
#include "generated/mdo_unity.c"
#undef ServiceInit
#undef RequestProc
static xnetengine* QaEngine;static xvalue* QaValue;
void ServiceInit(XS_HostInfo* H){{char* Plain=xrtEnvGet("QA_PLAIN");if(Plain){{QaSecure=false;QaPort=(uint16)atoi(Plain);}}xrtFree(Plain);QaEngine=H->Server->Engine;QaInit(H);char* Enabled=xrtEnvGet("QA_ARIA");
 if(Enabled){{char E[512]={{0}};xvalue* V=NULL;MdoExtensionsInstall("external-tools","{'A'*32}","data/aria.zip",NULL,true,NULL,&V,E);xrtValueRelease(V);MdoExtensionsSetEnabled("external-tools","{'A'*32}",true,E);MdoCliReload();}}xrtFree(Enabled);
}}
static int32 QaRun(void* Unused){{(void)Unused;
  MdoTransferReport Report={{0}};bool Ok=MdoTransferDownloadReport(QaEngine,"/file","data/downloads/network.bin",{len(PAYLOAD)},"{HASH}",NULL,NULL,NULL,&Report);
  char FetchUrl[160];snprintf(FetchUrl,sizeof(FetchUrl),"%s://127.0.0.1:%u/meta",QaSecure?"https":"http",(unsigned)QaPort);
  XS_FetchRequest F={{0}};F.Url=FetchUrl;F.Method="GET";F.Timeout=3000000;F.IdleTimeout=1000000;F.MaxBodyBytes=4096;XS_FetchResponse Out={{0}};
  bool Fetch=MdoDownloadFetch(&F,&Out)&&Out.Status==200&&Out.BodySize==11;MdoDownloadFetchUnit(&Out);
  bool ShortRetry=true;
  if(Ok){{snprintf(FetchUrl,sizeof(FetchUrl),"%s://127.0.0.1:%u/short",QaSecure?"https":"http",(unsigned)QaPort);xrtClearError();
   bool Read=MdoDownloadFetch(&F,&Out);ShortRetry=!Read&&MdoExtServiceTransientTransport(xrtGetError());
   if(!ShortRetry){{fprintf(stderr,"Short fixture accepted=%u bytes=%zu\\n",Read,Out.BodySize);for(const xerror* E=xrtGetError();E;E=xrtErrorCause(E))fprintf(stderr,"Short cause %s kind=%d code=%d\\n",xrtErrorDomain(E),(int)xrtErrorKind(E),(int)xrtErrorCode(E));}}
   MdoDownloadFetchUnit(&Out);xrtClearError();}}
  bool Sources=true;
  if(Ok&&QaSecure){{
   const cstr Paths[]={{"redirect","forbidden","downgrade","loop","file"}};
   for(size_t I=0;I<5;I++){{char Url[200],Relative[120];snprintf(Url,sizeof(Url),"https://127.0.0.1:{publisher.server_port}/%s",Paths[I]);snprintf(Relative,sizeof(Relative),"data/downloads/assets/model-%zu.bin",I);
    MdoDownloadSource Source={{Url,Relative,"{HASH}",I==4?536870913ULL:{len(PAYLOAD)},NULL,0}};MdoTransferReport R={{0}};
    bool Downloaded=MdoTransferDownloadSource(QaEngine,&Source,NULL,NULL,NULL,&R);
    Sources=Sources&&(I==0?Downloaded:!Downloaded)&&(I!=4||!strcmp(R.Code,"checksum"));
   }}
  }}
  xvalue* V=xrtValueObject();MdoApiValueSetBool(V,"download",Ok);MdoApiValueSetBool(V,"fetch",Fetch);MdoApiValueSetString(V,"code",Report.Code);
  MdoApiValueSetBool(V,"sources",Sources);
  MdoApiValueSetBool(V,"short_retry",ShortRetry);
  xvalue* Tasks=MdoDownloadsList();MdoApiValueSetTake(V,"tasks",&Tasks);QaValue=V;return 0;
}}
XS_RequestResult RequestProc(XS_HttpReq* R){{
 if(xrtStrEqual(R->head->Target,XRT_STR_LITERAL("/qa/run"))){{
  xthread* T=xrtThreadCreate(QaRun,NULL,0);if(T){{xrtThreadWait(T);xrtThreadDestroy(T);}}
  MdoApiContext C={{0}};C.Request=R;MdoApiRequestId(C.RequestId);MdoApiReplySuccessTake(&C,200,QaValue,NULL);QaValue=NULL;return XS_OK;
 }}return QaRequest(R);
}}
''',encoding='utf-8')
        config.write_text(json.dumps(dict(engine=dict(workers=3),services=[dict(enabled=True,**{'class':'http'},name='qa',ip='127.0.0.1',port=port,host_default=dict(enabled=True,name='mdo',path='web',devlang='c',devfile=source.name))])))
        try:
            cases=[('http-connect','',True,False,True,False),('socks5','',True,False,True,False),('http-connect','127.0.0.1',True,False,True,False),('http-connect','',False,False,False,False)]
            if os.name=='nt' and aria.is_file():cases += [('http-connect','',True,True,True,False),('socks5','',True,True,True,False),('http-connect','',False,True,True,True),('socks5','',False,True,True,True)]
            if args.source_only:cases=cases[:1]
            for kind,bypass,ca,managed,expected,plaintext in cases:
                with LocalProxy(('127.0.0.1',0),DownloadProxy) as proxy:
                    proxy.kind=kind;proxy.target_port=plain.server_port if plaintext else publisher.server_port;proxy.connections=0;proxy.errors=[]
                    threading.Thread(target=proxy.serve_forever,daemon=True).start()
                    policy=dict(kind=kind,host='127.0.0.1',port=proxy.server_address[1],user='probe',bypass=bypass,credential=dict(secret_ref='env:MDO_TEST_PROXY_PASSWORD'))
                    (home/'config/settings.json').write_text(json.dumps(dict(schema_version=1,patch=dict(transport=dict(ca_pem_path='certs/ca.pem' if ca else '',proxy=policy)))))
                    (home/'data/downloads/network.bin').unlink(missing_ok=True)
                    assets=home/'data/downloads/assets'
                    if assets.exists():
                        for file in assets.glob('model-*.bin'):file.unlink()
                    env=dict(os.environ,MDO_HOME=str(home),MDO_TEST_PROXY_PASSWORD='bounded-password')
                    if managed:env['QA_ARIA']='1'
                    if plaintext:env['QA_PLAIN']=str(plain.server_port)
                    with (base/'runtime.log').open('wb') as log:
                        process=subprocess.Popen([str(args.host.resolve()),str(config)],cwd=ROOT/'app',env=env,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                        try:
                            wait_ready(port,process)
                            connection=http.client.HTTPConnection('127.0.0.1',port,timeout=30)
                            try:connection.request('GET','/qa/run');response=connection.getresponse();status,raw=response.status,response.read()
                            finally:connection.close()
                            assert status==200,(status,raw)
                            result=json.loads(raw)['data'];assert result['download']==expected and result['fetch']==expected,(kind,managed,result)
                            assert result['sources'],result
                            assert result['short_retry'],result
                            if expected and kind=='http-connect' and not managed and not bypass:
                                task=next(row for row in result['tasks'] if row['identity'].endswith('/loop'))
                                status,_,body=request(port,'POST',f"/api/v1/download-tasks/{task['id']}/retry",body=b'{}',headers={'Content-Type':'application/json'})
                                assert status==200,(status,body)
                                deadline=time.monotonic()+15
                                while time.monotonic()<deadline:
                                    status,_,body=request(port,'GET','/api/v1/download-tasks');assert status==200
                                    resumed=next(row for row in json.loads(body)['data'] if row['id']==task['id'])
                                    if resumed['state']=='failed' and resumed['updated_at']>task['updated_at']:break
                                    time.sleep(.05)
                                else:raise AssertionError('Trusted HTTPS source dispatcher did not finish its retry')
                                assert resumed['error_code']=='redirect',resumed
                            if expected:
                                assert hashlib.sha256((home/'data/downloads/network.bin').read_bytes()).hexdigest()==HASH
                                assert (proxy.connections>0)==(not bypass),(kind,bypass,proxy.connections)
                                engine=result['tasks'][0]['engine'];assert engine==('aria2' if managed and plaintext and kind=='http-connect' else 'native'),result
                            if not ca and not plaintext:assert not result['download'],'Private CA must never be accepted implicitly'
                            print('PASS',kind,'bypass='+bypass,'ca='+str(ca),'aria2='+str(managed),flush=True)
                        except Exception:
                            print((base/'runtime.log').read_text(encoding='utf-8',errors='replace')[-5000:]);raise
                        finally:process.terminate();process.wait(timeout=10);proxy.shutdown()
            print('PASS trusted source redirects, old size boundary and native dispatcher' if args.source_only else 'PASS download TLS trust/rejection, authenticated proxy, bypass and trusted-source dispatcher; managed aria2 cases: '+str(any(row[3] for row in cases)))
        finally:source.unlink(missing_ok=True);config.unlink(missing_ok=True);publisher.shutdown();plain.shutdown()

if __name__=='__main__':main()
