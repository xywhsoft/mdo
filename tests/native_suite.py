"""Run current directory contracts serially against one owned product host."""
from contextlib import contextmanager
import json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
from test_api_runtime import ROOT,free_port,wait_ready
from markdown_runtime_support import markdown_host

@contextmanager
def product_host(host=None,packed=None):
    if packed:
        with markdown_host(packed) as (port,_call,home): yield port,home
        return
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='native-suite-') as raw:
        base=Path(raw);site=base/'site';shutil.copytree(ROOT/'app',site);home=base/'home';port=free_port()
        from fixture_offline import isolate_background_network
        isolate_background_network(site)
        (site/'xs.json').write_text(json.dumps(dict(services=[dict(enabled=True,**{'class':'http'},name='native-suite',ip='127.0.0.1',port=port,
            host_default=dict(enabled=True,path='web',devlang='c',devfile='generated/mdo_unity.c'))])),encoding='utf-8')
        with (base/'host.log').open('wb') as log:
            process=subprocess.Popen([str(host),'xs.json','--','--home',str(home)],cwd=site,stdout=log,stderr=log,
                env=dict(os.environ,USE_WEBVIEW='0'),creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                wait_ready(port,process);yield port,home
            except BaseException:
                print((base/'host.log').read_text(encoding='utf-8',errors='replace')[-6000:]);raise
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()

def run(names,host=None,packed=None):
    with product_host(host,packed) as (port,home):
        for name in names:
            args=[sys.executable,str(ROOT/'tests'/(name+'.py')),'--url',f'http://127.0.0.1:{port}','--home',str(home)]
            if name in ('test_mcp_directory_native','test_mcp_automatic_native'):args+=['--cc',shutil.which('gcc') or 'gcc']
            print('CONTRACT',name,flush=True)
            subprocess.run(args,cwd=ROOT,env=dict(os.environ,PYTHONIOENCODING='utf-8'),check=True,timeout=180)
