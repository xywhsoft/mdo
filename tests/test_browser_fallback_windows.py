"""Exercise the native fallback state machine without opening a real browser."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMPILER = shutil.which("gcc") or shutil.which("gcc", path=r"E:\software\w64devkit\bin")

SOURCE = r'''
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <assert.h>
#include <stdatomic.h>
enum { XS_APP_NAV_LOADING, XS_APP_NAV_URL, XS_APP_NAV_FAIL, XS_APP_NAV_SHUTDOWN };
typedef struct { const char* Name; } XS_ServerInfo;
typedef struct {
    XS_ServerInfo* pServer;
    volatile LONG iNavState, iBrowserFallback;
    char sTargetUrl[1024];
} XS_AppWindow;
static atomic_bool g_XS_Stop;
static volatile LONG g_iBrowserService, Opened, Messages;
static int RejectOpen;
static WCHAR LastUrl[1024], LastMessage[1280];
static HINSTANCE ProbeOpen(HWND w,LPCWSTR verb,LPCWSTR url,LPCWSTR args,LPCWSTR cwd,INT show) {
    (void)w;(void)args;(void)cwd;(void)show;
    assert(wcscmp(verb,L"open")==0);
    wcscpy(LastUrl,url);InterlockedIncrement(&Opened);
    return (HINSTANCE)(INT_PTR)(RejectOpen ? 31 : 33);
}
static int ProbeMessage(HWND w,LPCWSTR text,LPCWSTR title,UINT flags) {
    (void)w;(void)title;(void)flags;
    wcscpy(LastMessage,text);InterlockedIncrement(&Messages);return IDOK;
}
#define ShellExecuteW ProbeOpen
#define MessageBoxW ProbeMessage
#include "tools/native/xs_browser_fallback_win.h"
static XS_ServerInfo Server={"fallback-test"};
static XS_AppWindow Window;
static void Reset(int state) {
    Window=(XS_AppWindow){.pServer=&Server,.iNavState=state};
    strcpy(Window.sTargetUrl,"http://127.0.0.1:54321/?q=a&b=1");
    g_XS_Stop=0;g_iBrowserService=0;Opened=0;Messages=0;RejectOpen=0;
    LastUrl[0]=0;LastMessage[0]=0;
}
static DWORD WINAPI RaceOpen(void* argument) {
    (void)argument;app_browser_fallback(&Window,"test failure");
    app_browser_open(&Window);return 0;
}
int main(void) {
    Reset(XS_APP_NAV_LOADING);
    app_browser_fallback(&Window,"missing runtime");
    assert(Opened==0 && Window.iBrowserFallback==1 && g_iBrowserService==1);
    Window.iNavState=XS_APP_NAV_URL;
    app_browser_open(&Window);app_browser_open(&Window);
    assert(Opened==1 && Window.iBrowserFallback==2 && Messages==0);
    assert(wcscmp(LastUrl,L"http://127.0.0.1:54321/?q=a&b=1")==0);

    Reset(XS_APP_NAV_URL);
    app_browser_fallback(&Window,"late initialization failure");
    app_browser_fallback(&Window,"duplicate notification");
    assert(Opened==1);

    Reset(XS_APP_NAV_URL);Window.iBrowserFallback=-1;
    app_browser_fallback(&Window,"explicit headless");
    assert(Opened==0 && g_iBrowserService==0);

    Reset(XS_APP_NAV_URL);app_browser_open(&Window);
    assert(Opened==0); /* Healthy native window. */

    for(int state=XS_APP_NAV_FAIL;state<=XS_APP_NAV_SHUTDOWN;state++) {
        Reset(state);app_browser_fallback(&Window,"startup/stop");
        assert(Opened==0);
    }
    Reset(XS_APP_NAV_URL);g_XS_Stop=1;
    app_browser_fallback(&Window,"shutdown race");assert(Opened==0);

    Reset(XS_APP_NAV_URL);RejectOpen=1;
    app_browser_fallback(&Window,"no browser association");
    assert(Opened==1 && Messages==1 && wcsstr(LastMessage,LastUrl)!=NULL);
    app_browser_open(&Window);assert(Opened==1 && Messages==1);

    Reset(XS_APP_NAV_URL);
    HANDLE workers[16];
    for(int i=0;i<16;i++) { workers[i]=CreateThread(NULL,0,RaceOpen,NULL,0,NULL);assert(workers[i]); }
    assert(WaitForMultipleObjects(16,workers,TRUE,10000)==WAIT_OBJECT_0);
    for(int i=0;i<16;i++) CloseHandle(workers[i]);
    assert(Opened==1 && Messages==0);
    puts("PASS: ready ordering, bound URL, late failure, exactly once, headless, native, startup failure, shutdown, launch failure and concurrency");
    return 0;
}
'''


@unittest.skipUnless(os.name == "nt" and COMPILER, "Windows and MinGW GCC required")
class BrowserFallbackTests(unittest.TestCase):
    def test_native_fallback_lifecycle(self):
        parent = ROOT / ".build"
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="browser-fallback-test-", dir=parent) as directory:
            source = Path(directory) / "test.c"
            executable = Path(directory) / "test.exe"
            source.write_text(SOURCE, encoding="utf-8")
            subprocess.run([COMPILER, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                            str(source), "-o", str(executable), "-lole32", "-luser32"], check=True)
            result = subprocess.run([str(executable)], check=True, capture_output=True, timeout=20)
            self.assertIn(b"PASS:", result.stdout)
            print(result.stdout.decode("utf-8", errors="replace"))


if __name__ == "__main__":
    unittest.main()
