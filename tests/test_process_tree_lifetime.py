"""Native owned-tree cleanup after leader exit, Windows and POSIX."""
import argparse,json,os,signal,subprocess
from pathlib import Path
from locked_sdk import sdk_root
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',default='gcc');a=p.parse_args();out=ROOT/'.build/process-tree-lifetime';out.mkdir(parents=True,exist_ok=True);windows=os.name=='nt';exe=out/('probe.exe' if windows else 'probe')
 command=[a.cc,'-std=c11','-Os','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(sdk_root()/'lib'),str(ROOT/'tests/fixtures/process_tree_lifetime.c'),'-o',str(exe)]
 command+=['-lshell32','-luserenv','-ladvapi32'] if windows else ['-pthread','-lm','-ldl'];subprocess.run(command,check=True)
 results=[]
 for mode in ('tree','run'):
  worker=subprocess.Popen([str(exe),mode],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  try:
   stdout,stderr=worker.communicate(timeout=8);assert worker.returncode==0,(mode,worker.returncode,stdout,stderr);results.append(dict(mode=mode,output=stdout.strip()))
  except subprocess.TimeoutExpired:
   worker.kill();stdout,stderr=worker.communicate()
   if not windows:
    for line in stdout.splitlines():
     if line.startswith('root='):
      try:os.killpg(int(line[5:]),signal.SIGKILL)
      except ProcessLookupError:pass
   raise AssertionError('Process cleanup hung after leader exit: '+stdout)
 (out/('windows.json' if windows else 'linux.json')).write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(dict(passed=True,platform=os.name,results=results)))
if __name__=='__main__':main()
