/* Only registered, enabled programs get command entries. Never search PATH or
 * execute a program to discover capabilities. state.json is the Windows
 * dispatch index as well as the durable registry, published in one rename. */
static bool CliRelativeValid(cstr P)
{return P&&strlen(P)<4096&&MdoExtensionRelativePath(P)&&!strncmp(P,"external-tools/",15);}
static char* CliRelative(cstr Path)
{
    MdoHomeSnapshot H={0};H.Size=sizeof(H);if(!MdoHomeGetSnapshot(&H)||!H.Path)return NULL;
    char* R=xrtPathRelative(xrtStrView(H.Path),xrtStrView(Path),XPATH_NATIVE);
    if(R)for(char* C=R;*C;C++)if(*C=='\\')*C='/';
    if(!CliRelativeValid(R)){xrtFree(R);return NULL;}
    /* A manual path may traverse a user symlink. Only export a portable entry
     * when the actual executable stays inside the actual Home. */
    if(CliPresent(Path)){char* Root=xrtPathReal(H.Path),*Target=xrtPathReal(Path);size_t N=Root?strlen(Root):0;while(N&&(Root[N-1]=='/'||Root[N-1]=='\\'))N--;
        bool Inside=N&&Target&&strlen(Target)>N&&(Target[N]=='/'||Target[N]=='\\');
        for(size_t I=0;Inside&&I<N;I++){unsigned char A=Root[I],B=Target[I];if(A=='\\')A='/';if(B=='\\')B='/';
#if defined(_WIN32) || defined(_WIN64)
            if(A>='A'&&A<='Z')A+=32;
            if(B>='A'&&B<='Z')B+=32;
#endif
            if(A!=B)Inside=false;}
        xrtFree(Root);xrtFree(Target);if(!Inside){xrtFree(R);return NULL;}}
    return R;
}
static bool CliAliasesValid(const xvalue* R)
{
    const xvalue* A=xrtValueObjectGet(R,XRT_STR_LITERAL("aliases"));if(!A)return true;
    size_t Base=xrtValueCount(xrtValueObjectGet(R,XRT_STR_LITERAL("commands")));if(!Base)Base=1;
    if(xrtValueType(A)!=XVALUE_ARRAY||Base+xrtValueCount(A)>64)return false;
    for(size_t I=0;I<xrtValueCount(A);I++){xstrview V;if(!xrtValueGetString(xrtValueArrayGet(A,I),&V)||memchr(V.Data,0,V.Size)||!MdoCliCommandName(V.Data))return false;}return true;
}
static bool CliStoredMapValid(const xvalue* Map)
{
    if(!Map)return true;
    if(xrtValueType(Map)!=XVALUE_OBJECT||xrtValueCount(Map)>1024)return false;
    for(size_t I=0;I<xrtValueCount(Map);I++){xstrview Name;const xvalue* Row=xrtValueObjectAt(Map,I,&Name);if(memchr(Name.Data,0,Name.Size)||!MdoCliCommandName(Name.Data)||!CliRelativeValid(CliText(Row,"target",4096))||!MdoExtensionDirectoryId(CliText(Row,"tool_id",96)))return false;
        const xvalue* Args=xrtValueObjectGet(Row,XRT_STR_LITERAL("args"));if(xrtValueType(Args)!=XVALUE_ARRAY||xrtValueCount(Args)>64)return false;
        for(size_t J=0;J<xrtValueCount(Args);J++){xstrview V;if(!xrtValueGetString(xrtValueArrayGet(Args,J),&V)||V.Size>32768||memchr(V.Data,0,V.Size))return false;}}
    return true;
}
static bool CliMapAdd(xvalue* Map,const xvalue* Tool,cstr Name,cstr Relative,const xvalue* Args)
{
    char Key[65];snprintf(Key,sizeof(Key),"%s",Name);
#if defined(_WIN32) || defined(_WIN64)
    for(char* C=Key;*C;C++)if(*C>='A'&&*C<='Z')*C+=32;
#endif
    if(!MdoCliCommandName(Key)||!CliRelativeValid(Relative))return false;
    const xvalue* Old=xrtValueObjectGet(Map,xrtStrView(Key));
    if(Old){if(!strcmp(CliText(Old,"tool_id",96),CliText(Tool,"id",96))&&!strcmp(CliText(Old,"target",4096),Relative))return true;
        snprintf(g_MdoCli.BindingError,sizeof(g_MdoCli.BindingError),"Command '%s' is already provided by %s; disable it or choose another command name",Key,CliText(Old,"tool_id",96));return false;}
    if(xrtValueCount(Map)>=1024)return false;
    char* Native=MdoHomeExternalPath(Relative);bool Present=Native&&CliPresent(Native);xrtFree(Native);
    if(!Present){snprintf(g_MdoCli.BindingError,sizeof(g_MdoCli.BindingError),"Command '%s' program does not exist: %.130s",Key,Relative);return false;}
    xvalue* Row=xrtValueObject();bool Ok=Row&&CliSet(Row,"target",Relative)&&CliSet(Row,"tool_id",CliText(Tool,"id",96));
    if(Args)Ok=Ok&&xrtValueObjectSet(Row,XRT_STR_LITERAL("args"),Args);else Ok=Ok&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("args"),xrtValueArray());
    Ok=Ok&&xrtValueObjectSet(Map,xrtStrView(Key),Row);xrtValueRelease(Row);return Ok;
}
static xvalue* CliCommandMap(void)
{
    xvalue* Map=xrtValueObject();if(!Map)return NULL;
#if !defined(__ANDROID__)
    for(size_t I=0;I<xrtValueCount(CliItems());I++){
        const xvalue* R=xrtValueArrayGet(CliItems(),I);if(!CliBool(R,"valid",false)||!CliBool(R,"enabled",true))continue;
        bool Ok=CliAliasesValid(R);cstr Root=CliText(R,"managed_root",4096),Relative=CliText(R,"relative_path",4096);
        const xvalue* Commands=xrtValueObjectGet(R,XRT_STR_LITERAL("commands"));
        if(xrtValueCount(Commands)){
            Ok=Ok&&MdoCliCommandsValid(Commands,NULL);
            for(size_t J=0;Ok&&J<xrtValueCount(Commands);J++){
                const xvalue* C=xrtValueArrayGet(Commands,J);char* Target=xrtFormat("%s/%s",Root,CliText(C,"entry",4096));
                Ok=Target&&CliMapAdd(Map,R,CliText(C,"name",64),Target,xrtValueObjectGet(C,XRT_STR_LITERAL("args")));xrtFree(Target);
            }
        }else Ok=Ok&&CliMapAdd(Map,R,CliText(R,"command",64),Relative,NULL);
        const xvalue* Aliases=xrtValueObjectGet(R,XRT_STR_LITERAL("aliases"));
        for(size_t J=0;Ok&&J<xrtValueCount(Aliases);J++){xstrview Name={0};xrtValueGetString(xrtValueArrayGet(Aliases,J),&Name);Ok=CliMapAdd(Map,R,Name.Data,Relative,NULL);}
        if(!Ok){xrtValueRelease(Map);return NULL;}
    }
#endif
    return Map;
}
static char* CliWrapper(const xvalue* Row)
{
    /* exec the real entry, rather than a raw symlink: portable Linux runtime
     * scripts derive their loader/library paths from their own $0. */
    cstr Header="#!/bin/sh\n# mdo managed command v1\ncase $0 in */*) self=$0;; *) self=$(command -v -- \"$0\") || exit 126;; esac\nbin=${self%/*}\nhome=$(CDPATH= cd -- \"$bin/../../..\" && pwd -P) || exit 126\nexec \"$home/\"";
    const xvalue* Args=xrtValueObjectGet(Row,XRT_STR_LITERAL("args"));size_t Capacity=strlen(Header)+strlen(CliText(Row,"target",4096))*4+64;
    for(size_t I=0;I<xrtValueCount(Args);I++){xstrview V={0};xrtValueGetString(xrtValueArrayGet(Args,I),&V);Capacity+=V.Size*4+4;}
    char* Out=xrtMalloc(Capacity);if(!Out)return NULL;strcpy(Out,Header);size_t At=strlen(Out);
    for(size_t I=0;I<=xrtValueCount(Args);I++){xstrview V=I?xrtStrView(""):xrtStrView(CliText(Row,"target",4096));if(I)xrtValueGetString(xrtValueArrayGet(Args,I-1),&V);if(I)Out[At++]=' ';Out[At++]='\'';
        for(size_t J=0;J<V.Size;J++){if(V.Data[J]=='\''){memcpy(Out+At,"'\\''",4);At+=4;}else Out[At++]=V.Data[J];}Out[At++]='\'';}
    memcpy(Out+At," \"$@\"\n",7);return Out;
}
static bool CliFileMatches(cstr Relative,const void* Bytes,size_t Length)
{
    if(!Bytes||!Length)return false;
    bool Exists=false;xfileinfo Info;if(!MdoHomeExternalStat(Relative,&Exists,&Info)||!Exists||Info.Type!=XFILE_TYPE_FILE||Info.Size!=Length)return false;
    xfile F=MdoHomeOpenRead(Relative);uint8 Buffer[32768];size_t At=0;bool Ok=F!=NULL;
    while(Ok&&At<Length){size_t N=0,Want=Length-At;if(Want>sizeof(Buffer))Want=sizeof(Buffer);Ok=xrtRead(F,Buffer,Want,&N)&&N==Want&&!memcmp(Buffer,(const uint8*)Bytes+At,N);At+=N;}xrtClose(F);return Ok;
}
static void CliAliasPath(char Out[128],xstrview Name)
{snprintf(Out,128,"data/external-tools/bin/%.*s%s",(int)Name.Size,Name.Data,
#if defined(_WIN32) || defined(_WIN64)
    ".exe"
#else
    ""
#endif
);}
static bool CliOwnedAlias(cstr Path,const xvalue* Row,const void* Image,size_t Size)
{
    if(!Row)return false;
#if defined(_WIN32) || defined(_WIN64)
    if(CliFileMatches(Path,Image,Size))return true;
    cstr Expected=CliText(Row,"launcher_sha256",64);if(!MdoCliHashValid(Expected))return false;
    xfile F=MdoHomeOpenRead(Path);uint64 Length=0;bool Ok=F&&xrtFileSize(F,&Length)&&Length<=2097152;uint8 Buffer[32768],Digest[32];xsha256 Sha;xrtSha256Init(&Sha);
    while(Ok){size_t N=0;Ok=xrtRead(F,Buffer,sizeof(Buffer),&N);if(!Ok||!N)break;Ok=xrtSha256Update(&Sha,Buffer,N);}xrtClose(F);
    char Hash[65];if(Ok&&xrtSha256Final(&Sha,Digest)){CliHex(Digest,Hash);return !strcmp(Expected,Hash);}return false;
#else
    (void)Image;(void)Size;char* Script=CliWrapper(Row);bool Ok=Script&&CliFileMatches(Path,Script,strlen(Script));xrtFree(Script);return Ok;
#endif
}
static bool CliPrepareBindings(xvalue* Map,const xvalue* Previous,const void* Image,size_t Size,bool Restore)
{
    xroot Root=MdoHomeOpenStorageDirectory("data/external-tools/bin");if(!Root)return false;xrtRootClose(Root);
    for(size_t I=0;I<xrtValueCount(Map);I++){
        xstrview Name;xvalue* Row=xrtValueObjectAt(Map,I,&Name);const xvalue* Old=xrtValueObjectGet(Previous,Name);if(!Old)Old=xrtValueObjectGet(xrtValueObjectGet(g_MdoCli.Published,XRT_STR_LITERAL("retired_commands")),Name);char Path[128];CliAliasPath(Path,Name);bool Exists=false;xfileinfo Info;
        if(!MdoHomeExternalStat(Path,&Exists,&Info))return false;
        if(Exists&&!CliOwnedAlias(Path,Old,Image,Size)&&!CliOwnedAlias(Path,Row,Image,Size)){if(Restore)continue;snprintf(g_MdoCli.BindingError,sizeof(g_MdoCli.BindingError),"Command entry '%.*s' is occupied by an unrecognized file; original file retained",(int)Name.Size,Name.Data);return false;}
#if defined(_WIN32) || defined(_WIN64)
        char Hash[65],Master[140];uint8 Digest[32];if(!xrtSha256(Image,Size,Digest))return false;CliHex(Digest,Hash);
        if(!Exists){snprintf(Master,sizeof(Master),"data/external-tools/forwarders/%s.exe",Hash);
            if(!CliFileMatches(Master,Image,Size)&&!MdoHomeAtomicWrite(Master,Image,Size,false))return false;
            Root=MdoHomeOpenStorageDirectory("data/external-tools");if(!Root)return false;bool Linked=xrtRootLinkHard(Root,Master+20,Path+20);xrtRootClose(Root);
            if(!Linked&&!MdoHomeAtomicWrite(Path,Image,Size,false))return false;}
        else if(!CliFileMatches(Path,Image,Size)&&!MdoHomeAtomicWrite(Path,Image,Size,false)){/* Retain the known running launcher until its process releases the file. */snprintf(Hash,sizeof(Hash),"%s",CliText(Old,"launcher_sha256",64));}
        if(!CliSet(Row,"launcher_sha256",Hash))return false;
#else
        char* Script=CliWrapper(Row);bool Ok=Script&&((Exists&&CliFileMatches(Path,Script,strlen(Script)))||MdoHomeAtomicWrite(Path,Script,strlen(Script),false));xrtFree(Script);if(!Ok||!MdoHomeSetMode(Path,0755))return false;
#endif
    }return true;
}
static void CliRemoveBindings(const xvalue* Old,const xvalue* Keep,const void* Image,size_t Size)
{
    for(size_t I=0;I<xrtValueCount(Old);I++){xstrview Name;const xvalue* Row=xrtValueObjectAt(Old,I,&Name);if(!MdoCliCommandName(Name.Data)||xrtValueObjectGet(Keep,Name))continue;char Path[128];CliAliasPath(Path,Name);if(CliOwnedAlias(Path,Row,Image,Size))(void)MdoHomeRemove(Path,false);}
}
static xvalue* CliRetiredBindings(const xvalue* Map,const xvalue* Previous)
{
    const xvalue* Retired=xrtValueObjectGet(g_MdoCli.Published,XRT_STR_LITERAL("retired_commands"));xvalue* Next=xrtValueObject();if(!Next)return NULL;
    for(size_t I=0;I<xrtValueCount(Retired);I++){xstrview Name;const xvalue* Row=xrtValueObjectAt(Retired,I,&Name);if(xrtValueObjectGet(Map,Name))continue;char Path[128];CliAliasPath(Path,Name);bool Exists=false;xfileinfo Info;if(!MdoHomeExternalStat(Path,&Exists,&Info)||Exists)xrtValueObjectSet(Next,Name,Row);}
    for(size_t I=0;I<xrtValueCount(Previous);I++){xstrview Name;const xvalue* Row=xrtValueObjectAt(Previous,I,&Name);if(!xrtValueObjectGet(Map,Name))xrtValueObjectSet(Next,Name,Row);}
    if(xrtValueCount(Next)>1024){xrtValueRelease(Next);return NULL;}return Next;
}
static bool CliSave(void)
{
    g_MdoCli.BindingError[0]=0;xvalue* Map=CliCommandMap();if(!Map)return false;
    xvalue* Old=xrtValueObjectGet(g_MdoCli.Published,XRT_STR_LITERAL("commands"));bytes Image=NULL;size_t Size=0;bool Ok=true;
#if defined(_WIN32) || defined(_WIN64)
    if(xrtValueCount(Map)||xrtValueCount(Old)){Image=xrtVfsReadAllLimit(MdoHomeApplicationVfs(),"/app/generated/native/windows/cli-forwarder.exe",2097152,&Size);if(!Image||Size<2||Image[0]!='M'||Image[1]!='Z'){snprintf(g_MdoCli.BindingError,sizeof(g_MdoCli.BindingError),"Windows command forwarder is missing from the application VFS");Ok=false;}}
#endif
#if !defined(__ANDROID__)
    if(Ok)Ok=CliPrepareBindings(Map,Old,Image,Size,false);
#endif
    xvalue* Saved=Ok?xrtValueDeepClone(g_MdoCli.State):NULL;if(!Saved)Ok=false;
    if(Ok){xvalue* Retired=CliRetiredBindings(Map,Old);Ok=Retired&&xrtValueObjectSetNew(Saved,XRT_STR_LITERAL("retired_commands"),Retired);}
    if(Ok){xrtValueObjectSet(Saved,XRT_STR_LITERAL("commands"),Map);xvalue* Items=xrtValueObjectGet(Saved,XRT_STR_LITERAL("items"));
        for(size_t I=0;I<xrtValueCount(Items);I++){xvalue* R=xrtValueArrayGet(Items,I);char* Relative=CliRelative(CliText(R,"path",4096));if(Relative){CliSet(R,"relative_path",Relative);xrtValueObjectRemove(R,XRT_STR_LITERAL("path"));}else xrtValueObjectRemove(R,XRT_STR_LITERAL("relative_path"));xrtFree(Relative);}
        size_t Length=0;char* Json=xrtJsonStringify(Saved,false,&Length);xjsonreadconfig Check;xrtJsonReadConfigInit(&Check);Check.MaxInputBytes=1048576;Check.MaxValues=50000;Check.MaxDepth=24;Check.MaxStringBytes=65536;
        xvalue* Verified=Json&&Length<=1048576?xrtJsonRead(xrtStrViewN(Json,Length),&Check):NULL;Ok=Verified!=NULL;xrtFree(Json);xrtValueRelease(Verified);
        if(!Ok)snprintf(g_MdoCli.BindingError,sizeof(g_MdoCli.BindingError),"Tool registry exceeds command index resource limits");
        if(Ok)Ok=MdoDistSave("data/external-tools/state.json",Saved);}
    if(Ok){xrtValueObjectSet(g_MdoCli.State,XRT_STR_LITERAL("commands"),Map);xrtValueObjectSet(g_MdoCli.State,XRT_STR_LITERAL("retired_commands"),xrtValueObjectGet(Saved,XRT_STR_LITERAL("retired_commands")));CliRemoveBindings(xrtValueObjectGet(Saved,XRT_STR_LITERAL("retired_commands")),Map,Image,Size);xrtValueRelease(g_MdoCli.Published);g_MdoCli.Published=Saved;Saved=NULL;}
    else{
#if !defined(_WIN32) && !defined(_WIN64) && !defined(__ANDROID__)
        if(!CliPrepareBindings(Old,Map,Image,Size,true))g_MdoCli.Writable=false;
#endif
        CliRemoveBindings(Map,Old,Image,Size);if(!g_MdoCli.BindingError[0])snprintf(g_MdoCli.BindingError,sizeof(g_MdoCli.BindingError),"Cannot publish portable command entries; check file permissions and storage");}
    xrtValueRelease(Saved);xrtValueRelease(Map);xrtFree(Image);return Ok;
}
static bool CliPathInit(void)
{
#if !defined(__ANDROID__)
    g_MdoCli.OriginalPath=xrtEnvGet("PATH");char* Bin=MdoHomeExternalPath("data/external-tools/bin");if(!Bin)return false;
    size_t N=strlen(Bin)+(g_MdoCli.OriginalPath?strlen(g_MdoCli.OriginalPath):0)+2;g_MdoCli.ProcessPath=xrtMalloc(N);
    if(g_MdoCli.ProcessPath)snprintf(g_MdoCli.ProcessPath,N,"%s%c%s",Bin,
#if defined(_WIN32) || defined(_WIN64)
        ';',
#else
        ':',
#endif
        g_MdoCli.OriginalPath?g_MdoCli.OriginalPath:"");xrtFree(Bin);return g_MdoCli.ProcessPath&&xrtEnvSet("PATH",g_MdoCli.ProcessPath);
#else
    return true;
#endif
}
static void CliPathUnit(void)
{
    char* Current=xrtEnvGet("PATH");if(Current&&g_MdoCli.ProcessPath&&!strcmp(Current,g_MdoCli.ProcessPath)){if(g_MdoCli.OriginalPath)xrtEnvSet("PATH",g_MdoCli.OriginalPath);else xrtEnvRemove("PATH");}xrtFree(Current);xrtFree(g_MdoCli.ProcessPath);xrtFree(g_MdoCli.OriginalPath);
}
