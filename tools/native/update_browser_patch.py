"""Regenerate pinned xs browser/placement overlays from reviewed sources."""
from pathlib import Path
import argparse,difflib,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,help='Existing Git checkout containing the pinned xs revision')
    args=parser.parse_args()
    path=ROOT/'deps.lock';lock=json.loads(path.read_text(encoding='utf-8'));revision=lock['xserver']['commit']
    source=args.source or ROOT/'.build/locked-xs'/revision
    patch=ROOT/'tools/dependencies/xwork-mcp.patch';text=patch.read_text(encoding='utf-8')
    def read(name):return subprocess.check_output(['git','show',revision+':'+name],cwd=source).decode('utf-8').replace('\r\n','\n')
    files={};name='src/webview/webview_impl.cpp';old=read(name)
    new=old.replace('#include "xs_directory_drop.inc.hpp"','#include "xs_directory_drop.inc.hpp"\n#include "xs_browser.inc.hpp"').replace('if ( pWebview != nullptr ) webview_destroy((webview_t)pWebview);','if ( pWebview != nullptr ) { xsWvDestroyBrowsers(pWebview); webview_destroy((webview_t)pWebview); }');files[name]=(old,new)
    name='src/webview/xs_app_window.c';old=read(name)
    new=old.replace('extern int xsWvEnableDirectoryDrop(void* pWebview, const char* sUrl);','extern int xsWvEnableDirectoryDrop(void* pWebview, const char* sUrl);\nextern int xsWvEnableBrowser(void* pWebview, const char* sUrl, const char* sProfile);').replace('xsWvEnableDirectoryDrop((void*)pWin->pWebview, pWin->sTargetUrl);','xsWvEnableDirectoryDrop((void*)pWin->pWebview, pWin->sTargetUrl);\n\t\txsWvEnableBrowser((void*)pWin->pWebview, pWin->sTargetUrl, pWin->sProfileDir);').replace('xsWvEnableDirectoryDrop(pWv, pWin->sTargetUrl);','xsWvEnableDirectoryDrop(pWv, pWin->sTargetUrl);\n\t\txsWvEnableBrowser(pWv, pWin->sTargetUrl, pWin->sProfileDir);');files[name]=(old,new)
    def change(name,old,new):
        if name in files:base,current=files[name]
        else:base=read(name);current=base
        if current.count(old)!=1:raise ValueError('Pinned native window hook changed: '+name+' '+old)
        files[name]=(base,current.replace(old,new))
    name='src/webview/xs_window_profile.h'
    change(name,'static bool app_cfg_profile_dir(xvalue* pWin, char** ppPath)',
        'static bool app_cfg_window_path(xvalue* pWin, const char* sKey, char** ppPath)')
    change(name,'XRT_STR_LITERAL("profile_dir"));','xrtStrView(sKey));')
    change(name,'app_cfg_profile_text(pWin, "profile_dir");','app_cfg_profile_text(pWin, sKey);')
    change(name,'\n\n#endif','\nstatic bool app_cfg_profile_dir(xvalue* pWin,char** ppPath)\n{return app_cfg_window_path(pWin,"profile_dir",ppPath);}\n\n#endif')
    name='src/webview/xs_app_window.c'
    change(name,'#include "xs_webview.h"','#include "xs_webview.h"\n#include "xs_window_state_win.h"')
    change(name,'char*\t\t\tsProfileDir;','char*\t\t\tsStateFile;\n\tchar*\t\t\tsProfileDir;')
    change(name,'\t/* 创建完成，自查导航状态：早于本线程完成的通知在此生效 */','\txws_install(pWin->hWnd,pWin->sStateFile);\n\t/* 创建完成，自查导航状态：早于本线程完成的通知在此生效 */')
    change(name,'\t\t\tapp_cfg_str(pCfg, "title",','\t\t\tif (!app_cfg_window_path(pCfg,"state_file",&pWin->sStateFile))\n\t\t\t\tprintf("[xs] invalid window.state_file; placement persistence disabled\\n");\n\t\t\tapp_cfg_str(pCfg, "title",')
    change(name,'xrtFree(pWin->sProfileDir);','xrtFree(pWin->sStateFile);pWin->sStateFile=NULL;\n\t\t\txrtFree(pWin->sProfileDir);')
    change(name,'xrtFree(g_tWindows[i].sProfileDir);','xrtFree(g_tWindows[i].sStateFile);g_tWindows[i].sStateFile=NULL;\n\t\t\t\txrtFree(g_tWindows[i].sProfileDir);')
    change(name,'#include "xs_window_state_win.h"','#include "xs_window_state_win.h"\n#include <shellapi.h>')
    change(name,'\tvolatile LONG\t\tiAlive;','\tvolatile LONG\t\tiBrowserFallback; /* -1=headless, 0=native, 1=pending, 2=opened */\n\tvolatile LONG\t\tiAlive;')
    change(name,'static HANDLE g_hSingleMutex, g_hWakeEvent;','static volatile LONG g_iBrowserService;\nstatic int g_iBrowserOnly;\n#include "xs_browser_fallback_win.h"\nstatic HANDLE g_hSingleMutex, g_hWakeEvent;')
    change(name,'\t\tCoUninitialize();\n\t\tif ( pWin->iClose', '\t\tapp_browser_fallback(pWin, "WebView creation failed");\n\t\tCoUninitialize();\n\t\tif ( pWin->iClose')
    change(name,'pWin->pWebview = xsWvCreate(pWin->iDebug, pWin->sProfileDir);',
        'pWin->pWebview = getenv("XS_WV2_FORCE_CREATE_FAILURE") ? NULL : xsWvCreate(pWin->iDebug, pWin->sProfileDir);')
    change(name,'&& InterlockedDecrement(&g_iLiveStopWindows) == 0 )',
        '&& InterlockedDecrement(&g_iLiveStopWindows) == 0\n\t\t&& !InterlockedCompareExchange(&g_iBrowserService, 0, 0) )')
    change(name,'int iFallback = XS_APP_FB_ASK;', 'int iFallback = -1; /* Default: use the browser, without downloading a runtime. */')
    # Registry-only detection misses an application-local Fixed Version runtime.
    # The normal path lets the actual WebView loader determine availability.
    change(name,'\tif ( wv2_runtime_present() ) return true;\n','')
    change(name,'sizeof sFallback, "ask");','sizeof sFallback, "browser");\n\t\tif ( strcmp(sFallback, "ask") == 0 ) iFallback = XS_APP_FB_ASK;')
    change(name,'\tif ( iFallback == XS_APP_FB_HEADLESS ) {',
        '\tif (iFallback == -1) {\n\t\tg_iBrowserOnly = getenv("XS_WV2_FORCE_MISSING") != NULL;\n\t\treturn true; /* Try the loader, including app-local runtimes. */\n\t}\n\tif (wv2_runtime_present()) return true;\n\tif ( iFallback == XS_APP_FB_HEADLESS ) {')
    change(name,'printf("[xs] app: WebView2 install declined, headless\\n");\n\t\treturn false;',
        'printf("[xs] app: WebView2 install declined, browser fallback\\n");\n\t\tg_iBrowserOnly = 1; return true;')
    change(name,'printf("[xs] app: WebView2 install failed, headless\\n");\n\t\treturn false;',
        'printf("[xs] app: WebView2 install failed, browser fallback\\n");\n\t\tg_iBrowserOnly = 1; return true;')
    change(name,'\t\tchar sClose[16];','\t\tchar sClose[16], sFallback[16];')
    change(name,'pWin = &g_tWindows[g_iWindowCount];','pWin = &g_tWindows[g_iWindowCount++];')
    change(name,'\t\tpCfg = app_window_object(pServer);',
        '\t\tpCfg = app_window_object(pServer);\n\t\tif (pCfg) app_cfg_str(pCfg, "install_fallback", sFallback, sizeof sFallback, "browser");\n\t\telse snprintf(sFallback, sizeof sFallback, "browser");\n\t\tif (!strcmp(sFallback, "headless")) pWin->iBrowserFallback = -1;\n\t\tif (g_iBrowserOnly) { app_browser_fallback(pWin, "WebView2 Runtime unavailable"); continue; }')
    change(name,'\t\t\t\t\t"frontend disabled (no AppData fallback)\\n", pServer->Name);\n\t\t\t\tcontinue;',
        '\t\t\t\t\t"frontend disabled (no AppData fallback)\\n", pServer->Name);\n\t\t\t\tapp_browser_fallback(pWin, "Invalid WebView profile directory");\n\t\t\t\tcontinue;')
    change(name,'\t\t\t\tpServer->Name);\n\t\t\txrtFree(pWin->sStateFile);',
        '\t\t\t\tpServer->Name);\n\t\t\tapp_browser_fallback(pWin, "UI thread creation failed");\n\t\t\txrtFree(pWin->sStateFile);')
    change(name,'\t\tg_iWindowCount++;\n\t\tprintf(', '\t\tprintf(')
    change(name,'\t\tInterlockedExchange(&pWin->iNavState, XS_APP_NAV_URL);',
        '\t\tInterlockedExchange(&pWin->iNavState, XS_APP_NAV_URL);\n\t\tapp_browser_open(pWin);')
    # Reopening the executable also reopens its browser page in fallback mode.
    change(name,'\t\tif ( pWin->iClose == XS_APP_CLOSE_HIDE && pWin->iAlive',
        '\t\tif (InterlockedCompareExchange(&pWin->iBrowserFallback, 1, 2) == 2)\n\t\t\tapp_browser_open(pWin);\n\t\tif ( pWin->iClose == XS_APP_CLOSE_HIDE && pWin->iAlive')
    name='src/webview/xs_linux_window.c'
    change(name,'char* Profile = NULL;','char* Profile = NULL;char* StateFile = NULL;')
    change(name,'        char* Helper = Profile ?', '        if(Config&&!app_cfg_window_path(Config,"state_file",&StateFile))\n            printf("[xs] invalid window.state_file; placement persistence disabled\\n");\n        char* Helper = Profile ?')
    change(name,'Close ? Close : "stop", NULL};','Close ? Close : "stop", StateFile ? StateFile : "", NULL};')
    change(name,'xrtFree(Helper); xrtFree(Profile);','xrtFree(StateFile); xrtFree(Helper); xrtFree(Profile);')
    name='src/webview/xs_linux_ui.c'
    change(name,'static GtkWidget* AppWindow;','static GtkWidget* AppWindow;\n#include "xs_window_state_gtk.h"')
    change(name,'if (argc != 9 || fcntl','if ((argc != 9 && argc != 10) || fcntl')
    change(name,'    HideOnClose = !strcmp(argv[8], "hide");','    WindowStateInstall(argc==10?argv[9]:NULL,Geometry.min_width,Geometry.min_height);\n    HideOnClose = !strcmp(argv[8], "hide");')
    change(name,'    gtk_widget_destroy(AppWindow); g_object_unref(Context);','    WindowStateFinish();\n    gtk_widget_destroy(AppWindow); g_object_unref(Context);')
    for header in ['xs_browser.inc.hpp','xs_window_geometry.h','xs_window_state_win.h','xs_window_state_gtk.h','xs_browser_fallback_win.h']:
        files['src/webview/'+header]=('',(ROOT/'tools/native'/header).read_text(encoding='utf-8'))
    # Replace only owned WebView sections. Other fixes may have been appended
    # after them; truncating the patch would silently discard those fixes.
    replacements={name:''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name if old else '/dev/null',tofile='b/'+name)) for name,(old,new) in files.items()}
    sections=re.split(r'(?m)(?=^--- (?:a/|/dev/null))',text)
    output=[];seen=set()
    for section in sections:
        match=re.search(r'(?m)^\+\+\+ b/(.+)$',section)
        name=match[1] if match else None
        if name in replacements:
            if name in seen:raise ValueError('Duplicate managed SDK patch section: '+name)
            seen.add(name);output.append(replacements[name])
        else:output.append(section)
    text=''.join(output)+''.join(value for name,value in replacements.items() if name not in seen)
    patch.write_text(text,encoding='utf-8',newline='\n');lock['sdk_overlay']['sha256']=hashlib.sha256(patch.read_bytes()).hexdigest()
    path.write_text(json.dumps(lock,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(lock['sdk_overlay']['sha256'])
if __name__=='__main__':main()
