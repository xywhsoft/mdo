"""Compile and exercise the actual shared native geometry policy."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include "xs_window_geometry.h"
#include <assert.h>
int main(void){
    XsWindowGeometry original={1,-1700,100,1200,800,1,96,-1920,0,1920,1040},parsed={0};char text[256];
    int n=xwg_format(&original,text);assert(n>0&&xwg_parse(text,n,1,&parsed));assert(parsed.X==-1700&&parsed.Maximized==1);
    assert(!xwg_parse(text,n,2,&parsed));assert(!xwg_parse("broken",6,1,&parsed));
    const char* bad="xs-window-v1 1 999999999999999999999999999 0 900 600 0 96 0 0 1920 1080\n";
    assert(!xwg_parse(bad,strlen(bad),1,&parsed));
    text[5]=0;assert(!xwg_parse(text,n,1,&parsed));
    original.Maximized=0;XsWindowGeometry same=xwg_fit(original,-1920,0,1920,1040,96,360,320);
    assert(same.X==original.X&&same.Y==original.Y&&same.Width==1200&&same.Height==800);
    XsWindowGeometry moved=xwg_fit(original,0,40,1280,680,96,360,320);
    assert(moved.X>=0&&moved.Y>=40&&moved.X+moved.Width<=1280&&moved.Y+moved.Height<=720);
    XsWindowGeometry scaled=xwg_fit(original,0,0,3840,2160,144,540,480);
    assert(scaled.Width==1800&&scaled.Height==1200&&scaled.X==330&&scaled.Y==150);
    original.Width=100;original.Height=90;original.X=9999;original.Y=-9999;
    XsWindowGeometry small=xwg_fit(original,0,0,300,250,96,360,320);
    assert(small.Width==300&&small.Height==250&&small.X==0&&small.Y==0);
    return 0;
}
'''

class WindowGeometryTest(unittest.TestCase):
    def test_native_record_validation_and_monitor_dpi_recovery(self):
        with tempfile.TemporaryDirectory(prefix='mdo-geometry-') as directory:
            folder=Path(directory);source=folder/'probe.c';source.write_text(SOURCE,encoding='utf-8')
            binary=folder/('probe.exe' if os.name=='nt' else 'probe')
            built=subprocess.run([os.environ.get('CC','gcc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'tools/native'),str(source),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
