/* Included by xs_app_window.c. No frontend bridge, timers or disk writes run
 * on a worker thread; SetWindowSubclass owns the record through NCDESTROY. */
#include "xs_window_geometry.h"
typedef struct XsWindowState {
    wchar_t* Path;char* Parent;XsWindowGeometry Geometry;char Saved[256];int Restoring,Warned;
} XsWindowState;
static int xws_dpi(HWND window){
    typedef UINT (WINAPI* GetDpi)(HWND);GetDpi fn=NULL;FARPROC address=GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
    if(address)memcpy(&fn,&address,sizeof(fn));
    return fn?(int)fn(window):96;
}
static void xws_capture(HWND window,XsWindowState* state){
    WINDOWPLACEMENT p={0};MONITORINFO m={0};p.length=sizeof(p);m.cbSize=sizeof(m);
    if(!GetWindowPlacement(window,&p)||!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&m))return;
    int max=state->Geometry.Maximized;
    if(!IsIconic(window))max=IsZoomed(window)?1:0;
    /* WINDOWPLACEMENT normal bounds use workspace coordinates; persisted
     * coordinates are screen coordinates, including left/top taskbars. */
    int dx=m.rcWork.left-m.rcMonitor.left,dy=m.rcWork.top-m.rcMonitor.top;
    if(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)dx=dy=0;
    XsWindowGeometry g={1,p.rcNormalPosition.left+dx,p.rcNormalPosition.top+dy,
        p.rcNormalPosition.right-p.rcNormalPosition.left,p.rcNormalPosition.bottom-p.rcNormalPosition.top,max,xws_dpi(window),
        m.rcWork.left,m.rcWork.top,m.rcWork.right-m.rcWork.left,m.rcWork.bottom-m.rcWork.top};
    if(xwg_valid(&g))state->Geometry=g;
}
static void xws_save(HWND window,XsWindowState* state){
    if(state->Restoring)return;
    xws_capture(window,state);char text[256];int size=xwg_format(&state->Geometry,text);
    if(!size||!strcmp(text,state->Saved))return;
    wchar_t* temp=calloc(wcslen(state->Path)+100,sizeof(wchar_t));if(!temp)return;
    static volatile LONG serial=0;swprintf(temp,wcslen(state->Path)+100,L"%ls.pending-%lu-%lu-%ld",state->Path,(unsigned long)GetCurrentProcessId(),(unsigned long)GetCurrentThreadId(),InterlockedIncrement(&serial));
    int ok=state->Parent&&(xrtDirExists(state->Parent)||xrtDirCreateAll(state->Parent));
    HANDLE file=ok?CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
    if(file!=INVALID_HANDLE_VALUE){DWORD wrote=0;ok=WriteFile(file,text,(DWORD)size,&wrote,NULL)&&wrote==(DWORD)size&&FlushFileBuffers(file);CloseHandle(file);}
    else ok=0;
    if(ok)ok=MoveFileExW(temp,state->Path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(ok){memcpy(state->Saved,text,(size_t)size+1);state->Warned=0;}
    else{DeleteFileW(temp);if(!state->Warned)fprintf(stderr,"[xs] window placement could not be saved; continuing without persistence\n");state->Warned=1;}
    free(temp);
}
static LRESULT CALLBACK xws_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    XsWindowState* state=(XsWindowState*)data;
    if(message==WM_CLOSE||message==WM_DESTROY||message==WM_EXITSIZEMOVE)xws_save(window,state);
    if(message==WM_TIMER&&wp==id){KillTimer(window,id);xws_save(window,state);return 0;}
    if(message==WM_NCDESTROY){KillTimer(window,id);RemoveWindowSubclass(window,xws_proc,id);xrtFree(state->Parent);free(state->Path);free(state);return DefSubclassProc(window,message,wp,lp);}
    LRESULT result=DefSubclassProc(window,message,wp,lp);
    if((message==WM_SIZE||message==WM_MOVE||message==WM_DPICHANGED)&&!state->Restoring){
        xws_capture(window,state);SetTimer(window,id,350,NULL);
    }return result;
}
static void xws_install(HWND window,const char* path){
    if(!window||!path||!*path)return;
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if(!length||length>32700)return;
    XsWindowState* state=calloc(1,sizeof(*state));if(!state)return;
    state->Path=calloc((size_t)length,sizeof(wchar_t));state->Parent=xrtStrDup(path);
    if(!state->Path||!state->Parent){free(state->Path);xrtFree(state->Parent);free(state);return;}
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,state->Path,length);
    char* last=NULL;for(char* p=state->Parent;*p;p++)if(*p=='/'||*p=='\\')last=p;
    if(last)*last=0;else state->Parent[0]=0;
    xws_capture(window,state);
    HANDLE file=CreateFileW(state->Path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file!=INVALID_HANDLE_VALUE){
        char text[514];DWORD count=0;XsWindowGeometry saved={0};
        if(ReadFile(file,text,513,&count,NULL)&&count<=512){text[count]=0;
            if(xwg_parse(text,count,1,&saved)){
                RECT r={saved.X,saved.Y,saved.X+saved.Width,saved.Y+saved.Height};MONITORINFO m={0};m.cbSize=sizeof(m);
                if(GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&m)){
                    state->Restoring=1;
                    SetWindowPos(window,NULL,m.rcWork.left,m.rcWork.top,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);
                    MINMAXINFO limits={0};SendMessageW(window,WM_GETMINMAXINFO,0,(LPARAM)&limits);
                    saved=xwg_fit(saved,m.rcWork.left,m.rcWork.top,m.rcWork.right-m.rcWork.left,m.rcWork.bottom-m.rcWork.top,xws_dpi(window),limits.ptMinTrackSize.x,limits.ptMinTrackSize.y);
                    WINDOWPLACEMENT p={0};p.length=sizeof(p);p.showCmd=saved.Maximized?SW_SHOWMAXIMIZED:SW_SHOWNORMAL;
                    int dx=m.rcWork.left-m.rcMonitor.left,dy=m.rcWork.top-m.rcMonitor.top;
                    p.rcNormalPosition=(RECT){saved.X-dx,saved.Y-dy,saved.X+saved.Width-dx,saved.Y+saved.Height-dy};
                    SetWindowPlacement(window,&p);state->Geometry=saved;state->Restoring=0;
                }
            }
        }CloseHandle(file);
    }
    if(!SetWindowSubclass(window,xws_proc,(UINT_PTR)state,(DWORD_PTR)state)){xrtFree(state->Parent);free(state->Path);free(state);}
}
