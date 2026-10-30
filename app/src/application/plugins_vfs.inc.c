/* One mounted provider, one atomically replaced inventory. It never mounts a
 * physical parent directory: only reviewed files and synthetic directories are
 * visible. Native handles and directory snapshots outlive an inventory swap. */
typedef struct SdkPluginDirectory {xvalue* Entries;size_t Next;} SdkPluginDirectory;
static bool SdkPluginMapAppend(xvalue* Map,cstr Target,cstr Source,cstr Id,cstr Revision,uint64 Size){
    xvalue* Row=xrtValueObject();bool Ok=Row&&SdkPut(Row,"target",Target)&&SdkPut(Row,"source",Source)&&SdkPut(Row,"id",Id)&&SdkPut(Row,"revision",Revision)&&SdkPutUInt(Row,"bytes",Size)&&xrtValueArrayAppend(Map,Row);xrtValueRelease(Row);return Ok;
}
static xvalue* SdkPluginMapBuild(SdkPlugin* Plugins){
    xvalue* Map=xrtValueArray();bool Ok=Map!=NULL;
    for(size_t I=0;Ok&&I<MDO_PLUGIN_COUNT;I++){
        const xvalue* M=Plugins[I].Manifest;if(!M||Plugins[I].Error[0])continue;
        cstr Owner=SdkField(M,"id"),Id=SdkField(M,"directory_id"),Revision=SdkField(M,"revision");const xvalue* Files=SdkGet(M,"files"),*Doc=SdkGet(M,"manifest");
        if(!SdkPluginEnabled(M))continue;
        for(size_t J=0;Ok&&J<xrtValueCount(Files);J++){
            const xvalue* F=xrtValueArrayGet(Files,J);cstr Name=SdkField(F,"path");uint64 Size=0;SdkUInt(SdkGet(F,"bytes"),&Size);
            bool Directory=false;xrtValueGetBool(SdkGet(F,"directory"),&Directory);if(Directory)continue;
            char Source[4608],Target[4608];snprintf(Source,sizeof(Source),"plugins/%s/%s",Id,Name);
            snprintf(Target,sizeof(Target),"web/__mdo_plugins/%s/%s/%s",Id,Revision,Name);
            Ok=SdkPluginMapAppend(Map,Target,Source,Owner,Revision,Size);
            snprintf(Target,sizeof(Target),"__mdo_plugins/%s/%s/%s",Id,Revision,Name);
            Ok=Ok&&SdkPluginMapAppend(Map,Target,Source,Owner,Revision,Size);
        }
        if(!SdkPluginEnabled(M)||Plugins[I].Error[0])continue;
        const xvalue* Overrides=SdkGet(Doc,"overrides");
        for(size_t J=0;Ok&&J<xrtValueCount(Overrides);J++){
            const xvalue* O=xrtValueArrayGet(Overrides,J);cstr Target=SdkField(O,"target"),Name=SdkField(O,"source");
            const xvalue* F=MdoPluginFile(Files,Name);uint64 Size=0;SdkUInt(SdkGet(F,"bytes"),&Size);
            for(size_t K=0;Ok&&K<xrtValueCount(Map);K++){
                const xvalue* Prior=xrtValueArrayGet(Map,K);cstr Other=SdkField(Prior,"id"),P=SdkField(Prior,"target");
                if(MdoPluginPathOverlap(Target,P))Ok=false;
                if(!Ok)snprintf(Plugins[I].Error,sizeof(Plugins[I].Error),"File target conflicts with %s: %s",Other,Target);
            }
            char Source[4608];snprintf(Source,sizeof(Source),"plugins/%s/%s",Id,Name);
            Ok=Ok&&SdkPluginMapAppend(Map,Target,Source,Owner,Revision,Size);
        }
    }
    if(!Ok){xrtValueRelease(Map);return NULL;}return Map;
}
static bool SdkPluginPathMatches(xstrview Path,cstr Candidate){
    return Candidate&&Path.Size==strlen(Candidate)&&!memcmp(Path.Data,Candidate,Path.Size);
}
static char* SdkPluginResolve(xstrview Path){
    char* Result=NULL;xrtMutexLock(SdkPlugins.Lock);
    for(size_t I=0;I<xrtValueCount(SdkPlugins.Map);I++){
        const xvalue* Row=xrtValueArrayGet(SdkPlugins.Map,I);
        if(SdkPluginPathMatches(Path,SdkField(Row,"target"))){Result=xrtStrDup(SdkField(Row,"source"));break;}
    }xrtMutexUnlock(SdkPlugins.Lock);return Result;
}
static bool SdkPluginFileRead(void* F,void* B,size_t N,size_t* R){return xrtRead(F,B,N,R);}
static bool SdkPluginFileReadAt(void* F,uint64 O,void* B,size_t N,size_t* R){return xrtReadAt(F,O,B,N,R);}
static bool SdkPluginFileSeek(void* F,int64 O,xseek W,uint64* P){return xrtSeek(F,O,W,P);}
static bool SdkPluginFileStat(void* F,xfileinfo* I){return xrtFileStat(F,I);}
static void SdkPluginFileClose(void* F){(void)xrtClose(F);}
static const xvfsfileops_v1 SdkPluginFileOps={sizeof(xvfsfileops_v1),1,XVFS_FILE_READ|XVFS_FILE_READ_AT|XVFS_FILE_SEEK|XVFS_FILE_STAT,SdkPluginFileRead,NULL,SdkPluginFileReadAt,NULL,SdkPluginFileSeek,SdkPluginFileStat,NULL,NULL,SdkPluginFileClose};
static xvfslookup SdkPluginOpen(void* Context,xvfscase Case,xstrview Path,const xfileoptions* Options,xvfsfile_v1* File){
    (void)Context;(void)Case;char* Source=SdkPluginResolve(Path);if(!Source)return XVFS_LOOKUP_MISS;
    if(!Options||(Options->Flags&~XFILE_READ)||!(Options->Flags&XFILE_READ)){xrtFree(Source);return XVFS_LOOKUP_ERROR;}
    xfile Native=MdoHomeOpenRead(Source);xrtFree(Source);if(!Native)return XVFS_LOOKUP_ERROR;
    File->State=Native;File->Ops=&SdkPluginFileOps;File->Flags=Options->Flags;return XVFS_LOOKUP_OPENED;
}
static bool SdkPluginPrefix(xstrview Parent,cstr Child){
    size_t N=strlen(Child);return !Parent.Size||(N>Parent.Size&&!memcmp(Parent.Data,Child,Parent.Size)&&Child[Parent.Size]=='/');
}
static xvfslookup SdkPluginStat(void* Context,xvfscase Case,xstrview Path,bool Follow,xfileinfo* Info){
    (void)Context;(void)Case;(void)Follow;bool Directory=false;char* Source=SdkPluginResolve(Path);
    if(Source){xfile F=MdoHomeOpenRead(Source);xrtFree(Source);bool Ok=F&&xrtFileStat(F,Info);if(F)Ok=xrtClose(F)&&Ok;return Ok?XVFS_LOOKUP_OPENED:XVFS_LOOKUP_ERROR;}
    xrtMutexLock(SdkPlugins.Lock);for(size_t I=0;I<xrtValueCount(SdkPlugins.Map);I++)if(SdkPluginPrefix(Path,SdkField(xrtValueArrayGet(SdkPlugins.Map,I),"target"))){Directory=true;break;}xrtMutexUnlock(SdkPlugins.Lock);
    if(!Directory)return XVFS_LOOKUP_MISS;
    memset(Info,0,sizeof(*Info));
    Info->Type=XFILE_TYPE_DIRECTORY;
    return XVFS_LOOKUP_OPENED;
}
static bool SdkPluginDirNext(void* State,xdirentry* Entry,bool* End){
    SdkPluginDirectory* D=State;*End=D->Next>=xrtValueCount(D->Entries);if(*End)return true;
    const xvalue* R=xrtValueArrayGet(D->Entries,D->Next++);xstrview Name={0};xrtValueGetString(SdkGet(R,"name"),&Name);
    memset(Entry,0,sizeof(*Entry));Entry->Name=Name;Entry->Info.Type=SdkGet(R,"directory")?XFILE_TYPE_DIRECTORY:XFILE_TYPE_FILE;
    if(Entry->Info.Type==XFILE_TYPE_FILE){Entry->Info.Available=XFILE_INFO_SIZE;SdkUInt(SdkGet(R,"bytes"),&Entry->Info.Size);}return true;
}
static void SdkPluginDirClose(void* State){SdkPluginDirectory* D=State;xrtValueRelease(D->Entries);xrtFree(D);}
static const xvfsdirops_v1 SdkPluginDirectoryOps={sizeof(xvfsdirops_v1),1,SdkPluginDirNext,SdkPluginDirClose};
static xvfslookup SdkPluginDirOpen(void* Context,xvfscase Case,xstrview Path,uint32 Flags,xvfsdir_v1* Dir){
    (void)Context;(void)Case;(void)Flags;SdkPluginDirectory* D=xrtCalloc(1,sizeof(*D));if(!D)return XVFS_LOOKUP_ERROR;
    D->Entries=xrtValueArray();xvalue* Seen=xrtValueObject();bool Found=false,Ok=D->Entries&&Seen;
    xrtMutexLock(SdkPlugins.Lock);
    for(size_t I=0;Ok&&I<xrtValueCount(SdkPlugins.Map);I++){
        const xvalue* R=xrtValueArrayGet(SdkPlugins.Map,I);cstr Target=SdkField(R,"target");if(!SdkPluginPrefix(Path,Target))continue;Found=true;
        cstr Start=Target+(Path.Size?Path.Size+1:0),End=strchr(Start,'/');size_t N=End?(size_t)(End-Start):strlen(Start);xstrview Name=xrtStrViewN(Start,N);
        if(xrtValueObjectGet(Seen,Name))continue;
        xvalue* Row=xrtValueObject();
        Ok=Row&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("name"),xrtValueString(Name))&&xrtValueObjectSetNew(Seen,Name,xrtValueBool(true));
        if(End)Ok=Ok&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("directory"),xrtValueBool(true));
        else Ok=Ok&&xrtValueObjectSet(Row,XRT_STR_LITERAL("bytes"),SdkGet(R,"bytes"));
        Ok=Ok&&xrtValueArrayAppend(D->Entries,Row);xrtValueRelease(Row);
    }xrtMutexUnlock(SdkPlugins.Lock);xrtValueRelease(Seen);
    if(!Ok||!Found){SdkPluginDirClose(D);return Ok?XVFS_LOOKUP_MISS:XVFS_LOOKUP_ERROR;}
    Dir->Ops=&SdkPluginDirectoryOps;Dir->State=D;return XVFS_LOOKUP_OPENED;
}
static const xvfsprovider_v1 SdkPluginProvider={sizeof(xvfsprovider_v1),1,XVFS_PROVIDER_STAT|XVFS_PROVIDER_DIRECTORY,NULL,NULL,SdkPluginOpen,SdkPluginStat,SdkPluginDirOpen,NULL};

/* xrt resolves the longest mount prefix first. Bind the same immutable view at
 * the Home application-resource prefix too, above its existing disk mount. */
static xstrview SdkPluginHomePath(xstrview P,char B[4608]){int N=snprintf(B,4608,"default-home%s%.*s",P.Size?"/":"",(int)P.Size,P.Data?P.Data:"");return xrtStrViewN(B,N>0&&N<4608?(size_t)N:0);}
static xvfslookup SdkPluginHomeOpen(void* C,xvfscase K,xstrview P,const xfileoptions* O,xvfsfile_v1* F){char B[4608];xstrview V=SdkPluginHomePath(P,B);return V.Size?SdkPluginOpen(C,K,V,O,F):XVFS_LOOKUP_MISS;}
static xvfslookup SdkPluginHomeStat(void* C,xvfscase K,xstrview P,bool Follow,xfileinfo* I){char B[4608];xstrview V=SdkPluginHomePath(P,B);return V.Size?SdkPluginStat(C,K,V,Follow,I):XVFS_LOOKUP_MISS;}
static xvfslookup SdkPluginHomeDir(void* C,xvfscase K,xstrview P,uint32 F,xvfsdir_v1* D){char B[4608];xstrview V=SdkPluginHomePath(P,B);return V.Size?SdkPluginDirOpen(C,K,V,F,D):XVFS_LOOKUP_MISS;}
static const xvfsprovider_v1 SdkPluginHomeProvider={sizeof(xvfsprovider_v1),1,XVFS_PROVIDER_STAT|XVFS_PROVIDER_DIRECTORY,NULL,NULL,SdkPluginHomeOpen,SdkPluginHomeStat,SdkPluginHomeDir,NULL};
