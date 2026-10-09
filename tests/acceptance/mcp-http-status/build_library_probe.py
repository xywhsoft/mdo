"""Build the upstream MCP probes against the same xhttp/xrt core as xs."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xrt-root', type=Path, required=True)
    parser.add_argument('--xs-root', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    xrt, xs, stage = args.xrt_root.resolve(), args.xs_root.resolve(), args.directory.resolve()
    stage.mkdir(parents=True, exist_ok=False)
    shutil.copytree(xrt / 'extlibs/xwork', stage / 'extlibs/xwork',
                    ignore=shutil.ignore_patterns('dev', 'build', '__pycache__'))
    for name in ('xllm', 'xllm-session', 'xhttp'):
        shutil.copytree(xs / 'lib' / name, stage / 'extlibs' / name)
    (stage / 'single').mkdir()
    shutil.copy2(xs / 'lib/xrt.h', stage / 'single/xrt.h')
    exe = stage / ('test_xwork.exe' if os.name == 'nt' else 'test_xwork')
    command = [args.cc, '-std=c11', '-O0', '-ffunction-sections', '-fdata-sections']
    for name in ('single', 'extlibs/xllm', 'extlibs/xllm-session'):
        command.extend(['-I', str(stage / name)])
    command.extend([str(stage / 'extlibs/xwork/tests/test_xwork.c'),
                    str(stage / 'extlibs/xwork/xwork-xrt.c')])
    if os.name == 'nt':
        command.extend(['-Wl,--gc-sections', '-lws2_32', '-lbcrypt', '-lcrypt32',
                        '-liphlpapi', '-lshell32', '-lole32', '-luuid'])
    else:
        command.extend(['-pthread', '-lm', '-ldl'])
    command.extend(['-o', str(exe)])
    subprocess.run(command, check=True)
    for test in ('test_mcp_http_errors.py', 'test_mcp_http_runtime.py'):
        subprocess.run([sys.executable, str(xrt / 'extlibs/xwork/tests' / test), str(exe)], check=True)
    print('PASS upstream HTTP failures/recovery and normal protocol probes')


if __name__ == '__main__': main()
