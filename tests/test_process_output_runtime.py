"""Exercise real native exec/spawn/status/wait and file tools without model calls."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from test_model_runtime import ROOT, run_probe
from test_api_runtime import free_port

def output_bytes() -> bytes:
    return b'MDO_STDOUT_BEGIN\n'+b''.join(f'line {i:06d}: fixture output for paging and searching\n'.encode() for i in range(70000))+b'MDO_STDOUT_END\n'

def main() -> None:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=Path,required=True)
    p.add_argument('--status-tool',default='status')
    args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='process-output-runtime-',dir=ROOT/'.build') as raw:
        base=Path(raw);site=base/'site';(site/'web').mkdir(parents=True);(base/'workspace/sub').mkdir(parents=True);(base/'artifacts').mkdir()
        (site/'web/index.html').write_text('process output QA',encoding='utf-8')
        (site/'probe.c').write_bytes((ROOT/'tests/fixtures/process-output-runtime.c').read_bytes())
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
        result=run_probe(args.host.resolve(),site,base/'home',{'MDO_OUTPUT_PYTHON':sys.executable,'MDO_OUTPUT_CHILD':str(child),'MDO_OUTPUT_WORKSPACE':str(base/'workspace'),'MDO_OUTPUT_ARTIFACTS':str(base/'artifacts'),'MDO_OUTPUT_STATUS_TOOL':args.status_tool},timeout_seconds=15)
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
        print(f"Native process tools: PASS ({len(report['checks'])} checks; byte-exact long stdout/stderr and post-release logs)")

if __name__=='__main__':main()
