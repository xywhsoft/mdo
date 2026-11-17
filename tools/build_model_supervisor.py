"""Build a small platform-native model process-tree owner; no SDK/app copy."""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def build(output, cc='gcc', windows=None):
    windows = os.name == 'nt' if windows is None else windows
    output = Path(output); source = ROOT/'tools/model_supervisor.c'; compiler = shutil.which(cc) or cc
    flags = ['-std=c11','-Os','-s','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
    if windows: flags += ['-municode','-Wl,--no-insert-timestamp']
    command = [compiler,*flags,str(source),'-o',str(output),*(['-lshell32'] if windows else [])]
    fingerprint = hashlib.sha256(source.read_bytes()+json.dumps(command).encode()+subprocess.check_output([compiler,'--version'])).hexdigest()
    receipt = output.with_suffix('.build.json')
    if output.is_file() and receipt.is_file():
        data=json.loads(receipt.read_text());
        if data.get('fingerprint')==fingerprint and data.get('sha256')==hashlib.sha256(output.read_bytes()).hexdigest():return data
    output.parent.mkdir(parents=True,exist_ok=True);subprocess.run(command,check=True)
    data=dict(fingerprint=fingerprint,sha256=hashlib.sha256(output.read_bytes()).hexdigest(),bytes=output.stat().st_size)
    receipt.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8');return data
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',default='gcc');p.add_argument('--output',type=Path,default=ROOT/('app/generated/native/windows/model-supervisor.exe' if os.name=='nt' else 'app/generated/native/linux/model-supervisor'));a=p.parse_args();print(json.dumps(build(a.output,a.cc)))
