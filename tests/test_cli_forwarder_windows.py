"""Native Windows forwarder behavior, isolated from all installed tools/accounts."""
from pathlib import Path
import ctypes,json,os,shutil,struct,subprocess,sys,tempfile,time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_mdo,build_cli_forwarder

CHILD=r'''
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <io.h>
static void pidfile(const wchar_t* suffix){wchar_t base[32768],name[32768];DWORD n=GetEnvironmentVariableW(L"FORWARD_TEST_RECEIPT",base,32700);if(!n)return;swprintf(name,32768,L"%ls.%ls",base,suffix);FILE* f=_wfopen(name,L"wb");if(f){fprintf(f,"%lu",GetCurrentProcessId());fclose(f);}}
static BOOL WINAPI interrupt(DWORD type){if(type==CTRL_C_EVENT||type==CTRL_BREAK_EVENT){pidfile(L"interrupt");ExitProcess(23);}return FALSE;}
static void integer(unsigned n){fwrite(&n,4,1,stdout);}
static void text(const wchar_t* s){int n=WideCharToMultiByte(CP_UTF8,0,s,-1,NULL,0,NULL,NULL);char* p=malloc(n);WideCharToMultiByte(CP_UTF8,0,s,-1,p,n,NULL,NULL);integer(n-1);fwrite(p,1,n-1,stdout);free(p);}
int wmain(int n,wchar_t**v){
 if(n>1&&!wcscmp(v[1],L"--grandchild")){pidfile(L"grandchild");Sleep(INFINITE);return 0;}
 if(n>1&&!wcscmp(v[1],L"--tree")){wchar_t self[32768],cmd[32768];GetModuleFileNameW(NULL,self,32768);swprintf(cmd,32768,L"\"%ls\" --grandchild",self);STARTUPINFOW s={sizeof(s)};PROCESS_INFORMATION p={0};if(!CreateProcessW(self,cmd,NULL,NULL,FALSE,0,NULL,NULL,&s,&p))return 90;CloseHandle(p.hThread);CloseHandle(p.hProcess);pidfile(L"child");Sleep(INFINITE);return 0;}
 if(n>1&&!wcscmp(v[1],L"--interrupt")){SetConsoleCtrlHandler(NULL,FALSE);SetConsoleCtrlHandler(interrupt,TRUE);pidfile(L"ready");Sleep(INFINITE);return 0;}
 if(n>2&&!wcscmp(v[1],L"--control")){wchar_t receipt[32768],ready[32768];GetEnvironmentVariableW(L"FORWARD_TEST_RECEIPT",receipt,32700);swprintf(ready,32768,L"%ls.ready",receipt);STARTUPINFOW s={sizeof(s)};s.dwFlags=STARTF_USESHOWWINDOW;s.wShowWindow=SW_HIDE;PROCESS_INFORMATION p={0};wchar_t cmd[32768];swprintf(cmd,32768,L"\"%ls\"",v[2]);if(!CreateProcessW(v[2],cmd,NULL,NULL,FALSE,CREATE_NEW_CONSOLE,NULL,NULL,&s,&p))return 91;for(int i=0;i<300&&GetFileAttributesW(ready)==INVALID_FILE_ATTRIBUTES;i++)Sleep(10);FreeConsole();if(!AttachConsole(p.dwProcessId))return 92;SetConsoleCtrlHandler(NULL,TRUE);if(!GenerateConsoleCtrlEvent(CTRL_C_EVENT,0))return 93;DWORD w=WaitForSingleObject(p.hProcess,6000),code=94;FreeConsole();if(w==WAIT_OBJECT_0)GetExitCodeProcess(p.hProcess,&code);else TerminateProcess(p.hProcess,95);CloseHandle(p.hThread);CloseHandle(p.hProcess);return code;}
 _setmode(_fileno(stdout),0x8000);_setmode(_fileno(stdin),0x8000);integer(n-1);for(int i=1;i<n;i++)text(v[i]);wchar_t cwd[32768];GetCurrentDirectoryW(32768,cwd);text(cwd);char b[4096];size_t k;while((k=fread(b,1,sizeof(b),stdin)))fwrite(b,1,k,stdout);fputs("stderr-sentinel",stderr);return 37;
}
'''

def alive(pid):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.restype=ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32];kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    handle=kernel.OpenProcess(0x100000,False,pid)
    if not handle:return False
    try:return kernel.WaitForSingleObject(handle,0)==258
    finally:kernel.CloseHandle(handle)

def run():
    if os.name!='nt':print('SKIP Windows-only executable; Linux wrappers have a separate native regression');return
    lock=build_mdo.load_object(build_mdo.LOCK_PATH);xs=build_mdo.find_xserver(None,lock)
    helper=ROOT/'.build/cli-forwarder-development/forwarder.exe';receipt=build_cli_forwarder.build(xs,helper)
    with tempfile.TemporaryDirectory(prefix='forwarder-native-',dir=ROOT/'.build') as temp:
        base=Path(temp);home=base/'home 中文';folder=home/'data/cli-tools';bin=folder/'bin';bin.mkdir(parents=True);(home/'installed').mkdir()
        source=base/'child.c';source.write_text(CHILD,encoding='utf-8');target=home/'installed/child.exe'
        subprocess.run(['gcc','-Os','-s','-municode',str(source),'-o',str(target)],check=True)
        for name in ('probe','other','tree','signal','missing','escape','loop'):shutil.copy2(helper,bin/(name+'.exe'))
        os.link(bin/'probe.exe',bin/'linked.exe')
        commands={n:{'target':'installed/child.exe','args':[]} for n in ('probe','other','linked')}
        commands.update(tree={'target':'installed/child.exe','args':['--tree']},signal={'target':'installed/child.exe','args':['--interrupt']},missing={'target':'installed/absent.exe'},escape={'target':'../outside.exe'},loop={'target':'data/cli-tools/bin/loop.exe'})
        config=folder/'state.json';config.write_text(json.dumps({'commands':commands}),encoding='utf-8')
        cwd=base/'work space';cwd.mkdir();args=['','a b','q"x','中文','tail\\','%PATH%','a&b','line\nbreak']
        def invoke(name,extra=args,**options):return subprocess.run([str(bin/(name+'.exe')),*extra],cwd=cwd,input=b'input\x00\r\n',capture_output=True,timeout=10,**options)
        def check(p,expected):
            assert p.returncode==37,(p.returncode,p.stderr);assert p.stderr==b'stderr-sentinel'
            data=p.stdout;off=0
            def number():
                nonlocal off
                n=struct.unpack_from('<I',data,off)[0];off+=4;return n
            def text():
                nonlocal off
                n=number();s=data[off:off+n].decode('utf-8');off+=n;return s
            assert number()==len(expected);assert [text() for _ in expected]==expected;assert Path(text())==cwd;assert data[off:]==b'input\x00\r\n'
        for name in ('probe','other','linked'):check(invoke(name),args)
        commands['other']['args']=['prefix','中文前缀',''];config.write_text(json.dumps({'commands':commands}),encoding='utf-8');check(invoke('other'),commands['other']['args']+args)
        for name in ('missing','escape','loop'):assert invoke(name,[]).returncode==126
        assert invoke('probe',[],env={**os.environ,'MDO_CLI_FORWARD_DEPTH':'4'}).returncode==126
        config.write_text('{"commands":{"probe":{},"probe":{}}}');assert invoke('probe',[]).returncode==126
        config.write_text(json.dumps({'commands':commands}),encoding='utf-8')
        marker=base/'tree-receipt';env={**os.environ,'FORWARD_TEST_RECEIPT':str(marker)}
        process=subprocess.Popen([str(bin/'tree.exe')],cwd=cwd,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        ids=[]
        try:
            deadline=time.monotonic()+8
            while time.monotonic()<deadline and not marker.with_suffix('.grandchild').exists():time.sleep(.02)
            ids=[int(marker.with_suffix('.'+part).read_text()) for part in ('child','grandchild')];assert all(alive(pid) for pid in ids)
            process.terminate();process.wait(5)
            deadline=time.monotonic()+5
            while time.monotonic()<deadline and any(alive(pid) for pid in ids):time.sleep(.02)
            assert not any(alive(pid) for pid in ids),'launcher death must terminate the complete child tree'
        finally:
            if process.poll() is None:process.kill();process.wait()
            process.stderr.close()
        marker=base/'interrupt-receipt'
        control=subprocess.run([str(target),'--control',str(bin/'signal.exe')],cwd=cwd,env={**os.environ,'FORWARD_TEST_RECEIPT':str(marker)},capture_output=True,timeout=15)
        assert control.returncode==23,(control.returncode,control.stderr);assert marker.with_suffix('.interrupt').exists()
        moved=base/'moved location';shutil.copytree(home,moved);bin=moved/'data/cli-tools/bin';check(invoke('probe'),args)
        report={'passed':True,'bytes':receipt['bytes'],'checks':['relative target and independent cwd','empty/unicode/quote/backslash/metacharacter args','prefix args and hardlinked aliases','binary stdin/stdout and exact stderr/exit status','missing/escape/recursive targets and duplicate JSON reject','parent death contains descendants','Ctrl-C forwarding with hidden console','Home relocation']}
        out=ROOT/'.build/cli-forwarder-development/windows-regression.json';out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':run()
