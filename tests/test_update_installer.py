"""Windows installer smoke: actual replacement, rollback and argument quoting."""
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from ctypes import wintypes

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r'''
#include <windows.h>
#include <stdio.h>
int wmain(int argc,wchar_t** argv) {
    FILE* file=fopen("started.txt","wb"); int i;
    if (!file) return 1;
    fprintf(file,"%s\n",VERSION);
    for(i=1;i<argc;i++) {
        char text[4096];
        WideCharToMultiByte(CP_UTF8,0,argv[i],-1,text,sizeof(text),NULL,NULL);
        fprintf(file,"%s\n",text);
    }
    fclose(file);
    if(argc>2 && !wcscmp(argv[1],L"--wait"))
        while(GetFileAttributesW(argv[2])==INVALID_FILE_ATTRIBUTES) Sleep(50);
    return 0;
}
'''


def wait_for(path, timeout=15):
    deadline = time.monotonic()+timeout
    while not path.exists():
        if time.monotonic()>deadline: raise AssertionError("Missing " + str(path))
        time.sleep(.05)


def run():
    if os.name != "nt": print("SKIP Windows installer"); return
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,
                                  wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    with tempfile.TemporaryDirectory(prefix="update-installer-",dir=ROOT/".build") as raw:
        base=Path(raw); code=base/"probe.c"; code.write_text(PROGRAM)
        versions={}
        for version in ("old","new"):
            executable=base/(version+".exe")
            subprocess.run(["gcc","-municode",str(code),'-DVERSION="'+version+'"',"-o",str(executable)],check=True)
            versions[version]=executable
        for failure in ("", "locked", "hash", "identity"):
            directory=base/("墨斗 [测试] $&' " + (failure or "success")); directory.mkdir()
            cache=directory/"mdo-home/data/update"; cache.mkdir(parents=True)
            target=directory/"墨斗 '$&.exe"; shutil.copy2(versions["old"],target)
            new=cache/"new.exe"; shutil.copy2(versions["new"],new)
            script=cache/"install.ps1"; shutil.copy2(ROOT/"app/update/install.ps1",script)
            stop=directory/"exit.marker"
            parent=subprocess.Popen([str(target),"--wait",str(stop)],cwd=directory,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            created,end,system,user=(wintypes.FILETIME() for _ in range(4))
            assert kernel.GetProcessTimes(parent._handle,ctypes.byref(created),ctypes.byref(end),
                                          ctypes.byref(system),ctypes.byref(user))
            identity=(created.dwHighDateTime<<32)|created.dwLowDateTime
            args=["has spaces","中文参数",'quote"inside', "tail\\", ""]
            expected=hashlib.sha256(new.read_bytes()).hexdigest()
            parameters=cache/"install.json"
            parameters.write_text(json.dumps(dict(target=str(target),sha256="0"*64 if failure=="hash" else expected,
                pid=str(parent.pid),identity=str(identity+1 if failure=="identity" else identity),
                home=str(directory/"mdo-home"),work_dir=str(directory),args=args)),encoding="utf-8")
            lock=None; helper=None
            try:
                if failure=="locked":
                    lock=kernel.CreateFileW(str(target),0x80000000,1,None,3,0,None)
                    assert lock != wintypes.HANDLE(-1).value
                helper=subprocess.Popen(["powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass",
                    "-File",str(script),"-Parameters",str(parameters)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                if failure in ("hash","identity"):
                    helper.communicate(timeout=15)
                    assert not (cache/"install.ready").exists() and parent.poll() is None
                    assert target.read_bytes()==versions["old"].read_bytes()
                    assert json.loads((cache/"install-result.json").read_text(encoding="utf-8"))["status"]=="failed"
                else:
                    try: wait_for(cache/"install.ready")
                    except BaseException:
                        if helper.poll() is not None:
                            out,err=helper.communicate(timeout=1)
                            print(out.decode(errors="replace"),err.decode(errors="replace"))
                        if (cache/"install-result.json").exists():
                            print((cache/"install-result.json").read_text(encoding="utf-8"))
                        raise
                    (cache/"install.go").write_text(expected,encoding="utf-8")
                    stop.touch(); parent.wait(timeout=10)
                    out,err=helper.communicate(timeout=15)
                    result=json.loads((cache/"install-result.json").read_text(encoding="utf-8"))
                    if failure:
                        assert result["status"]=="failed", (result,out,err)
                        assert target.read_bytes()==versions["old"].read_bytes()
                    else:
                        assert result["status"]=="success", (result,out,err)
                        assert target.read_bytes()==versions["new"].read_bytes()
                        assert (cache/"old.exe").read_bytes()==versions["old"].read_bytes()
                        wait_for(directory/"started.txt")
                        assert (directory/"started.txt").read_text(encoding="utf-8").splitlines()==["new",*args]
                print("PASS Windows installer " + (failure or "replace, backup, restart and Unicode argument quoting"))
            finally:
                if lock: kernel.CloseHandle(lock)
                stop.touch()
                if parent.poll() is None: parent.wait(timeout=5)
                if helper and helper.poll() is None: helper.terminate(); helper.wait(timeout=5)


if __name__=="__main__": run()
