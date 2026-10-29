"""Exercise real native exec/spawn/status/wait and file tools without model calls."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
from test_model_runtime import ROOT, run_probe
from test_api_runtime import free_port

def output_bytes() -> bytes:
    return b'MDO_STDOUT_BEGIN\n'+b''.join(f'line {i:06d}: fixture output for paging and searching\n'.encode() for i in range(70000))+b'MDO_STDOUT_END\n'

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    target=p.add_mutually_exclusive_group(required=True)
    target.add_argument('--host',type=Path)
    target.add_argument('--native',action='store_true',help='Compile a small standalone probe using the same sources')
    p.add_argument('--sdk',type=Path)
    p.add_argument('--cc',default='gcc')
    p.add_argument('--status-tool',default='status')
    p.add_argument('--legacy-output-root',action='store_true',help='Exercise relative artifact directories without an ArtifactStore')
    args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='process-output-runtime-',dir=ROOT/'.build') as raw:
        base=Path(raw);site=base/'site';(site/'web').mkdir(parents=True);(base/'workspace/sub').mkdir(parents=True);(base/'artifacts').mkdir()
        (site/'web/index.html').write_text('process output QA',encoding='utf-8')
        (site/'probe.c').write_bytes((ROOT/'tests/fixtures/process-output-runtime.c').read_bytes())
        (site/'tool-catalog.h').write_bytes((ROOT/'app/include/mdo/tool_catalog.h').read_bytes())
        (site/'xs.json').write_text(json.dumps({'services':[{'enabled':True,'class':'http','name':'process-output-qa','ip':'127.0.0.1','port':free_port(),'host_default':{'enabled':True,'name':'process-output','path':'web','devlang':'c','devfile':'probe.c'}}]}),encoding='utf-8')
        payload=base/'expected.bin';payload.write_bytes(output_bytes())
        child=base/'child.py';child.write_text('''import sys,time,json
from pathlib import Path
mode=sys.argv[1]
if mode=='short':print('short-out');print('short-err',file=sys.stderr)
elif mode=='failure':print('failure-out');print('failure-err',file=sys.stderr);sys.exit(7)
elif mode in ('large','background'):
    data=Path(__file__).with_name('expected.bin').read_bytes()
    for i in range(0,len(data),16384):
        sys.stdout.buffer.write(data[i:i+16384]);sys.stdout.buffer.flush()
    sys.stderr.buffer.write(b'fixture-error\\n'*1000);sys.stderr.buffer.flush()
    if mode=='background':time.sleep(.1)
elif mode=='long':
    print('running',flush=True)
    while True:time.sleep(.1)
elif mode=='input':
    print(sys.stdin.read(),flush=True)
elif mode=='descendant':
    import subprocess
    subprocess.Popen([sys.executable,'-u',__file__,'long'])
    print('leader-exited',flush=True)
elif mode=='mcp':
    for line in sys.stdin:
        request=json.loads(line)
        if 'id' not in request:continue
        method=request['method']
        if method=='initialize':result={'protocolVersion':request['params']['protocolVersion'],'capabilities':{'tools':{}},'serverInfo':{'name':'output-parser-fixture','version':'1'}}
        elif method=='tools/list':result={'tools':[{'name':'echo','description':'Synthetic echo','inputSchema':{'type':'object','properties':{},'additionalProperties':False}}]}
        elif method=='tools/call':result={'content':[{'type':'text','text':'mcp-ok'}]}
        else:result={}
        print(json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result}),flush=True)
''',encoding='utf-8')
        env={'MDO_OUTPUT_PYTHON':sys.executable,'MDO_OUTPUT_CHILD':str(child),'MDO_OUTPUT_WORKSPACE':str(base/'workspace'),'MDO_OUTPUT_ARTIFACTS':str(base/'artifacts'),'MDO_OUTPUT_STATUS_TOOL':args.status_tool}
        if args.legacy_output_root:env['MDO_OUTPUT_LEGACY_ROOT']='1'
        if args.native:
            lock=json.loads((ROOT/'deps.lock').read_text(encoding='utf-8'))
            sdk=(args.sdk or ROOT/'.build/sdk-overlays'/lock['sdk_overlay']['sha256']).resolve()
            unit=site/'main.c';binary=base/('probe.exe' if os.name=='nt' else 'probe')
            unit.write_text('#define XRT_IMPLEMENTATION\n#include "xwork-xrt-roots.h"\n#include "xllm.c"\n#include "xllm-session.c"\n#include "xwork.c"\n#define XS_SDK_XSBASE_H\ntypedef struct XS_HostInfo XS_HostInfo;\n#include "probe.c"\nint main(void){ServiceInit(NULL);return 0;}\n',encoding='utf-8')
            # Mirror the host's modular xhttp build; its older amalgamation
            # contains another XRT implementation and must not be linked.
            modules={m['name']:m for m in json.loads((sdk/'lib/xhttp/config/modules.json').read_text(encoding='utf-8'))['modules']}
            selected=set()
            def select(name):
                if name in selected or name not in modules:return
                selected.add(name)
                for dependency in modules[name].get('depends',[]):select(dependency)
            roots=(sdk/'lib/xwork/xwork-xrt-roots.h').read_text(encoding='utf-8')
            for name in re.findall(r'#define XHTTP_MODULE_(\w+)',roots):select(name.lower())
            sources=sorted({sdk/'lib/xhttp'/source.split('extlibs/xhttp/',1)[1] for name in selected for source in modules[name].get('sources',[])})
            prelude=site/'http-roots.h';prelude.write_text('#include "xwork-xrt-roots.h"\n#include <xhttp/features.h>\n#include "xrt.h"\n',encoding='utf-8')
            (site/'xrt_decl.h').write_text('#include "xrt.h"\n',encoding='utf-8')
            command=[args.cc,'-D_GNU_SOURCE','-std=c11','-O1','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
            command+=['-I'+str(sdk/path) for path in ('lib','lib/xwork','lib/xllm','lib/xllm-session','lib/xhttp/include','lib/xrtshim','src/sdk')]
            command+=['-I'+str(site),'-include',str(prelude),*['-D'+modules[name]['feature']+'=1' for name in sorted(selected)],str(unit),*map(str,sources),'-o',str(binary)]
            command+=['-lws2_32','-lbcrypt','-lcrypt32','-liphlpapi','-lwinmm','-ladvapi32','-lshell32','-lshlwapi','-lole32','-luuid'] if os.name=='nt' else ['-pthread','-ldl','-lm']
            subprocess.run(command,check=True)
            done=subprocess.run([str(binary)],capture_output=True,env={**os.environ,**env},timeout=25)
            result=done.stdout.decode('utf-8',errors='replace')+done.stderr.decode('utf-8',errors='replace')
            assert done.returncode==0,result[-6000:]
        else:
            result=run_probe(args.host.resolve(),site,base/'home',env,timeout_seconds=25)
        lines=[line for line in result.splitlines() if line.startswith('process_output_report=')]
        assert lines and 'probe_done=1' in result,result[-6000:]
        report=json.loads(lines[-1].split('=',1)[1]);failed=[name for name,ok in report['checks'].items() if not ok]
        assert not failed,(failed,report,result[-6000:])
        expected=hashlib.sha256(payload.read_bytes()).hexdigest()
        for key in ('exec_stdout','spawn_stdout'):
            file=Path(report[key]);assert file.is_file() and hashlib.sha256(file.read_bytes()).hexdigest()==expected,(key,report)
        assert Path(report['exec_stderr']).read_bytes()==b'fixture-error\n'*1000
        assert Path(report['explicit_stdout']).resolve()==(base/'workspace/sub/chosen-out.log').resolve()
        assert Path(report['explicit_stdout']).read_bytes() in (b'short-out\n',b'short-out\r\n')
        assert not (base/'workspace/sub/denied.log').exists()
        print(f"Native process tools: PASS ({len(report['checks'])} checks; byte-exact long stdout/stderr and post-release logs)")

if __name__=='__main__':main()
