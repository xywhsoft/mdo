#!/usr/bin/env python3
"""Build the Windows-only portable command forwarder using mdo's locked XRT."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess

ROOT=Path(__file__).resolve().parents[1]
def build(xserver,output,cc='gcc'):
    source=ROOT/'tools/cli_forwarder.c';header=Path(xserver)/'lib/xrt.h';output=Path(output)
    compiler=shutil.which(cc) or cc
    flags=['-std=c11','-Os','-s','-ffunction-sections','-fdata-sections','-municode','-Wl,--gc-sections','-Wl,--no-insert-timestamp']
    command=[compiler,*flags,'-I'+str(header.parent),str(source),'-o',str(output),'-lshell32','-luserenv','-ladvapi32']
    fingerprint=hashlib.sha256(source.read_bytes()+header.read_bytes()+json.dumps(command).encode()+subprocess.check_output([compiler,'--version'])).hexdigest()
    receipt=output.with_suffix('.build.json')
    if output.is_file() and receipt.is_file():
        previous=json.loads(receipt.read_text())
        if previous.get('fingerprint')==fingerprint and previous.get('sha256')==hashlib.sha256(output.read_bytes()).hexdigest():return previous
    output.parent.mkdir(parents=True,exist_ok=True);temporary=output.with_suffix('.pending.exe');command[command.index('-o')+1]=str(temporary)
    try:
        subprocess.run(command,check=True);temporary.replace(output)
    finally:temporary.unlink(missing_ok=True)
    result={'fingerprint':fingerprint,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'bytes':output.stat().st_size,'compiler':compiler,'flags':flags}
    receipt.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');return result

if __name__=='__main__':
    import build_mdo
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--xserver-root',type=Path);p.add_argument('--output',type=Path,default=ROOT/'.build/cli-forwarder/forwarder.exe');p.add_argument('--cc',default='gcc');a=p.parse_args()
    lock=build_mdo.load_object(build_mdo.LOCK_PATH);xs=build_mdo.find_xserver(a.xserver_root,lock);build_mdo.verify_dependencies(xs,lock)
    print(json.dumps(build(xs,a.output,a.cc),indent=2))
