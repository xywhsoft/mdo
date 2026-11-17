"""Verify argument fidelity and parent-death cleanup using a tiny native owner."""
import json, os, subprocess, sys, tempfile, time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_model_supervisor import ROOT, build
def main():
    program=ROOT/('app/generated/native/windows/model-supervisor.exe' if os.name=='nt' else 'app/generated/native/linux/model-supervisor')
    build(program)
    with tempfile.TemporaryDirectory(prefix='model-supervisor-',dir=ROOT/'.build') as raw:
        base=Path(raw); marker=base/'pid.json';child=base/'child.py'
        child.write_text('import json,os,sys,time\nfrom pathlib import Path\nPath(sys.argv[1]).write_text(json.dumps([os.getpid(),sys.argv[2:]]))\ntime.sleep(60)\n',encoding='utf-8')
        values=['space and 中文','quote"inside','trailing slash\\','',"$(no shell)"]
        parent=subprocess.Popen([str(program),sys.executable,str(child),str(marker),*values],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            end=time.monotonic()+10
            while not marker.exists() and time.monotonic()<end:time.sleep(.02)
            pid,args=json.loads(marker.read_text());assert args==values,(args,values)
            if os.name=='nt':
                import ctypes
                kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.restype=ctypes.c_void_p
                handle=kernel.OpenProcess(0x100000,False,pid);assert handle
                kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint];kernel.CloseHandle.argtypes=[ctypes.c_void_p]
                parent.kill();parent.wait(timeout=5)
                assert kernel.WaitForSingleObject(handle,5000)==0,'orphan runtime survived owner death';kernel.CloseHandle(handle)
            else:
                parent.stdin.close();parent.wait(timeout=5)
                try:os.kill(pid,0)
                except ProcessLookupError:pass
                else:raise AssertionError('orphan runtime survived lifeline EOF')
        finally:
            if parent.poll() is None:parent.stdin.close();parent.wait(timeout=5)
            if parent.stdin and not parent.stdin.closed:parent.stdin.close()
        other=subprocess.Popen([str(program),sys.executable,'-c','raise SystemExit(7)'],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        assert other.wait(timeout=10)==7;other.stdin.close()
        print('PASS native supervisor argument quoting, parent death, process tree and exit status')
if __name__=='__main__':main()
