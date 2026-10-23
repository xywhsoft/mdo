"""Regenerate pinned xs browser/placement overlays from reviewed sources."""
from pathlib import Path
import difflib,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]

def main():
    path=ROOT/'deps.lock';lock=json.loads(path.read_text(encoding='utf-8'));revision=lock['xserver']['commit']
    source=ROOT/'.build/locked-xs'/revision
    patch=ROOT/'tools/dependencies/xwork-mcp.patch';text=patch.read_text(encoding='utf-8')
    marker='--- a/src/webview/webview_impl.cpp\n'
    if marker in text:text=text[:text.index(marker)]
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
    for header in ['xs_browser.inc.hpp','xs_window_geometry.h','xs_window_state_win.h','xs_window_state_gtk.h']:
        files['src/webview/'+header]=('',(ROOT/'tools/native'/header).read_text(encoding='utf-8'))
    # A ConPTY is already a separate console. Keep Job ownership/suspended
    # admission, but CREATE_NEW_PROCESS_GROUP would disable keyboard Ctrl+C
    # in PowerShell and its children (Win32 process creation contract).
    name='lib/xrt.h';old=read(name)
    start=old.index('bool __xrtProcessTerminalSpawnWindows(')
    end=old.index('\ncleanup:',start)
    chunk=old[start:end];flag='iFlags |= CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED;'
    if chunk.count(flag)!=1:raise ValueError('Pinned ConPTY process group hook changed')
    chunk=chunk.replace(flag,'/* ConPTY owns its console; Job still owns the process tree. */\n\t\tiFlags |= CREATE_SUSPENDED;')
    new=old[:start]+chunk+old[end:];files[name]=(old,new)
    lock['sdk_overlay']['xrt_single_header_sha256']=hashlib.sha256(new.encode('utf-8')).hexdigest()
    for name,(old,new) in files.items():
        text+=''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name if old else '/dev/null',tofile='b/'+name))
    patch.write_text(text,encoding='utf-8',newline='\n');lock['sdk_overlay']['sha256']=hashlib.sha256(patch.read_bytes()).hexdigest()
    path.write_text(json.dumps(lock,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(lock['sdk_overlay']['sha256'])
if __name__=='__main__':main()
