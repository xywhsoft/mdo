/* Same-host compiler probe. Never initializes a second mdo application. */
#include <xsbase.h>
#include <libtcc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "benchmark-config.h"
#ifdef BENCH_OBJECT_MANIFEST
#include BENCH_OBJECT_MANIFEST
#endif

static char Diagnostics[8192];
static void CompilerError(void* User, const char* Message) {
    (void)User;size_t At=strlen(Diagnostics);
    if(At<sizeof(Diagnostics)-2)snprintf(Diagnostics+At,sizeof(Diagnostics)-At,"%s\n",Message);
}
static void CompilerDefines(TCCState* T) {
#include "compiler-defines.inc"
}
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;
    xvfs V=xrtVfsCreate();xvfsdisk Disk=xrtVfsDiskCreate(BENCH_APP_ROOT,XVFS_DISK_READ);
    xvfsmount Mount=V&&Disk?xrtVfsDiskMount(V,"/app",0,XVFS_CASE_SENSITIVE,Disk,0):NULL;
    FILE* Report=fopen(BENCH_RESULT_PATH,"wb");
    if(!Mount||!Report)exit(10);
    fputs("[",Report);
    for(int Run=0;Run<BENCH_ITERATIONS;Run++) {
        Diagnostics[0]=0;double Begin=xrtTimer();
        xs_tcc_config Config;xsTccConfigInit(&Config);Config.ApplicationVfs=V;Config.ApplicationRoot=BENCH_APP_ROOT;
        // An object must not embed this process's ASLR-dependent host imports.
        if(BENCH_EMIT)Config.Flags=XS_TCC_RESTRICT_HOST_SYMBOLS;
        xerror* Error=NULL;TCCState* T=xsCreateTCCEx(&Config,&Error);
        if(Error){fprintf(stderr,"%s\n",xrtErrorMessage(Error));xrtErrorFree(Error);}
        if(!T)exit(11);
        tcc_set_error_func(T,NULL,CompilerError);
        if(BENCH_EMIT) {
            CompilerDefines(T);
            // -c changes the action without reinitializing TCC's sections.
            if(tcc_set_options(T,"-c -std=c11")<0)exit(12);
        } else if(tcc_set_options(T,"-std=c11")<0)exit(12);
#ifdef BENCH_LIBRARY_PATH
        if(tcc_add_library_path(T,BENCH_LIBRARY_PATH)<0)exit(12);
#endif
        double Created=xrtTimer();
        bool Added=true;
#ifdef BENCH_OBJECT_MANIFEST
        Added=MdoLoadCommonObjects(T);
#endif
        Added=Added&&tcc_add_file(T,BENCH_INPUT)==0;
        double AddedAt=xrtTimer();bool Linked=false;
        if(Added)Linked=BENCH_EMIT?tcc_output_file(T,BENCH_OBJECT_PATH)==0:tcc_relocate(T)==0;
        double LinkedAt=xrtTimer();
        bool Entries=BENCH_EMIT||(Linked&&tcc_get_symbol(T,"ServiceInit")&&tcc_get_symbol(T,"ServiceUnit")&&tcc_get_symbol(T,"RequestProc"));
        xsDestroyTCC(T);double Deleted=xrtTimer();
        fprintf(Report,"%s{\"create_ms\":%.6f,\"add_ms\":%.6f,\"link_ms\":%.6f,\"destroy_ms\":%.6f,\"total_ms\":%.6f,\"ok\":%s}",
            Run?",":"",(Created-Begin)*1000,(AddedAt-Created)*1000,(LinkedAt-AddedAt)*1000,(Deleted-LinkedAt)*1000,(Deleted-Begin)*1000,
            Added&&Linked&&Entries?"true":"false");
        if(!Added||!Linked||!Entries){fprintf(stderr,"compiler failure: %s\n",Diagnostics);fputs("]",Report);fclose(Report);exit(13);}
    }
    fputs("]\n",Report);fclose(Report);
    xrtVfsMountDestroy(Mount);xrtVfsDiskDestroy(Disk);xrtVfsDestroy(V);exit(0);
}
void ServiceUnit(XS_HostInfo* Host){(void)Host;}
