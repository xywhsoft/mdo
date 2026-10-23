/* Shared bounded placement record. Coordinates are native Windows pixels or
 * GTK logical pixels; backend tags prevent mixing them in a portable Home. */
#ifndef XS_WINDOW_GEOMETRY_H
#define XS_WINDOW_GEOMETRY_H
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
typedef struct XsWindowGeometry {
    int Backend, X, Y, Width, Height, Maximized, Dpi;
    int WorkX, WorkY, WorkWidth, WorkHeight;
} XsWindowGeometry;
static int xwg_between(int n,int low,int high){return n>=low&&n<=high;}
static int xwg_valid(const XsWindowGeometry* g){
    return (g->Backend==1||g->Backend==2)&&xwg_between(g->X,-1048576,1048576)&&xwg_between(g->Y,-1048576,1048576)&&
        xwg_between(g->Width,80,32768)&&xwg_between(g->Height,80,32768)&&xwg_between(g->Maximized,0,1)&&
        xwg_between(g->Dpi,48,768)&&xwg_between(g->WorkX,-1048576,1048576)&&xwg_between(g->WorkY,-1048576,1048576)&&
        xwg_between(g->WorkWidth,80,131072)&&xwg_between(g->WorkHeight,80,131072);
}
static int xwg_parse(const char* text,size_t bytes,int backend,XsWindowGeometry* out){
    XsWindowGeometry value={0};long n[11];
    if(!text||bytes<13||bytes>512||memchr(text,0,bytes)||memcmp(text,"xs-window-v1 ",13))return 0;
    const char* at=text+13;char* end=NULL;
    for(int i=0;i<11;i++){
        if(at>=text+bytes)return 0;
        errno=0;n[i]=strtol(at,&end,10);
        if(errno||end==at||n[i]<-1048576||n[i]>1048576)return 0;
        if(i<10&&(!*end||!isspace((unsigned char)*end)))return 0;
        at=end;
    }
    while(at<text+bytes&&isspace((unsigned char)*at))at++;
    if(at!=text+bytes)return 0;
    value=(XsWindowGeometry){(int)n[0],(int)n[1],(int)n[2],(int)n[3],(int)n[4],(int)n[5],(int)n[6],(int)n[7],(int)n[8],(int)n[9],(int)n[10]};
    if(value.Backend!=backend||!xwg_valid(&value))return 0;
    *out=value;return 1;
}
static int xwg_format(const XsWindowGeometry* g,char text[256]){
    if(!xwg_valid(g))return 0;
    return snprintf(text,256,"xs-window-v1 %d %d %d %d %d %d %d %d %d %d %d\n",g->Backend,g->X,g->Y,g->Width,g->Height,g->Maximized,g->Dpi,g->WorkX,g->WorkY,g->WorkWidth,g->WorkHeight);
}
static int xwg_clamp(int n,int low,int high){return n<low?low:n>high?high:n;}
static int xwg_scale(int n,int from,int to){return (int)((int64_t)n*to/from);}
static XsWindowGeometry xwg_fit(XsWindowGeometry g,int x,int y,int w,int h,int dpi,int minw,int minh){
    int from=g.Dpi;g.Width=xwg_clamp(xwg_scale(g.Width,from,dpi),minw<w?minw:w,w);
    g.Height=xwg_clamp(xwg_scale(g.Height,from,dpi),minh<h?minh:h,h);
    g.X=xwg_clamp(x+xwg_scale(g.X-g.WorkX,from,dpi),x,x+w-g.Width);
    g.Y=xwg_clamp(y+xwg_scale(g.Y-g.WorkY,from,dpi),y,y+h-g.Height);
    g.WorkX=x;g.WorkY=y;g.WorkWidth=w;g.WorkHeight=h;g.Dpi=dpi;return g;
}
#endif
