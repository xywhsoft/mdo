"""Production md4c service and real chat DOM, including native resource bytes."""
import argparse,http.client,json,os,subprocess,tempfile,threading
from functools import partial
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
from markdown_runtime_support import ROOT,markdown_host,markdown_session

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--packed',type=Path,required=True);args=parser.parse_args()
    proof=ROOT/'.build/markdown-browser';proof.mkdir(exist_ok=True);reports=[]
    with markdown_host(args.packed) as (port,call,home):
        owner=markdown_session(port,home);retry={}
        class Handler(SimpleHTTPRequestHandler):
            def log_message(self,*_):pass
            def proxy(self):
                length=int(self.headers.get('Content-Length','0'));body=self.rfile.read(length) if length else None
                headers={k:v for k,v in self.headers.items() if k.lower() in ('content-type','x-mdo-write-token','if-match')}
                connection=http.client.HTTPConnection('127.0.0.1',port,timeout=8)
                try:
                    connection.request(self.command,self.path,body=body,headers=headers);response=connection.getresponse();data=response.read()
                    self.send_response(response.status)
                    for key,value in response.getheaders():
                        if key.lower() in ('content-type','x-mdo-write-token','etag'):self.send_header(key,value)
                    self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
                finally:connection.close()
            def do_POST(self):self.proxy()
            def do_GET(self):
                path=urlsplit(self.path).path
                if path.startswith('/api/v1/'):return self.proxy()
                if path=='/__qa/info':
                    data=json.dumps(owner).encode();kind='application/json'
                elif path=='/__qa/retry.png':
                    count=retry.get(self.path,0);retry[self.path]=count+1
                    if not count:return self.send_error(404)
                    data=Path(owner['image']).read_bytes();kind='image/png'
                else:
                    if path.startswith('/lang/'):self.path='/app/web'+self.path;path=urlsplit(self.path).path
                    if path.startswith('/app/web/'):
                        self.path=self.path[len('/app/web'):];return self.proxy()
                    if path!='/tests/fixtures/markdown-md4c-browser.html':return self.proxy()
                    return super().do_GET()
                self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        with ThreadingHTTPServer(('127.0.0.1',0),partial(Handler,directory=str(ROOT))) as server:
            thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
            try:
                for locale in ('zh-CN','en-US','ru-RU'):
                    for width in (1400,390):
                        with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='md4c-browser-') as profile:
                            process=subprocess.run(['node',str(ROOT/'tests/queue_browser_driver.mjs'),'C:/Program Files/Google/Chrome/Application/chrome.exe',profile,
                                f'http://127.0.0.1:{server.server_port}/tests/fixtures/markdown-md4c-browser.html?locale={locale}',str(width),str(proof/f'{locale}-{width}.png'),'','desktop','18000'],capture_output=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                            assert process.returncode==0,process.stderr.decode(errors='replace')[-3000:]
                            report=json.loads(process.stdout.decode('utf-8'));reports.append({'locale':locale,'width':width,**report})
                            (proof/'report.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
                            assert report['passed'],reports[-1]
                            print('PASS Markdown',locale,width,flush=True)
                with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='md4c-workbench-') as profile:
                    process=subprocess.run(['node',str(ROOT/'tests/queue_browser_driver.mjs'),'C:/Program Files/Google/Chrome/Application/chrome.exe',profile,
                        f'http://127.0.0.1:{server.server_port}/','1400',str(proof/'workbench.png'),str(ROOT/'tests/fixtures/markdown-workbench-browser.js'),'desktop'],
                        capture_output=True,timeout=55,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                    assert process.returncode==0,process.stderr.decode(errors='replace')[-3000:]
                    report=json.loads(process.stdout.decode('utf-8'));reports.append({'kind':'workbench',**report})
                    (proof/'report.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
                    assert report['passed'],reports[-1]
                    print('PASS full application Markdown file link and workbench line navigation',flush=True)
            finally:server.shutdown();thread.join(timeout=5)

if __name__=='__main__':main()
