"""Real native windows, private Home only; no account or model requests."""
import argparse,ctypes as c,json,os,subprocess,tempfile,time
from ctypes import wintypes as w
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);parser.add_argument('--evidence',type=Path,required=True);args=parser.parse_args()
    if os.name!='nt':raise SystemExit('Windows only')
    args.evidence.mkdir(parents=True,exist_ok=True)
    user=c.WinDLL('user32',use_last_error=True)
    user.SetThreadDpiAwarenessContext.argtypes=[c.c_void_p];user.SetThreadDpiAwarenessContext.restype=c.c_void_p;user.SetThreadDpiAwarenessContext(c.c_void_p(-4))
    user.EnumWindows.argtypes=[c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM),w.LPARAM];user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
    for method in ['IsWindowVisible','IsZoomed','IsIconic']:getattr(user,method).argtypes=[w.HWND]
    user.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)];user.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
    user.ShowWindow.argtypes=[w.HWND,c.c_int];user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    user.MonitorFromWindow.argtypes=[w.HWND,w.DWORD];user.MonitorFromWindow.restype=w.HMONITOR
    class Monitor(c.Structure):_fields_=[('size',w.DWORD),('monitor',w.RECT),('work',w.RECT),('flags',w.DWORD)]
    user.GetMonitorInfoW.argtypes=[w.HMONITOR,c.POINTER(Monitor)]
    def wait(check,description,seconds=20):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=check()
            if value:return value
            time.sleep(.05)
        raise AssertionError(description)
    def find(pid):
        found=[]
        @user.EnumWindows.argtypes[0]
        def visit(hwnd,_):
            process=w.DWORD();user.GetWindowThreadProcessId(hwnd,c.byref(process))
            if process.value==pid and user.IsWindowVisible(hwnd):found.append(hwnd)
            return True
        user.EnumWindows(visit,0);return found[0] if found else None
    def rect(hwnd):
        value=w.RECT();assert user.GetWindowRect(hwnd,c.byref(value));return [value.left,value.top,value.right-value.left,value.bottom-value.top]
    with tempfile.TemporaryDirectory(prefix='window-state-',dir=ROOT/'.build') as temporary:
        base=Path(temporary);config=write_site(base,free_port());home=base/'窗口 Home';state=home/'data/ui/main-window.state';wrong=base/'ignored-env-home'
        settings=json.loads(config.read_text(encoding='utf-8'));service=settings['services'][0];service['class']='app';service['window']=json.loads((ROOT/'app/xs.json').read_text(encoding='utf-8'))['services'][0]['window'];service['window']['title']='Mdo window placement QA';service['window']['install_fallback']='headless';config.write_text(json.dumps(settings),encoding='utf-8')
        process=None;logs=[]
        def start():
            nonlocal process
            log=(args.evidence/('launch-'+str(len(logs))+'.log')).open('wb');logs.append(log)
            process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=base,env={**os.environ,'MDO_HOME':str(wrong)},stdout=log,stderr=log)
            window=wait(lambda:find(process.pid),'Native window did not open');time.sleep(.5);return window
        def close(window):
            user.PostMessageW(window,0x10,0,0);process.wait(timeout=15);time.sleep(.2)
        try:
            hwnd=start();monitor=Monitor();monitor.size=c.sizeof(monitor);assert user.GetMonitorInfoW(user.MonitorFromWindow(hwnd,2),c.byref(monitor));work=monitor.work
            target=[work.left+40,work.top+40,min(900,work.right-work.left-80),min(650,work.bottom-work.top-80)]
            assert user.SetWindowPos(hwnd,None,*target,0x0014);wait(lambda:state.exists(),'Placement was not debounced to disk');time.sleep(.5)
            normal=rect(hwnd);assert normal==target,(normal,target);close(hwnd)
            hwnd=start();assert rect(hwnd)==normal,(rect(hwnd),normal);assert not wrong.exists();user.ShowWindow(hwnd,3);wait(lambda:user.IsZoomed(hwnd),'Maximize failed');close(hwnd)
            hwnd=start();assert user.IsZoomed(hwnd),'Maximized state lost';user.ShowWindow(hwnd,9);wait(lambda:not user.IsZoomed(hwnd),'Restore failed');assert rect(hwnd)==normal,(rect(hwnd),normal)
            user.ShowWindow(hwnd,6);wait(lambda:user.IsIconic(hwnd),'Minimize failed');close(hwnd)
            hwnd=start();assert not user.IsIconic(hwnd) and not user.IsZoomed(hwnd);assert rect(hwnd)==normal
            user.ShowWindow(hwnd,3);wait(lambda:user.IsZoomed(hwnd),'Maximize failed');user.ShowWindow(hwnd,6);wait(lambda:user.IsIconic(hwnd),'Minimize maximized failed');close(hwnd)
            hwnd=start();assert user.IsZoomed(hwnd) and not user.IsIconic(hwnd),'Minimization overwrote maximization';close(hwnd)
            state.write_text('xs-window-v1 1 -50000 -40000 10000 8000 0 96 -50000 -40000 12000 9000\n',encoding='utf-8')
            hwnd=start();box=rect(hwnd);info=Monitor();info.size=c.sizeof(info);assert user.GetMonitorInfoW(user.MonitorFromWindow(hwnd,2),c.byref(info));area=info.work
            assert box[0]>=area.left and box[1]>=area.top and box[0]+box[2]<=area.right and box[1]+box[3]<=area.bottom,(box,list((area.left,area.top,area.right,area.bottom)));close(hwnd)
            state.write_bytes(b'broken\x00record');hwnd=start();assert user.IsWindowVisible(hwnd);close(hwnd)
            result={'passed':True,'normal':normal,'checks':['move/size restart','maximized restart and restored bounds','minimized normal restart','minimized maximized restart','off-screen/oversized recovery','damaged file fallback','explicit Home and Unicode path']}
            (args.evidence/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=10)
            for log in logs:log.close()
if __name__=='__main__':main()
