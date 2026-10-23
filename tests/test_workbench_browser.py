"""Real full-application DOM regression; uses an already available Playwright."""
import argparse,os,subprocess,tempfile,time
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,wait_ready

def main():
 p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);modes=p.add_mutually_exclusive_group();modes.add_argument('--files',action='store_true');modes.add_argument('--changes',action='store_true');modes.add_argument('--terminal',action='store_true');modes.add_argument('--start',action='store_true');args=p.parse_args()
 args.evidence.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-ui-') as temporary:
  base=Path(temporary);port=free_port();config=write_site(base,port)
  if args.changes:
   repo=base/'changes-fixture';repo.mkdir()
   def git(*argv):subprocess.run(['git','-c','user.name=Workbench QA','-c','user.email=workbench@example.invalid',*argv],cwd=repo,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   git('init','-b','master')
   (repo/'tracked.js').write_text('const answer = 1;\nconsole.log(answer);\n',encoding='utf-8')
   (repo/'large.txt').write_text(''.join(f'old line {i}\n' for i in range(2600)),encoding='utf-8')
   (repo/'binary.dat').write_bytes(b'old\0binary');git('add','--all');git('commit','-m','baseline');git('branch','baseline')
   (repo/'branch.txt').write_text('committed\n');git('add','--all');git('commit','-m','branch')
   (repo/'recreated.txt').write_text('before recreation\n');git('add','--all');git('commit','-m','recreation baseline')
   git('rm','recreated.txt');(repo/'recreated.txt').write_text('after recreation\n')
   (repo/'tracked.js').write_text('const answer = 2; // staged\nconsole.log(answer);\n',encoding='utf-8');git('add','tracked.js')
   (repo/'tracked.js').write_text('const answer = 3; // working\nconsole.log(answer);\n',encoding='utf-8')
   (repo/'large.txt').write_text(''.join(f'new line {i}\n' for i in range(2600)),encoding='utf-8')
   (repo/'binary.dat').write_bytes(b'new\0binary');(repo/'小说.txt').write_text('第一行\n',encoding='utf-8')
   (repo/'nested').mkdir()
   for at in range(450):(repo/'nested'/f'new-{at:04d}.txt').write_text('new\n')
  with (args.evidence/'host.log').open('wb') as log:
   # Do not make Windows PowerShell auto-import the harness pwsh 7 modules.
   # This is a private, clean shell fixture; production keeps the user's env.
   environment={key:value for key,value in os.environ.items() if not (args.terminal or args.start) or key.lower()!='psmodulepath'}
   process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,env={**environment,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
   try:
    wait_ready(port,process)
    fixture='workbench-start-browser.cjs' if args.start else 'workbench-terminal-browser.cjs' if args.terminal else 'workbench-changes-browser.cjs' if args.changes else 'workbench-files-browser.cjs' if args.files else 'workbench-browser.cjs'
    subprocess.run(['node',str(ROOT/'tests/fixtures'/fixture),f'http://127.0.0.1:{port}',str(args.evidence.resolve()),*([str(repo)] if args.changes else [])],check=True,timeout=150)
   finally:process.terminate();process.wait(timeout=10);time.sleep(1)
if __name__=='__main__':main()
