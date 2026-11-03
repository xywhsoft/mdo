"""Exercise native platform queries through compact TCC, without OS headers."""
import argparse,json,os,shutil,subprocess,tempfile,time
from pathlib import Path
from test_api_runtime import ROOT,free_port

SOURCE = r'''
#include <xsbase.h>
#include <stdio.h>
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;uint64 Available=123;
    bool Valid=xsDiskAvailableBytes(".",&Available);
    printf("disk_valid=%d available=%llu pid=%llu\n",Valid,(unsigned long long)Available,(unsigned long long)xsCurrentProcessId());
    bool Missing=!xsDiskAvailableBytes("__mdo_missing_directory_473d8__",&Available)&&Available==0;
    Available=123;bool Null=!xsDiskAvailableBytes(NULL,&Available)&&Available==0;
    Available=123;bool Empty=!xsDiskAvailableBytes("",&Available)&&Available==0;
    printf("disk_invalid=%d null=%d empty=%d no_output=%d probe_done=1\n",Missing,Null,Empty,!xsDiskAvailableBytes(".",NULL));
    fflush(stdout);
}
void ServiceUnit(XS_HostInfo* Host){(void)Host;}
'''

def check(host):
    import re
    with tempfile.TemporaryDirectory(prefix='platform-system-',dir=ROOT/'.build') as raw:
        base=Path(raw);(base/'web').mkdir();(base/'probe.c').write_text(SOURCE,encoding='utf-8')
        (base/'xs.json').write_text(json.dumps(dict(services=[dict(enabled=True,**{'class':'http'},name='platform-queries',ip='127.0.0.1',port=free_port(),
            host_default=dict(enabled=True,path='web',devlang='c',devfile='probe.c'))])),encoding='utf-8')
        with (base/'host.log').open('wb') as log:
            process=subprocess.Popen([str(host),'xs.json'],cwd=base,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    output=(base/'host.log').read_text(encoding='utf-8',errors='replace')
                    if 'probe_done=1' in output or process.poll() is not None: break
                    time.sleep(.05)
                match=re.search(r'disk_valid=1 available=(\d+) pid=(\d+)',output)
                assert match and int(match[1])>0 and int(match[2])==process.pid,output
                assert 'disk_invalid=1 null=1 empty=1 no_output=1 probe_done=1' in output,output
                assert abs(shutil.disk_usage(base).free-int(match[1]))<512*1024*1024,output
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    print('PASS native PID and disk bytes through compact TCC; invalid inputs clear output')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,required=True)
    check(parser.parse_args().host.resolve())
