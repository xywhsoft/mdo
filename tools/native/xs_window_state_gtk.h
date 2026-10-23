/* GTK owns placement in its UI process. Wayland controls absolute position;
 * there we restore size/maximization and leave positioning to the compositor. */
#include "xs_window_geometry.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
static char* WindowStatePath;
static XsWindowGeometry WindowGeometry;
static guint WindowSaveTimer;
static gboolean WindowGeometryReady,WindowSaveWarned;
static void WindowCapture(void){
    if(!WindowStatePath||!WindowGeometryReady)return;
    GdkWindow* native=gtk_widget_get_window(AppWindow);if(!native)return;
    GdkWindowState state=gdk_window_get_state(native);
    if(state&(GDK_WINDOW_STATE_ICONIFIED|GDK_WINDOW_STATE_MAXIMIZED|GDK_WINDOW_STATE_FULLSCREEN))return;
    int x=0,y=0,w=0,h=0;gtk_window_get_size(GTK_WINDOW(AppWindow),&w,&h);
    if(GDK_IS_X11_DISPLAY(gdk_display_get_default()))gtk_window_get_position(GTK_WINDOW(AppWindow),&x,&y);
    GdkMonitor* monitor=gdk_display_get_monitor_at_window(gdk_display_get_default(),native);if(!monitor)return;
    GdkRectangle work;gdk_monitor_get_workarea(monitor,&work);
    XsWindowGeometry g={2,x,y,w,h,WindowGeometry.Maximized,96,work.x,work.y,work.width,work.height};
    if(xwg_valid(&g))WindowGeometry=g;
}
static gboolean WindowSave(gpointer unused){
    (void)unused;WindowSaveTimer=0;if(!WindowStatePath||!WindowGeometryReady)return G_SOURCE_REMOVE;
    char text[256];int size=xwg_format(&WindowGeometry,text);if(!size)return G_SOURCE_REMOVE;
    char* parent=g_path_get_dirname(WindowStatePath);int ok=g_mkdir_with_parents(parent,0700)==0;g_free(parent);
    char* temp=g_strconcat(WindowStatePath,".pending-XXXXXX",NULL);int fd=ok?g_mkstemp_full(temp,O_RDWR|O_CLOEXEC,0600):-1;size_t at=0;
    if(fd>=0){
        while(at<(size_t)size){ssize_t n=write(fd,text+at,(size_t)size-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=(size_t)n;}
        ok=at==(size_t)size&&fsync(fd)==0;if(close(fd))ok=0;
        if(ok)ok=g_rename(temp,WindowStatePath)==0;
    }else ok=0;
    if(!ok){g_unlink(temp);if(!WindowSaveWarned)g_printerr("[xs-window] placement could not be saved; continuing without persistence\n");WindowSaveWarned=TRUE;}
    else WindowSaveWarned=FALSE;
    g_free(temp);return G_SOURCE_REMOVE;
}
static void WindowSchedule(void){if(WindowSaveTimer)g_source_remove(WindowSaveTimer);WindowSaveTimer=g_timeout_add(350,WindowSave,NULL);}
static gboolean WindowConfigure(GtkWidget* widget,GdkEvent* event,gpointer unused){
    (void)widget;(void)event;(void)unused;WindowCapture();WindowSchedule();return FALSE;
}
static gboolean WindowStateChange(GtkWidget* widget,GdkEventWindowState* event,gpointer unused){
    (void)widget;(void)unused;
    if((event->changed_mask&GDK_WINDOW_STATE_MAXIMIZED)&&!(event->new_window_state&GDK_WINDOW_STATE_ICONIFIED))WindowGeometry.Maximized=!!(event->new_window_state&GDK_WINDOW_STATE_MAXIMIZED);
    WindowSchedule();return FALSE;
}
static void WindowStateInstall(const char* path,int minw,int minh){
    if(!path||!*path)return;
    WindowStatePath=g_strdup(path);
    int fd=g_open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK,0);XsWindowGeometry g={0};
    if(fd>=0){
        struct stat stat;char text[514];ssize_t size=-1;
        if(!fstat(fd,&stat)&&S_ISREG(stat.st_mode)&&stat.st_size<=512)size=read(fd,text,513);
        if(size>=0){text[size]=0;if(!xwg_parse(text,(size_t)size,2,&g))memset(&g,0,sizeof(g));}close(fd);
    }
    if(g.Backend==2){
        GdkDisplay* display=gdk_display_get_default();GdkMonitor* monitor=gdk_display_get_monitor_at_point(display,g.X,g.Y);
        if(monitor){GdkRectangle work;gdk_monitor_get_workarea(monitor,&work);g=xwg_fit(g,work.x,work.y,work.width,work.height,96,minw,minh);
            gtk_window_set_default_size(GTK_WINDOW(AppWindow),g.Width,g.Height);
            if(GDK_IS_X11_DISPLAY(display))gtk_window_move(GTK_WINDOW(AppWindow),g.X,g.Y);
            if(g.Maximized)gtk_window_maximize(GTK_WINDOW(AppWindow));
            WindowGeometry=g;
        }
    }
    WindowGeometryReady=TRUE;
    g_signal_connect(AppWindow,"configure-event",G_CALLBACK(WindowConfigure),NULL);
    g_signal_connect(AppWindow,"window-state-event",G_CALLBACK(WindowStateChange),NULL);
}
static void WindowStateFinish(void){
    if(WindowSaveTimer){g_source_remove(WindowSaveTimer);WindowSaveTimer=0;}
    WindowCapture();WindowSave(NULL);g_free(WindowStatePath);WindowStatePath=NULL;
}
