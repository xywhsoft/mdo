"""Real full-application DOM regression; uses an already available Playwright."""
import argparse,os,subprocess,tempfile
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,wait_ready

def main():
 p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);args=p.parse_args()
 args.evidence.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-ui-') as temporary:
  base=Path(temporary);port=free_port();config=write_site(base,port)
  with (args.evidence/'host.log').open('wb') as log:
   process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,env={**os.environ,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
   try:
    wait_ready(port,process)
    subprocess.run(['node',str(ROOT/'tests/fixtures/workbench-browser.cjs'),f'http://127.0.0.1:{port}',str(args.evidence.resolve())],check=True,timeout=100)
   finally:process.terminate();process.wait(timeout=10)
if __name__=='__main__':main()
