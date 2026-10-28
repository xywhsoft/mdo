"""Actual Android launcher source on Linux: argv/cwd/io/signal and root checks.

Uses native executables and disposable links, never a second service/runtime.
Run through WSL on Windows. This is a POSIX gate, not a real-device claim.
"""
import hashlib,json,os,shutil,signal,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 if os.name=='nt':raise SystemExit('Run this test with Linux Python through WSL')
 sdk=ROOT/'.build/sdk-overlays'/json.loads((ROOT/'deps.lock').read_text())['sdk_overlay']['sha256']
 out=ROOT/'.build/android-execution-linux';out.mkdir(parents=True,exist_ok=True)
 flags=['gcc','-std=c11','-Os','-s','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(sdk/'lib')]
 subprocess.run([*flags,str(ROOT/'tools/android/cli_forwarder.c'),'-lm','-ldl','-pthread','-o',str(out/'forwarder')],check=True)
 with tempfile.TemporaryDirectory(prefix='fixture-',dir=out) as raw:
  base=Path(raw);home=base/'Home 中文';native=base/'apk-native';native.mkdir();folder=home/'data/external-tools';(folder/'bin').mkdir(parents=True);binding=folder/'android/fixture';binding.mkdir(parents=True)
  source=base/'probe.c';source.write_text('#include <stdio.h>\n#include <unistd.h>\n#include <string.h>\n#include <signal.h>\nint main(int n,char** a){if(n>1&&!strcmp(a[1],"wait")){puts("ready");fflush(stdout);for(;;)pause();}char b[4096];puts(getcwd(b,sizeof(b)));for(int i=1;i<n;i++)puts(a[i]);return 37;}\n');subprocess.run(['gcc',str(source),'-o',str(native/'libprobe.so')],check=True)
  (binding/'probe').symlink_to(native/'libprobe.so');entry=folder/'bin/qa';entry.symlink_to(out/'forwarder');state=folder/'state.json'
  def publish(args):state.write_text(json.dumps(dict(commands={'qa':dict(tool_id='fixture',target='external-tools/fixture/probe',args=args)})))
  env={**os.environ,'MDO_CLI_HOME':str(home),'MDO_NATIVE_DIR':str(native)};publish(['prefix 中文','a b'])
  run=subprocess.run([str(entry),'','quote"','line\nbreak'],cwd=base,env=env,capture_output=True,text=True,timeout=10)
  assert run.returncode==37 and run.stdout.splitlines()==[str(base),'prefix 中文','a b','','quote"','line','break'],run
  publish(['wait']);worker=subprocess.Popen([str(entry)],env=env,stdout=subprocess.PIPE,text=True)
  try:
   assert worker.stdout.readline().strip()=='ready';worker.send_signal(signal.SIGTERM);assert worker.wait(timeout=5)==-signal.SIGTERM
  finally:
   if worker.poll() is None:worker.kill();worker.wait()
  state.write_text('{"commands":{}}');assert subprocess.run([str(entry)],env=env,capture_output=True).returncode in (126,127)
  publish([]);(binding/'probe').unlink();(binding/'probe').symlink_to('/bin/echo');assert subprocess.run([str(entry)],env=env,capture_output=True).returncode in (126,127)
  for target in ('external-tools/fixture/../escape','external-tools/other/probe','/bin/echo','external-tools/fixture//probe'):
   state.write_text(json.dumps(dict(commands={'qa':dict(tool_id='fixture',target=target,args=[])})));assert subprocess.run([str(entry)],env=env,capture_output=True).returncode in (126,127)
 print(json.dumps(dict(passed=True,checks=['native Android launcher source compiles','argv prefix/unicode/empty/newline','cwd and exit status','same-PID signals','disabled entry','native root boundary','unsafe registry paths'],bytes=(out/'forwarder').stat().st_size)))
if __name__=='__main__':main()
