"""Compile canonical script context/lifetime rules against the existing SDK."""
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]
SDK=Path(os.environ.get('MDO_TEST_SDK',''))
CXX=shutil.which(os.environ.get('CXX','g++')) or 'E:/software/w64devkit/bin/g++.exe'

@unittest.skipUnless(os.name=='nt' and (SDK/'lib/webview/webview/webview.h').is_file() and Path(CXX).is_file(),
                     'Set MDO_TEST_SDK to the existing SDK and provide a Windows C++ compiler')
class BrowserScriptNativeTests(unittest.TestCase):
    def test_private_context_identity_and_retained_callback_budget(self):
        web=(SDK/'lib/webview/webview/webview.h').read_text(encoding='utf-8')
        parser=web[web.index('inline int json_parse_c('):web.index('// Holds a symbol name')]
        native=(ROOT/'tools/native/xs_browser_frames.inc.hpp').read_text()
        registry=native[native.index('static void xb_script_metadata('):native.index('static void xb_frame_event(')]
        world=(ROOT/'tools/native/xs_browser_worlds.inc.hpp').read_text()
        number=world[world.index('static int xb_world_number('):world.index('static bool xb_world_live(')]
        state=(ROOT/'tools/native/xs_browser.inc.hpp').read_text()
        context=state[state.index('struct xb_script_budget{'):state.index('struct xb_frame_session{')]
        fixture=(ROOT/'tests/fixtures/browser-script-contexts.cpp').read_text().replace('// PARSER',parser).replace('// CONTEXT',context).replace('// REGISTRY',number+registry)
        directory=ROOT/'.build/browser-script-native';directory.mkdir(parents=True,exist_ok=True)
        cpp=directory/'contexts.cpp';binary=directory/'contexts.exe';cpp.write_text(fixture,encoding='utf-8')
        result=subprocess.run([CXX,'-std=c++17','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-parentheses',str(cpp),'-o',str(binary)],capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stderr[-3000:])
        result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,env=dict(os.environ,PATH=str(Path(CXX).parent)+os.pathsep+os.environ.get('PATH','')))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__':unittest.main()
