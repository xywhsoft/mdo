"""Real GTK placement persistence on an isolated X11 display, no WebKit/model."""
import os,shutil,subprocess,sys,tempfile,unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
static GtkWidget* AppWindow;
#include "xs_window_state_gtk.h"
static int Mode,Result;
static gboolean Finish(gpointer unused){
    (void)unused;int x,y,width,height;gtk_window_get_position(GTK_WINDOW(AppWindow),&x,&y);gtk_window_get_size(GTK_WINDOW(AppWindow),&width,&height);
    if(Mode==2&&(x!=100||y!=80||width!=900||height!=650))Result=1;
    if(Mode==3&&(width!=640||height!=480))Result=2;
    if(Mode==4&&(x<0||y<0||width>1280||height>800||x+width>1280||y+height>800))Result=3;
    WindowStateFinish();gtk_main_quit();return G_SOURCE_REMOVE;
}
static gboolean Move(gpointer unused){(void)unused;gtk_window_move(GTK_WINDOW(AppWindow),100,80);gtk_window_resize(GTK_WINDOW(AppWindow),900,650);return G_SOURCE_REMOVE;}
int main(int argc,char** argv){
    if(argc!=3||!gtk_init_check(NULL,NULL))return 10;
    Mode=atoi(argv[2]);AppWindow=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_default_size(GTK_WINDOW(AppWindow),640,480);
    WindowStateInstall(argv[1],360,320);gtk_widget_show_all(AppWindow);
    if(Mode==1)g_timeout_add(150,Move,NULL);
    g_timeout_add(1000,Finish,NULL);gtk_main();gtk_widget_destroy(AppWindow);return Result;
}
'''

@unittest.skipUnless(sys.platform=='linux','GTK/X11 regression runs under Linux')
class GtkWindowStateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if any(not shutil.which(name) for name in ('cc','pkg-config','xvfb-run')):
            raise unittest.SkipTest('GTK regression needs the existing compiler, pkg-config and Xvfb')
        if subprocess.run(['pkg-config','--exists','gtk+-3.0']).returncode:
            raise unittest.SkipTest('GTK3 development headers unavailable')
    def test_actual_gtk_restart_and_monitor_recovery(self):
        flags=subprocess.check_output(['pkg-config','--cflags','--libs','gtk+-3.0'],text=True).split()
        with tempfile.TemporaryDirectory(prefix='mdo-gtk-placement-') as directory:
            folder=Path(directory);source=folder/'probe.c';source.write_text(SOURCE,encoding='utf-8');binary=folder/'probe';state=folder/'窗口 Home/data/ui/main-window.state'
            built=subprocess.run(['cc','-D_GNU_SOURCE','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-I'+str(ROOT/'tools/native'),str(source),'-o',str(binary),*flags],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            for mode in (1,2):subprocess.run(['xvfb-run','-a','-s','-screen 0 1280x800x24',str(binary),str(state),str(mode)],check=True,timeout=15)
            self.assertTrue(state.read_text().startswith('xs-window-v1 2 100 80 900 650'))
            state.write_bytes(b'bad\0record');subprocess.run(['xvfb-run','-a','-s','-screen 0 1280x800x24',str(binary),str(state),'3'],check=True,timeout=15)
            state.write_text('xs-window-v1 2 -50000 -40000 10000 8000 0 96 -50000 -40000 12000 9000\n')
            subprocess.run(['xvfb-run','-a','-s','-screen 0 1280x800x24',str(binary),str(state),'4'],check=True,timeout=15)

if __name__=='__main__':unittest.main()
