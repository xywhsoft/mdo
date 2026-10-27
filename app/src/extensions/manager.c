#include "../../include/mdo/extensions.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/extension_manifest.h"

#define MDO_EXT_STATE "data/extensions/state.json"
#define MDO_EXT_STATE_LIMIT (8u*1024u*1024u)
#define MDO_EXT_TASKS "data/extension-tasks"
static xmutex* MdoExtLock;
struct MdoExtensionLease {char* Key;struct MdoExtensionLease* Next;};
static MdoExtensionLease* MdoExtLeases;
static bool MdoExtBusy(cstr Kind,cstr Id){
    for(MdoExtensionLease* L=MdoExtLeases;L;L=L->Next)
        if(!strncmp(L->Key,Kind,strlen(Kind))&&L->Key[strlen(Kind)]==':'&&!strcmp(L->Key+strlen(Kind)+1,Id))return true;
    return false;
}
static bool MdoExtError(char Error[512],cstr Why){if(Error)snprintf(Error,512,"%s",Why);return false;}
static bool MdoExtMissing(void)
{const xerror* E=xrtGetError();bool Missing=E&&xrtErrorKind(E)==XERR_NOT_FOUND;if(Missing)xrtClearError();return Missing;}
static bool MdoExtPut(xvalue* V,cstr K,cstr S)
{return S&&xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S)));}
static xvalue* MdoExtReadJson(cstr Path,size_t Limit,bool Empty)
{
    xfile F=MdoHomeOpenRead(Path);if(!F)return Empty&&MdoExtMissing()?xrtValueObject():NULL;
    uint64 N=0;bool Ok=xrtFileSize(F,&N)&&N&&N<=Limit;char* Text=Ok?xrtMalloc((size_t)N+1):NULL;
    Ok=Text&&xrtReadFull(F,Text,(size_t)N,NULL);xrtClose(F);
    xjsonreadconfig C;xrtJsonReadConfigInit(&C);C.MaxInputBytes=Limit;C.MaxStringBytes=Limit;C.MaxValues=Limit<=MDO_EXTENSION_MANIFEST_BYTES?4096:100000;C.MaxDepth=Limit<=MDO_EXTENSION_MANIFEST_BYTES?16:20;
    xvalue* V=Ok&&!memchr(Text,0,(size_t)N)?xrtJsonRead(xrtStrViewN(Text,(size_t)N),&C):NULL;xrtFree(Text);return V;
}
static char* MdoExtJson(const xvalue* V,size_t Limit)
{
    xjsonwriteconfig C;xrtJsonWriteConfigInit(&C);C.MaxOutputBytes=Limit;xjsonwriter* W=xrtJsonWriterCreate(&C);char* Text=NULL;
    if(W&&xrtJsonWriterValue(W,V)&&xrtJsonWriterFinish(W))Text=xrtJsonWriterTake(W,NULL);xrtJsonWriterFree(W);return Text;
}
static bool MdoExtSaveJson(cstr Path,const xvalue* V,size_t Limit)
{char* Text=MdoExtJson(V,Limit);bool Ok=Text&&MdoHomeAtomicWrite(Path,Text,strlen(Text),false);xrtFree(Text);return Ok;}
static bool MdoExtStat(cstr Path,bool* Exists,xfileinfo* Info)
{memset(Info,0,sizeof(*Info));return MdoHomeExternalStat(Path,Exists,Info);}
static char* MdoExtKey(cstr Kind,cstr Id)
{return MdoExtensionDirectoryKind(Kind)&&MdoExtensionDirectoryId(Id)?xrtFormat("%s:%s",Kind,Id):NULL;}
char* MdoExtensionsRoot(cstr Kind,cstr Id){return MdoExtensionDirectoryPath(Kind,Id);}
static bool MdoExtCanonical(cstr Kind,cstr Id)
{
    /* A case-insensitive filesystem must not turn a differently cased store
     * ID into an unregistered, apparently editable manual extension. */
    xdir D=MdoHomeOpenDirectory(Kind,XDIR_STAT);if(!D)return false;xdirentry E;xdirnext Next;size_t Count=0;bool Found=false;
    while((Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){if(++Count>8192)break;
        if(E.Info.Type==XFILE_TYPE_DIRECTORY&&E.Name.Size==strlen(Id)&&!memcmp(E.Name.Data,Id,E.Name.Size)){Found=true;break;}}
    return xrtDirClose(D)&&Found;
}
static xvalue* MdoExtRead(cstr Kind,cstr Id,const xvalue* State,char Error[512])
{
    char* Root=MdoExtensionsRoot(Kind,Id),*Path=Root?xrtFormat("%s/extension.json",Root):NULL;
    xvalue* Meta=Path?MdoExtReadJson(Path,MDO_EXTENSION_MANIFEST_BYTES,false):NULL;
    xrtFree(Path);
    if(!Meta||!MdoExtensionManifest(Meta,Error,512)||strcmp(Kind,MdoExtensionText(Meta,"kind",32))){xrtFree(Root);xrtValueRelease(Meta);MdoExtError(Error,"Extension directory has no valid matching extension.json");return NULL;}
    const char* const Keys[]={"entry","native","frontend","readme"};bool Ok=true;
    for(size_t I=0;Ok&&I<sizeof(Keys)/sizeof(*Keys);I++){cstr P=MdoExtensionText(Meta,Keys[I],MDO_EXTENSION_PATH_BYTES);if(!P)continue;
        char* Full=xrtFormat("%s/%s",Root,P);bool Exists=false;xfileinfo Info;
        Ok=Full&&MdoExtStat(Full,&Exists,&Info)&&Exists&&Info.Type==XFILE_TYPE_FILE&&Info.Size<=MDO_EXTENSION_FILE_BYTES&&(Info.Size||!strcmp(Keys[I],"readme"));xrtFree(Full);}
    xvalue* Row=Ok?xrtValueClone(Meta):NULL;
    char* Key=MdoExtKey(Kind,Id);const xvalue* Record=Key?xrtValueObjectGet(State,xrtStrView(Key)):NULL;
    bool Enabled=true;const xvalue* Flag=xrtValueObjectGet(Record,XRT_STR_LITERAL("enabled"));if(Flag&&!xrtValueGetBool(Flag,&Enabled))Ok=false;
    Ok=Ok&&Row&&MdoExtPut(Row,"id",Id)&&MdoExtPut(Row,"root",Root)&&MdoExtPut(Row,"source",Record?MdoExtensionText(Record,"source",16):"manual")&&
        xrtValueObjectSetNew(Row,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled))&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("valid"),xrtValueBool(true));
    if(Ok&&Record)Ok=xrtValueObjectSet(Row,XRT_STR_LITERAL("installation"),Record);
    if(Ok&&Record){const char* const Credits[]={"submitter","owner","xid"};for(size_t I=0;Ok&&I<3;I++){
        const xvalue* V=xrtValueObjectGet(Record,xrtStrView(Credits[I]));if(V)Ok=xrtValueObjectSet(Row,xrtStrView(Credits[I]),V);}}
    xrtFree(Key);xrtFree(Root);xrtValueRelease(Meta);
    if(!Ok){xrtValueRelease(Row);MdoExtError(Error,"Extension entry, source record or file is unavailable");return NULL;}return Row;
}
bool MdoExtensionsInit(void)
{if(MdoExtLock)return true;MdoExtLock=xrtMutexCreate();return MdoExtLock!=NULL;}
void MdoExtensionsUnit(void){while(MdoExtLeases){MdoExtensionLease* L=MdoExtLeases;MdoExtLeases=L->Next;xrtFree(L->Key);xrtFree(L);}if(MdoExtLock)xrtMutexDestroy(MdoExtLock);MdoExtLock=NULL;}
xvalue* MdoExtensionsRead(cstr Kind,cstr Id,char Error[512])
{
    if(Error)Error[0]=0;if(!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id)){MdoExtError(Error,"Choose a type and an installed directory");return NULL;}
    if(!MdoExtLock){MdoExtError(Error,"Extension manager is not initialized");return NULL;}
    xrtMutexLock(MdoExtLock);xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);
    xvalue* Row=State&&xrtValueType(State)==XVALUE_OBJECT&&MdoExtCanonical(Kind,Id)?MdoExtRead(Kind,Id,State,Error):NULL;xrtValueRelease(State);xrtMutexUnlock(MdoExtLock);
    if(!Row&&Error&&!Error[0])MdoExtError(Error,"Cannot verify extension installation state");return Row;
}
xvalue* MdoExtensionsList(cstr Kind,char Error[512])
{
    if(Error)Error[0]=0;if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)){MdoExtError(Error,"Choose one of the nine extension types");return NULL;}
    xrtMutexLock(MdoExtLock);xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true),*Rows=xrtValueArray();
    bool Ok=State&&xrtValueType(State)==XVALUE_OBJECT&&Rows;xdir D=Ok?MdoHomeOpenDirectory(Kind,XDIR_STAT):NULL;
    if(Ok&&!D&&!MdoExtMissing())Ok=false;
    xdirentry Entry;xdirnext Next=XDIR_NEXT_END;size_t Count=0;
    while(Ok&&D&&(Next=xrtDirNext(D,&Entry))==XDIR_NEXT_ITEM){
        if(!(Entry.Flags&XDIR_ENTRY_UTF8)||Entry.Info.Type!=XFILE_TYPE_DIRECTORY||!Entry.Name.Size||Entry.Name.Size>96)continue;
        char Id[97];memcpy(Id,Entry.Name.Data,Entry.Name.Size);Id[Entry.Name.Size]=0;if(!MdoExtensionDirectoryId(Id))continue;
        if(++Count>4096){Ok=false;break;}char Why[512]={0};xvalue* Row=MdoExtRead(Kind,Id,State,Why);
        if(!Row){Row=xrtValueObject();Ok=Row&&MdoExtPut(Row,"id",Id)&&MdoExtPut(Row,"kind",Kind)&&MdoExtPut(Row,"name",Id)&&MdoExtPut(Row,"error",Why)&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("valid"),xrtValueBool(false));}
        Ok=Ok&&xrtValueArrayAppend(Rows,Row);xrtValueRelease(Row);
    }
    if(Next==XDIR_NEXT_ERROR)Ok=false;if(D&&!xrtDirClose(D))Ok=false;xrtValueRelease(State);xrtMutexUnlock(MdoExtLock);
    if(!Ok){xrtValueRelease(Rows);MdoExtError(Error,"Cannot read extension directory or installation state");return NULL;}return Rows;
}
char* MdoExtensionsEntryPath(cstr Kind,cstr Id,char Error[512])
{
    xvalue* M=MdoExtensionsRead(Kind,Id,Error);cstr Entry=MdoExtensionText(M,"entry",MDO_EXTENSION_PATH_BYTES),Root=MdoExtensionText(M,"root",MDO_EXTENSION_PATH_BYTES);
    char* Path=Entry&&Root?xrtFormat("%s/%s",Root,Entry):NULL;xrtValueRelease(M);return Path;
}
xvalue* MdoExtensionsFindAlias(cstr Kind,cstr Alias,char Error[512])
{
    if(Error)Error[0]=0;if(!MdoExtensionAlias(Alias)){MdoExtError(Error,"Choose a valid extension alias");return NULL;}
    xvalue* Rows=MdoExtensionsList(Kind,Error),*Found=NULL;bool Ambiguous=false;
    for(size_t I=0;I<xrtValueCount(Rows);I++){
        const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Name=MdoExtensionText(Row,"alias",64);bool Valid=false,Enabled=false;
        xrtValueGetBool(xrtValueObjectGet(Row,XRT_STR_LITERAL("valid")),&Valid);xrtValueGetBool(xrtValueObjectGet(Row,XRT_STR_LITERAL("enabled")),&Enabled);
        if(Name&&!strcmp(Name,Alias)&&Valid&&Enabled){if(Found){Ambiguous=true;break;}Found=xrtValueClone(Row);}
    }
    xrtValueRelease(Rows);if(Ambiguous){xrtValueRelease(Found);Found=NULL;MdoExtError(Error,"Multiple enabled directories declare the same alias");}
    if(!Found&&Error&&!Error[0])MdoExtError(Error,"No enabled extension directory declares this alias");return Found;
}
typedef struct MdoExtFile {char* Path;xfileinfo Info;bool Directory;} MdoExtFile;
typedef struct MdoExtTree {MdoExtFile* Files;size_t Count,Capacity;uint64 Bytes;} MdoExtTree;
static void MdoExtTreeUnit(MdoExtTree* T)
{for(size_t I=0;I<T->Count;I++)xrtFree(T->Files[I].Path);xrtFree(T->Files);memset(T,0,sizeof(*T));}
static bool MdoExtTreeAppend(MdoExtTree* T,cstr Path,const xfileinfo* Info)
{
    if(T->Count>=MDO_EXTENSION_DIRECTORY_ENTRIES||!MdoExtensionRelativePath(Path)||(Info->Type!=XFILE_TYPE_FILE&&Info->Type!=XFILE_TYPE_DIRECTORY)||
        (Info->Type==XFILE_TYPE_FILE&&(Info->Size>MDO_EXTENSION_FILE_BYTES||Info->Size>MDO_EXTENSION_DIRECTORY_BYTES-T->Bytes)))return false;
    if(T->Count==T->Capacity){size_t N=T->Capacity?T->Capacity*2:32;MdoExtFile* F=xrtRealloc(T->Files,N*sizeof(*F));if(!F)return false;T->Files=F;T->Capacity=N;}
    MdoExtFile* F=&T->Files[T->Count];F->Path=xrtStrDup(Path);if(!F->Path)return false;F->Info=*Info;F->Directory=Info->Type==XFILE_TYPE_DIRECTORY;T->Count++;if(!F->Directory)T->Bytes+=Info->Size;return true;
}
static int MdoExtFileCompare(const void* A,const void* B){return strcmp(((const MdoExtFile*)A)->Path,((const MdoExtFile*)B)->Path);}
static bool MdoExtCollect(cstr Root,MdoExtTree* T,xcancel* Cancel,char Error[512])
{
    xvalue* Queue=xrtValueArray();bool Ok=Queue&&xrtValueArrayAppendNew(Queue,xrtValueString(XRT_STR_LITERAL("")));
    for(size_t At=0;Ok&&At<xrtValueCount(Queue);At++){
        xstrview Relative={0};xrtValueGetString(xrtValueArrayGet(Queue,At),&Relative);char* Full=Relative.Size?xrtFormat("%s/%s",Root,Relative.Data):xrtStrDup(Root);
        xdir D=Full?MdoHomeOpenDirectory(Full,XDIR_STAT):NULL;xrtFree(Full);if(!D){Ok=false;break;}
        xdirentry E;xdirnext Next=XDIR_NEXT_END;
        while(Ok&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){
            if((Cancel&&xrtCancelRequested(Cancel))||!(E.Flags&XDIR_ENTRY_UTF8)||!E.Name.Size||memchr(E.Name.Data,0,E.Name.Size)){Ok=false;break;}
            char* Name=xrtStrDupN(E.Name.Data,E.Name.Size),*Path=Name?(Relative.Size?xrtFormat("%s/%s",Relative.Data,Name):xrtStrDup(Name)):NULL;xrtFree(Name);
            Ok=Path&&MdoExtTreeAppend(T,Path,&E.Info);if(!Ok&&Error&&Path)snprintf(Error,512,"Unsupported extension file or path: %.240s",Path);
            if(Ok&&E.Info.Type==XFILE_TYPE_DIRECTORY)Ok=xrtValueArrayAppendNew(Queue,xrtValueString(xrtStrView(Path)));xrtFree(Path);
        }
        if(Next==XDIR_NEXT_ERROR)Ok=false;if(!xrtDirClose(D))Ok=false;
    }xrtValueRelease(Queue);if(!Ok)return MdoExtError(Error,"Extension tree contains an unsupported path, link, type, size or cancelled operation");
    qsort(T->Files,T->Count,sizeof(*T->Files),MdoExtFileCompare);return true;
}
static bool MdoExtSameFile(const xfileinfo* A,const xfileinfo* B)
{
    return A->Type==B->Type&&A->Size==B->Size&&(!(A->Available&B->Available&XFILE_INFO_IDENTITY)||(A->Identity==B->Identity&&A->Device==B->Device))&&
        (!(A->Available&B->Available&XFILE_INFO_MODIFY_TIME)||A->Modified==B->Modified)&&(!(A->Available&B->Available&XFILE_INFO_CHANGE_TIME)||A->Changed==B->Changed);
}
static bool MdoExtFingerprint(cstr Root,const MdoExtTree* T,char Hash[65],xcancel* Cancel)
{
    xsha256 Sha;xrtSha256Init(&Sha);bool Ok=true;uint8 Buffer[65536],Digest[32];
    for(size_t I=0;Ok&&I<T->Count;I++){const MdoExtFile* F=&T->Files[I];uint8 Header[10];Header[0]=F->Directory?1:0;Header[1]=(F->Info.Mode&0111)?1:0;
        for(size_t J=0;J<8;J++)Header[J+2]=(uint8)(F->Info.Size>>(J*8));
        Ok=xrtSha256Update(&Sha,F->Path,strlen(F->Path)+1)&&xrtSha256Update(&Sha,Header,sizeof(Header));if(!Ok||F->Directory)continue;
        char* Path=xrtFormat("%s/%s",Root,F->Path);xfile Input=Path?MdoHomeOpenRead(Path):NULL;xrtFree(Path);xfileinfo Before,After;uint64 At=0;
        Ok=Input&&xrtFileStat(Input,&Before)&&MdoExtSameFile(&F->Info,&Before);
        while(Ok&&At<Before.Size){size_t N=Before.Size-At<sizeof(Buffer)?(size_t)(Before.Size-At):sizeof(Buffer);
            Ok=(!Cancel||!xrtCancelRequested(Cancel))&&xrtReadFull(Input,Buffer,N,NULL)&&xrtSha256Update(&Sha,Buffer,N);At+=N;}
        Ok=Ok&&xrtFileStat(Input,&After)&&MdoExtSameFile(&Before,&After);xrtClose(Input);
    }
    Ok=Ok&&(!Cancel||!xrtCancelRequested(Cancel))&&xrtSha256Final(&Sha,Digest);if(Ok)for(size_t I=0;I<32;I++)snprintf(Hash+I*2,3,"%02x",Digest[I]);return Ok;
}
bool MdoExtensionsFingerprint(cstr Kind,cstr Id,char Hash[65],xcancel* Cancel,char Error[512])
{
    if(Error)Error[0]=0;char* Root=MdoExtensionsRoot(Kind,Id);if(!Root||!MdoExtLock){xrtFree(Root);return MdoExtError(Error,"Choose an installed extension directory");}
    xrtMutexLock(MdoExtLock);MdoExtTree T={0};bool Ok=MdoExtCollect(Root,&T,Cancel,Error)&&MdoExtFingerprint(Root,&T,Hash,Cancel);MdoExtTreeUnit(&T);xrtMutexUnlock(MdoExtLock);xrtFree(Root);
    return Ok||MdoExtError(Error,"Cannot fingerprint a stable extension directory");
}
static bool MdoExtVerify(cstr Kind,cstr Id,const xvalue* State,xcancel* Cancel,char Error[512])
{
    xvalue* M=MdoExtRead(Kind,Id,State,Error);if(!M)return false;
    cstr Source=MdoExtensionText(M,"source",16);bool Ok=!Cancel||!xrtCancelRequested(Cancel);
    if(Ok&&Source&&strcmp(Source,"manual")){
        const xvalue* Record=xrtValueObjectGet(M,XRT_STR_LITERAL("installation"));cstr Expected=MdoExtensionText(Record,"directory_digest",64),Root=MdoExtensionText(M,"root",MDO_EXTENSION_PATH_BYTES);
        MdoExtTree T={0};char Actual[65];Ok=Expected&&strlen(Expected)==64&&Root&&MdoExtCollect(Root,&T,Cancel,Error)&&MdoExtFingerprint(Root,&T,Actual,Cancel)&&!strcmp(Expected,Actual);MdoExtTreeUnit(&T);
    }
    xrtValueRelease(M);return Ok||MdoExtError(Error,"Installed extension files changed after approval; reinstall before enabling or executing them");
}
bool MdoExtensionsVerify(cstr Kind,cstr Id,xcancel* Cancel,char Error[512])
{
    if(Error)Error[0]=0;if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id))return MdoExtError(Error,"Choose an installed extension directory");
    xrtMutexLock(MdoExtLock);xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);
    bool Ok=State&&xrtValueType(State)==XVALUE_OBJECT&&MdoExtCanonical(Kind,Id)&&MdoExtVerify(Kind,Id,State,Cancel,Error);xrtValueRelease(State);xrtMutexUnlock(MdoExtLock);return Ok;
}
MdoExtensionLease* MdoExtensionsAcquire(cstr Kind,cstr Id,char Error[512])
{
    if(Error)Error[0]=0;if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id)){MdoExtError(Error,"Choose an installed extension directory");return NULL;}
    xrtMutexLock(MdoExtLock);xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);
    xvalue* Row=State&&MdoExtCanonical(Kind,Id)?MdoExtRead(Kind,Id,State,Error):NULL;bool Enabled=false;
    xrtValueGetBool(xrtValueObjectGet(Row,XRT_STR_LITERAL("enabled")),&Enabled);
    bool Ok=Row&&Enabled&&MdoExtVerify(Kind,Id,State,NULL,Error);
    MdoExtensionLease* L=Ok?xrtCalloc(1,sizeof(*L)):NULL;
    if(L){L->Key=MdoExtKey(Kind,Id);if(L->Key){L->Next=MdoExtLeases;MdoExtLeases=L;}else{xrtFree(L);L=NULL;}}
    xrtValueRelease(State);xrtValueRelease(Row);xrtMutexUnlock(MdoExtLock);
    if(!L&&Error&&!Error[0])MdoExtError(Error,"Extension is missing, disabled, changed or unavailable");return L;
}
void MdoExtensionsRelease(MdoExtensionLease* L)
{
    if(!L||!MdoExtLock)return;xrtMutexLock(MdoExtLock);MdoExtensionLease** P=&MdoExtLeases;
    while(*P&&*P!=L)P=&(*P)->Next;if(*P){*P=L->Next;xrtFree(L->Key);xrtFree(L);}xrtMutexUnlock(MdoExtLock);
}
xvalue* MdoExtensionsFiles(cstr Kind,cstr Id,xcancel* Cancel,char Error[512])
{
    if(Error)Error[0]=0;if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id)){MdoExtError(Error,"Choose an installed directory");return NULL;}
    xrtMutexLock(MdoExtLock);char* Root=MdoExtensionsRoot(Kind,Id);MdoExtTree Tree={0};xvalue* Rows=xrtValueArray();
    bool Ok=Root&&Rows&&MdoExtCanonical(Kind,Id)&&MdoExtCollect(Root,&Tree,Cancel,Error);
    for(size_t I=0;Ok&&I<Tree.Count;I++){
        MdoExtFile* F=&Tree.Files[I];xvalue* Row=xrtValueObject();
        Ok=Row&&MdoExtPut(Row,"path",F->Path)&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("directory"),xrtValueBool(F->Directory))&&
            xrtValueObjectSetNew(Row,XRT_STR_LITERAL("bytes"),xrtValueUInt(F->Info.Size))&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("mode"),xrtValueUInt(F->Info.Mode))&&xrtValueArrayAppend(Rows,Row);
        xrtValueRelease(Row);
    }
    MdoExtTreeUnit(&Tree);xrtFree(Root);xrtMutexUnlock(MdoExtLock);if(!Ok){xrtValueRelease(Rows);MdoExtError(Error,"Cannot safely enumerate the extension directory");return NULL;}return Rows;
}
static bool MdoExtFileDigest(xfile File,xcancel* Cancel,char Hash[65],uint64* Size)
{
    uint8 Buffer[65536],Digest[32];xsha256 Sha;xrtSha256Init(&Sha);uint64 At=0;bool Ok=xrtFileSize(File,Size);
    while(Ok&&At<*Size){size_t N=*Size-At<sizeof(Buffer)?(size_t)(*Size-At):sizeof(Buffer);
        Ok=(!Cancel||!xrtCancelRequested(Cancel))&&xrtReadAtFull(File,At,Buffer,N,NULL)&&xrtSha256Update(&Sha,Buffer,N);At+=N;}
    Ok=Ok&&(!Cancel||!xrtCancelRequested(Cancel))&&xrtSha256Final(&Sha,Digest);if(Ok)for(size_t I=0;I<32;I++)snprintf(Hash+I*2,3,"%02x",Digest[I]);return Ok;
}
bool MdoExtensionsPack(cstr Kind,cstr Id,cstr Output,xcancel* Cancel,char Hash[65],uint64* Size,char Error[512])
{
    if(Error)Error[0]=0;
    /* Export tasks own their destination; never allow an archive inside a
     * source extension, where it could recursively package itself. */
    if(!MdoExtLock||!Output||strncmp(Output,"data/extension-tasks/",21)||!MdoExtensionRelativePath(Output))return MdoExtError(Error,"Archive output must belong to an extension task");
    xrtMutexLock(MdoExtLock);xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true),*Meta=State&&MdoExtensionDirectoryKind(Kind)&&MdoExtensionDirectoryId(Id)&&MdoExtCanonical(Kind,Id)?MdoExtRead(Kind,Id,State,Error):NULL;xrtValueRelease(State);
    if(Meta&&!strcmp(MdoExtensionText(Meta,"source",16),"store")){xrtValueRelease(Meta);Meta=NULL;MdoExtError(Error,"Store extensions cannot be exported or republished");}
    char* Root=Meta?MdoExtensionsRoot(Kind,Id):NULL;MdoExtTree T={0};char Before[65]={0},After[65]={0};
    bool Ok=Root&&MdoExtCollect(Root,&T,Cancel,Error)&&MdoExtFingerprint(Root,&T,Before,Cancel);xfile File=Ok?MdoHomeOpenWrite(Output,XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE):NULL;
    bool Created=File!=NULL;MdoZipWriter W;MdoZipWriterInit(&W,File);Ok=Ok&&File;
    for(size_t I=0;Ok&&I<T.Count;I++){MdoExtFile* F=&T.Files[I];char* Path=xrtFormat("%s/%s",Root,F->Path);xfile Input=!F->Directory&&Path?MdoHomeOpenRead(Path):NULL;xrtFree(Path);
        Ok=(F->Directory||Input)&&MdoZipWriterAdd(&W,F->Path,F->Directory,F->Info.Mode,Input,Cancel);xrtClose(Input);}
    Ok=Ok&&MdoZipWriterFinish(&W);MdoZipWriterUnit(&W);xrtClose(File);
    /* Recollect so newly added or removed files also invalidate an export. */
    MdoExtTree Current={0};Ok=Ok&&MdoExtCollect(Root,&Current,Cancel,Error)&&MdoExtFingerprint(Root,&Current,After,Cancel)&&!strcmp(Before,After);MdoExtTreeUnit(&Current);
    File=Ok?MdoHomeOpenRead(Output):NULL;if(Ok)Ok=File&&MdoExtFileDigest(File,Cancel,Hash,Size);xrtClose(File);
    if(!Ok&&Error&&!Error[0])MdoExtError(Error,"Cannot package a stable extension directory");
    /* An exclusive-create failure must not remove somebody else's file. */
    if(!Ok&&Created)MdoHomeRemove(Output,false);
    MdoExtTreeUnit(&T);xrtFree(Root);xrtValueRelease(Meta);xrtMutexUnlock(MdoExtLock);return Ok;
}
static bool MdoExtEnsure(cstr Path)
{xroot R=MdoHomeOpenStorageDirectory(Path);if(!R)return false;return xrtRootClose(R);}
static bool MdoExtRemoveTree(cstr Path)
{
    cstr Slash=strchr(Path,'/');if(!Slash)return false;
    char* Parent=xrtStrDupN(Path,(size_t)(Slash-Path));bool Allowed=Parent&&(MdoExtensionDirectoryKind(Parent)||!strncmp(Path,MDO_EXT_TASKS "/",sizeof(MDO_EXT_TASKS)));
    xroot Root=Allowed?MdoHomeOpenStorageDirectory(Parent):NULL;xrtFree(Parent);if(!Root)return false;
    cstr Leaf=Slash+1;xfileinfo Info;if(!xrtRootStat(Root,Leaf,false,&Info)){bool Missing=MdoExtMissing();xrtRootClose(Root);return Missing;}
    xvalue* Queue=xrtValueArray();bool Ok=Queue&&xrtValueArrayAppendNew(Queue,xrtValueString(xrtStrView(Leaf)));size_t Count=0;
    for(size_t At=0;Ok&&At<xrtValueCount(Queue);At++){
        xstrview Name={0};xrtValueGetString(xrtValueArrayGet(Queue,At),&Name);xdir D=xrtRootDirOpen(Root,Name.Data,XDIR_STAT);if(!D){Ok=false;break;}
        xdirentry E;xdirnext Next=XDIR_NEXT_END;
        while(Ok&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){
            if(++Count>MDO_EXTENSION_DIRECTORY_ENTRIES+128||!(E.Flags&XDIR_ENTRY_UTF8)||!E.Name.Size||memchr(E.Name.Data,0,E.Name.Size)){Ok=false;break;}
            char* Child=xrtFormat("%s/%.*s",Name.Data,(int)E.Name.Size,E.Name.Data);if(!Child){Ok=false;break;}
            /* RootRemove unlinks a leaf link; it never follows its target. */
            Ok=E.Info.Type==XFILE_TYPE_DIRECTORY?xrtValueArrayAppendNew(Queue,xrtValueString(xrtStrView(Child))):xrtRootRemove(Root,Child);xrtFree(Child);
        }if(Next==XDIR_NEXT_ERROR)Ok=false;if(!xrtDirClose(D))Ok=false;
    }
    for(size_t I=xrtValueCount(Queue);Ok&&I>0;I--){xstrview Name={0};xrtValueGetString(xrtValueArrayGet(Queue,I-1),&Name);Ok=xrtRootRemove(Root,Name.Data);}
    xrtValueRelease(Queue);xrtRootClose(Root);return Ok;
}
static bool MdoExtRecord(cstr Kind,cstr Id,const xvalue* Value)
{
    xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);char* Key=MdoExtKey(Kind,Id);
    bool Ok=State&&Key&&xrtValueType(State)==XVALUE_OBJECT;
    if(Ok){if(Value&&xrtValueType(Value)!=XVALUE_NULL)Ok=xrtValueObjectSet(State,xrtStrView(Key),Value);else xrtValueObjectRemove(State,xrtStrView(Key));}
    Ok=Ok&&MdoExtSaveJson(MDO_EXT_STATE,State,MDO_EXT_STATE_LIMIT);xrtFree(Key);xrtValueRelease(State);return Ok;
}
/* Recover from directory presence, not an in-place progress counter. The
 * journal never contains archive bytes or a caller-selected deletion path. */
static bool MdoExtRecoverTask(cstr Task,char Error[512])
{
    if(!MdoExtensionDirectoryId(Task))return MdoExtError(Error,"Invalid extension recovery task");
    char *Base=xrtFormat(MDO_EXT_TASKS "/%s",Task),*Journal=Base?xrtFormat("%s/transaction.json",Base):NULL;
    bool Exists=false;xfileinfo Info;if(!Base||!Journal||!MdoExtStat(Journal,&Exists,&Info)){xrtFree(Base);xrtFree(Journal);return false;}
    if(!Exists){xrtFree(Base);xrtFree(Journal);return true;} /* An unsealed download/export task. */
    xvalue* J=MdoExtReadJson(Journal,MDO_EXT_STATE_LIMIT,false);
    cstr Kind=MdoExtensionText(J,"kind",32),Id=MdoExtensionText(J,"id",96),Phase=MdoExtensionText(J,"phase",16),Operation=MdoExtensionText(J,"operation",16);
    bool Old=false,Known=xrtValueGetBool(xrtValueObjectGet(J,XRT_STR_LITERAL("old_exists")),&Old);
    const xvalue* Before=xrtValueObjectGet(J,XRT_STR_LITERAL("before"));
    bool Valid=Known&&Before&&(xrtValueType(Before)==XVALUE_OBJECT||xrtValueType(Before)==XVALUE_NULL)&&MdoExtensionDirectoryKind(Kind)&&MdoExtensionDirectoryId(Id)&&Phase&&
        (!strcmp(Phase,"prepared")||!strcmp(Phase,"committed"))&&Operation&&(!strcmp(Operation,"install")||!strcmp(Operation,"remove"));
    char *Root=Valid?MdoExtensionsRoot(Kind,Id):NULL,*Prior=Base?xrtFormat("%s/prior",Base):NULL,*Incoming=Base?xrtFormat("%s/incoming",Base):NULL;
    bool Ok=Root&&Prior&&Incoming;
    if(Ok&&!strcmp(Phase,"prepared")){
        bool R=false,P=false,N=false;Ok=MdoExtStat(Root,&R,&Info)&&MdoExtStat(Prior,&P,&Info)&&MdoExtStat(Incoming,&N,&Info);
        if(Ok&&P){
            if(!Old||(R&&(strcmp(Operation,"install")||N)))Ok=false;
            if(Ok&&R)Ok=MdoExtRemoveTree(Root);
            if(Ok)Ok=MdoHomeRenameNoReplace(Prior,Root);
        }else if(Ok&&Old){
            /* Without prior/, the old directory must still be at its root. */
            Ok=R&&(!strcmp(Operation,"remove")||N);
        }else if(Ok&&R){
            Ok=!strcmp(Operation,"install")&&!N&&MdoExtRemoveTree(Root);
        }
        if(Ok)Ok=MdoExtRecord(Kind,Id,Before);
    }
    if(Ok)Ok=MdoExtRemoveTree(Base);
    if(!Ok)MdoExtError(Error,"Extension transaction needs recovery; installation files and journal were preserved");
    xrtValueRelease(J);xrtFree(Base);xrtFree(Journal);xrtFree(Root);xrtFree(Prior);xrtFree(Incoming);return Ok;
}
bool MdoExtensionsRecover(char Error[512])
{
    if(Error)Error[0]=0;if(!MdoExtLock)return MdoExtError(Error,"Extension manager is not initialized");
    xrtMutexLock(MdoExtLock);xdir D=MdoHomeOpenDirectory(MDO_EXT_TASKS,XDIR_STAT);bool Ok=D||MdoExtMissing();xvalue* Tasks=xrtValueArray();xdirentry E;xdirnext Next=XDIR_NEXT_END;
    while(Ok&&D&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){if(E.Info.Type!=XFILE_TYPE_DIRECTORY||!(E.Flags&XDIR_ENTRY_UTF8)||!E.Name.Size||E.Name.Size>96)continue;
        if(xrtValueCount(Tasks)>=4096){Ok=false;break;}Ok=xrtValueArrayAppendNew(Tasks,xrtValueString(xrtStrViewN(E.Name.Data,E.Name.Size)));}
    if(Next==XDIR_NEXT_ERROR)Ok=false;if(D&&!xrtDirClose(D))Ok=false;
    for(size_t I=0;Ok&&I<xrtValueCount(Tasks);I++){xstrview Name={0};xrtValueGetString(xrtValueArrayGet(Tasks,I),&Name);Ok=MdoExtRecoverTask(Name.Data,Error);}
    xrtValueRelease(Tasks);xrtMutexUnlock(MdoExtLock);return Ok;
}
static bool MdoExtRequiresTrust(const xvalue* M)
{
    cstr Kind=MdoExtensionText(M,"kind",32),Entry=MdoExtensionText(M,"entry",MDO_EXTENSION_PATH_BYTES);size_t N=Entry?strlen(Entry):0;
    return !strcmp(Kind,"tools")||!strcmp(Kind,"commands")||!strcmp(Kind,"plugins")||!strcmp(Kind,"mcp")||MdoExtensionText(M,"native",MDO_EXTENSION_PATH_BYTES)||(N>2&&!strcmp(Entry+N-2,".c"));
}
typedef struct MdoExtExtract {xfile File;xcancel* Cancel;} MdoExtExtract;
static bool MdoExtExtractSink(const void* Data,size_t Size,void* User)
{MdoExtExtract* S=User;return (!S->Cancel||!xrtCancelRequested(S->Cancel))&&xrtWriteFull(S->File,Data,Size,NULL);}
static bool MdoExtExtractZip(MdoZip* Z,cstr Root,xcancel* Cancel,char Error[512])
{
    if(!MdoHomeCreateDirectory(Root))return false;bool Ok=true;
    for(size_t I=0;Ok&&I<Z->Count;I++){
        MdoZipEntry* E=&Z->Entries[I];char* Path=xrtFormat("%s/%s",Root,E->Path);if(!Path){Ok=false;break;}
        if(Cancel&&xrtCancelRequested(Cancel))Ok=false;
        if(Ok&&E->Directory)Ok=MdoExtEnsure(Path)&&MdoZipEntryRead(Z,I,NULL,NULL,Cancel,NULL);
        else if(Ok){MdoExtExtract S={0};S.Cancel=Cancel;S.File=MdoHomeOpenWrite(Path,XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE);
            Ok=S.File&&MdoZipEntryRead(Z,I,MdoExtExtractSink,&S,Cancel,NULL)&&xrtFlush(S.File);if(S.File&&!xrtClose(S.File))Ok=false;
#if !defined(_WIN32) && !defined(_WIN64)
            if(Ok)Ok=MdoHomeSetMode(Path,E->Mode);
#endif
        }xrtFree(Path);
    }return Ok||MdoExtError(Error,Z->Error[0]?Z->Error:"Cannot extract extension files exclusively inside the task directory");
}
static bool MdoExtPublish(cstr Kind,cstr Id,cstr Task,bool Remove,const xvalue* Record,xcancel* Cancel,xvalue** Result,char Error[512])
{
    char *Root=MdoExtensionsRoot(Kind,Id),*Base=xrtFormat(MDO_EXT_TASKS "/%s",Task),*Prior=Base?xrtFormat("%s/prior",Base):NULL,*Incoming=Base?xrtFormat("%s/incoming",Base):NULL,*Journal=Base?xrtFormat("%s/transaction.json",Base):NULL,*Key=MdoExtKey(Kind,Id);
    xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);const xvalue* Before=Key?xrtValueObjectGet(State,xrtStrView(Key)):NULL;
    bool Exists=false;xfileinfo Info;bool Ok=Root&&Base&&Prior&&Incoming&&Journal&&Key&&State&&xrtValueType(State)==XVALUE_OBJECT&&MdoExtStat(Root,&Exists,&Info);
    if(Ok&&Exists)Ok=Info.Type==XFILE_TYPE_DIRECTORY&&MdoExtCanonical(Kind,Id);
    if(Remove&&!Exists)Ok=false;
    xvalue* J=xrtValueObject();Ok=Ok&&J&&MdoExtPut(J,"kind",Kind)&&MdoExtPut(J,"id",Id)&&MdoExtPut(J,"operation",Remove?"remove":"install")&&MdoExtPut(J,"phase","prepared")&&
        xrtValueObjectSetNew(J,XRT_STR_LITERAL("old_exists"),xrtValueBool(Exists));
    if(Ok)Ok=Before?xrtValueObjectSet(J,XRT_STR_LITERAL("before"),Before):xrtValueObjectSetNew(J,XRT_STR_LITERAL("before"),xrtValueNull());
    bool Prepared=false,Committed=false;
    if(Ok)Ok=(!Cancel||!xrtCancelRequested(Cancel))&&MdoExtEnsure(Kind)&&MdoExtSaveJson(Journal,J,MDO_EXT_STATE_LIMIT);
    Prepared=Ok;
    if(Ok&&Exists)Ok=MdoHomeRenameNoReplace(Root,Prior);
    if(Ok&&!Remove)Ok=MdoHomeRenameNoReplace(Incoming,Root);
    if(Ok)Ok=(!Cancel||!xrtCancelRequested(Cancel))&&MdoExtRecord(Kind,Id,Remove?NULL:Record);
    if(Ok)Ok=MdoExtPut(J,"phase","committed")&&MdoExtSaveJson(Journal,J,MDO_EXT_STATE_LIMIT);
    Committed=Ok;
    if(!Ok&&Prepared){char Recovery[512]={0};if(!MdoExtRecoverTask(Task,Recovery))MdoExtError(Error,Recovery);}
    if(Committed){bool Clean=MdoExtRemoveTree(Base);*Result=xrtValueObject();Ok=*Result&&MdoExtPut(*Result,"kind",Kind)&&MdoExtPut(*Result,"id",Id)&&MdoExtPut(*Result,"root",Root)&&
            xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("cleanup_pending"),xrtValueBool(!Clean))&&xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("removed"),xrtValueBool(Remove));}
    if(!Ok&&Error&&!Error[0])MdoExtError(Error,"Extension directory transaction failed; previous installation was recovered when possible");
    xrtFree(Root);xrtFree(Base);xrtFree(Prior);xrtFree(Incoming);xrtFree(Journal);xrtFree(Key);xrtValueRelease(State);xrtValueRelease(J);return Ok;
}
bool MdoExtensionsInstall(cstr Kind,cstr Id,cstr Archive,const xvalue* Credits,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512])
{
    *Result=NULL;if(Error)Error[0]=0;
    if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id)||!Archive||!MdoExtensionRelativePath(Archive))return MdoExtError(Error,"Choose an extension type, directory identity and downloaded ZIP");
    xrtMutexLock(MdoExtLock);if(MdoExtBusy(Kind,Id)){xrtMutexUnlock(MdoExtLock);return MdoExtError(Error,"Extension directory is in use; stop its active calls before updating");}
    xfile Input=MdoHomeOpenRead(Archive);MdoZip Z={0};xvalue* Meta=NULL,*Record=NULL;char Digest[65]={0};uint64 Bytes=0;
    bool Ok=Input&&MdoZipOpen(&Z,Input);if(Ok)Meta=MdoExtensionZipManifest(&Z,Cancel);
    Ok=Ok&&Meta&&!strcmp(Kind,MdoExtensionText(Meta,"kind",32));
    if(Ok&&MdoExtRequiresTrust(Meta)&&!Trust){MdoExtError(Error,"Explicit code approval is required for this extension directory");Ok=false;}
    cstr Source=MdoExtensionText(Credits,"source",16);if(!Source)Source="manual";
    cstr Expected=MdoExtensionText(Credits,"sha256",64);
    Ok=Ok&&(!strcmp(Source,"manual")||!strcmp(Source,"store")||!strcmp(Source,"builtin"))&&MdoExtFileDigest(Input,Cancel,Digest,&Bytes)&&(!Expected||!strcmp(Expected,Digest));
    if(Ok&&!strcmp(Source,"store"))Ok=Expected&&strlen(Expected)==64;
    char* Task=Ok?xrtXidMakeString():NULL,*Base=Task?xrtFormat(MDO_EXT_TASKS "/%s",Task):NULL,*Incoming=Base?xrtFormat("%s/incoming",Base):NULL;
    Ok=Ok&&Task&&Base&&Incoming&&MdoExtExtractZip(&Z,Incoming,Cancel,Error);
    if(Ok){char Check[65];uint64 Current=0;Ok=MdoExtFileDigest(Input,Cancel,Check,&Current)&&Current==Bytes&&!strcmp(Check,Digest);}
    if(Ok){char DirectoryDigest[65];MdoExtTree Tree={0};Ok=MdoExtCollect(Incoming,&Tree,Cancel,Error)&&MdoExtFingerprint(Incoming,&Tree,DirectoryDigest,Cancel);MdoExtTreeUnit(&Tree);
        xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);char* Key=MdoExtKey(Kind,Id);const xvalue* Old=State&&Key?xrtValueObjectGet(State,xrtStrView(Key)):NULL;
        bool Enabled=strcmp(Kind,"plugins")!=0;const xvalue* Flag=xrtValueObjectGet(Old,XRT_STR_LITERAL("enabled"));if(Flag&&!xrtValueGetBool(Flag,&Enabled))Ok=false;
        cstr OldSource=MdoExtensionText(Old,"source",16);if(OldSource&&!strcmp(OldSource,"store")&&strcmp(Source,"store"))Ok=false;
        Record=xrtValueObject();Ok=Ok&&State&&xrtValueType(State)==XVALUE_OBJECT&&Record&&MdoExtPut(Record,"source",Source)&&MdoExtPut(Record,"sha256",Digest)&&MdoExtPut(Record,"directory_digest",DirectoryDigest)&&MdoExtPut(Record,"kind",Kind)&&MdoExtPut(Record,"id",Id)&&
            xrtValueObjectSetNew(Record,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled));xrtValueRelease(State);xrtFree(Key);
        const char* const Names[]={"owner","submitter","author","copyright","license","xid"};
        for(size_t I=0;Ok&&I<sizeof(Names)/sizeof(*Names);I++){const xvalue* V=xrtValueObjectGet(Credits,xrtStrView(Names[I]));if(V)Ok=xrtValueObjectSet(Record,xrtStrView(Names[I]),V);}}
    if(Ok)Ok=MdoExtPublish(Kind,Id,Task,false,Record,Cancel,Result,Error);
    if(!Ok&&Base){char* Journal=xrtFormat("%s/transaction.json",Base);bool Exists=false;xfileinfo Info;
        if(Journal&&MdoExtStat(Journal,&Exists,&Info)&&!Exists)(void)MdoExtRemoveTree(Base);xrtFree(Journal);}
    if(!Ok&&Error&&!Error[0])MdoExtError(Error,Z.Error[0]?Z.Error:"Extension ZIP kind, approval, source or digest check failed");
    MdoZipClose(&Z);xrtClose(Input);xrtValueRelease(Meta);xrtValueRelease(Record);xrtFree(Task);xrtFree(Base);xrtFree(Incoming);xrtMutexUnlock(MdoExtLock);return Ok;
}
bool MdoExtensionsRemove(cstr Kind,cstr Id,xvalue** Result,char Error[512])
{
    *Result=NULL;if(Error)Error[0]=0;if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id))return MdoExtError(Error,"Choose an installed extension directory");
    xrtMutexLock(MdoExtLock);if(MdoExtBusy(Kind,Id)){xrtMutexUnlock(MdoExtLock);return MdoExtError(Error,"Extension directory is in use; stop its active calls before uninstalling");}
    char* Task=xrtXidMakeString(),*Base=Task?xrtFormat(MDO_EXT_TASKS "/%s",Task):NULL;
    bool Ok=Base&&MdoExtEnsure(Base)&&MdoExtPublish(Kind,Id,Task,true,NULL,NULL,Result,Error);
    if(!Ok&&Base){char* Journal=xrtFormat("%s/transaction.json",Base);bool Exists=false;xfileinfo Info;if(Journal&&MdoExtStat(Journal,&Exists,&Info)&&!Exists)(void)MdoExtRemoveTree(Base);xrtFree(Journal);}
    xrtFree(Task);xrtFree(Base);xrtMutexUnlock(MdoExtLock);return Ok;
}
bool MdoExtensionsSetEnabled(cstr Kind,cstr Id,bool Enabled,char Error[512])
{
    if(Error)Error[0]=0;if(!MdoExtLock||!MdoExtensionDirectoryKind(Kind)||!MdoExtensionDirectoryId(Id))return MdoExtError(Error,"Choose an installed extension directory");
    xrtMutexLock(MdoExtLock);xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);xvalue* Meta=State&&MdoExtCanonical(Kind,Id)?MdoExtRead(Kind,Id,State,Error):NULL;
    char* Key=MdoExtKey(Kind,Id);const xvalue* Before=State&&Key?xrtValueObjectGet(State,xrtStrView(Key)):NULL;xvalue* Record=Before?xrtValueClone(Before):xrtValueObject();
    bool Ok=State&&xrtValueType(State)==XVALUE_OBJECT&&Record&&(Enabled?Meta!=NULL:MdoExtCanonical(Kind,Id));
    if(Ok&&Enabled)Ok=MdoExtVerify(Kind,Id,State,NULL,Error);
    if(Ok&&!Before)Ok=MdoExtPut(Record,"source","manual");
    Ok=Ok&&xrtValueObjectSetNew(Record,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled))&&MdoExtRecord(Kind,Id,Record);
    xrtValueRelease(State);xrtValueRelease(Meta);xrtValueRelease(Record);xrtFree(Key);xrtMutexUnlock(MdoExtLock);
    if(Ok){if(Error)Error[0]=0;return true;}return MdoExtError(Error,"Cannot update extension activation state");
}
