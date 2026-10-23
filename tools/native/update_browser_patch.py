"""Regenerate the pinned xs browser overlay from its reviewed source file."""
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
    files['src/webview/xs_browser.inc.hpp']=('',(ROOT/'tools/native/xs_browser.inc.hpp').read_text(encoding='utf-8'))
    for name,(old,new) in files.items():
        text+=''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name if old else '/dev/null',tofile='b/'+name))
    patch.write_text(text,encoding='utf-8',newline='\n');lock['sdk_overlay']['sha256']=hashlib.sha256(patch.read_bytes()).hexdigest()
    path.write_text(json.dumps(lock,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(lock['sdk_overlay']['sha256'])
if __name__=='__main__':main()
