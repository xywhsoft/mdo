"""Real DOM tool grouping, live updates and locale/mobile layout; in place."""
import argparse,json,os,shutil,subprocess,tempfile,threading
from functools import partial
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
ROOT=Path(__file__).resolve().parents[1]
class Handler(SimpleHTTPRequestHandler):
    fixture=ROOT/'tests/fixtures/tool-call-groups-browser.html'
    def log_message(self,*_):pass
    def do_GET(self):
        if self.path.startswith('/lang/'):self.path='/app/web'+self.path
        path=(ROOT/urlsplit(self.path).path.lstrip('/')).resolve()
        if not path.is_relative_to(ROOT/'app/web') and path!=self.fixture:return self.send_error(404)
        return super().do_GET()
def main(fixture=None,output_name='tool-call-groups',locales=('zh-CN','en-US','ru-RU')):
    Handler.fixture=fixture or Handler.fixture
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--browser',type=Path);args=parser.parse_args()
    choices=[args.browser,Path('C:/Program Files/Google/Chrome/Application/chrome.exe'),Path('C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe')]
    browser=next((p for p in choices if p and p.is_file()),None)
    if not browser:
        executable=shutil.which('chromium') or shutil.which('google-chrome')
        if not executable:parser.error('A Chromium browser is required')
        browser=Path(executable)
    proof=ROOT/'.build'/output_name;proof.mkdir(parents=True,exist_ok=True);reports=[]
    with ThreadingHTTPServer(('127.0.0.1',0),partial(Handler,directory=str(ROOT))) as server:
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            for locale in locales:
                for width in (1400,390):
                    with tempfile.TemporaryDirectory(prefix='tool-group-browser-',dir=ROOT/'.build') as profile:
                        screenshot=proof/f'{locale}-{width}.png'
                        command=['node',str(ROOT/'tests/queue_browser_driver.mjs'),str(browser),profile,f'http://127.0.0.1:{server.server_port}/tests/fixtures/{Handler.fixture.name}?locale={locale}',str(width),str(screenshot),'','desktop']
                        result=subprocess.run(command,capture_output=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                        assert result.returncode==0,result.stderr.decode('utf-8',errors='replace')[-3000:]
                        report=json.loads(result.stdout.decode('utf-8'));reports.append(dict(locale=locale,width=width,**report))
                        assert report['passed'],reports[-1]
                        print('PASS',locale,width,flush=True)
        finally:server.shutdown();thread.join(timeout=5)
    (proof/'report.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
if __name__=='__main__':main()
