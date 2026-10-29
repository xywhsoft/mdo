"""Compile native process-output storage checks using the existing SDK/compiler.

Uses a small disposable file directory in this project, no additional runtime,
service, account or model. The production SDK implementation is compiled as-is.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path)
    p.add_argument('--cc', default='gcc')
    args = p.parse_args()
    lock = json.loads((ROOT/'deps.lock').read_text(encoding='utf-8'))
    sdk = (args.sdk or ROOT/'.build/sdk-overlays'/lock['sdk_overlay']['sha256']).resolve()
    compiler = shutil.which(args.cc) or args.cc
    with tempfile.TemporaryDirectory(prefix='process-output-', dir=ROOT/'.build') as raw:
        base = Path(raw); (base/'files').mkdir(); binary = base/('probe.exe' if os.name=='nt' else 'probe')
        command = [compiler, '-std=c11', '-O1', '-fwhole-program', '-ffunction-sections', '-fdata-sections',
            '-Wl,--gc-sections', '-I'+str(sdk/'lib/xwork'), '-I'+str(sdk/'lib'),
            '-I'+str(sdk/'lib/xrtshim'), '-I'+str(sdk/'lib/xhttp/include'),
            '-I'+str(sdk/'lib/xllm'), '-I'+str(sdk/'lib/xllm-session'),
            str(ROOT/'tests/fixtures/process-output-store.c'), '-o', str(binary)]
        if os.name!='nt':command.insert(1,'-D_GNU_SOURCE')
        command += ['-lws2_32','-lbcrypt','-lcrypt32','-liphlpapi','-lwinmm','-ladvapi32','-lshell32','-lshlwapi','-lole32','-luuid'] if os.name=='nt' else ['-pthread','-ldl','-lm']
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary), str(base/'files')], cwd=ROOT, check=True, timeout=30)

if __name__=='__main__': main()
