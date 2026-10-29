/* Sequential, bounded process logs shared by exec, spawn and the host SDK.
 * Ordinary writes enter the OS cache; no per-chunk durable flush is required.
 * File-backed output retains the prefix, never overwrites it with a tail. */
#define XWORK_OUTPUT_INLINE (12u * 1024u)
#define XWORK_OUTPUT_MAX (64u * 1024u * 1024u)
#define XWORK_OUTPUT_STORE_MAX (1024u * 1024u * 1024u)
#define XWORK_OUTPUT_AGE (7u * 24u * 60u * 60u)

typedef struct xwork_output_stream {
    xfile File;
    xwork_buf Preview;
    char* Path;
    char* Locator;
    uint64_t Received;
    uint64_t Stored;
    uint64_t FileBytes;
    bool Explicit;
    bool Lost;
} xwork_output_stream;

struct xwork_process_output {
    xmutex* Lock;
    xroot Root;
    xroot Run;
    xfile Lease;
    char Id[33];
    char Error[96];
    int SystemCode;
    size_t InlineBytes;
    uint64_t MaxBytes;
    uint64_t Stored;
    bool FilesFromStart;
    bool MergeStderr;
    bool Finished;
    bool Reserved;
    struct xwork_process_output* ActiveNext;
    xwork_output_stream Streams[2];
};

/* POSIX fcntl locks are process-scoped: a second handle in this process can
 * acquire them, and closing that handle releases the process's lock. Protect
 * local collectors/writers with one mutex and skip local active leases before
 * opening them; native file locks then protect us against other processes. */
static xonce xwork__output_once;
static xmutex* xwork__output_gate;
static xwork_process_output* xwork__output_active;
static bool xwork__output_init(void* Unused)
{(void)Unused;xwork__output_gate=xrtMutexCreate();return xwork__output_gate!=NULL;}
static bool xwork__output_local_lock(void)
{return xrtOnce(&xwork__output_once,xwork__output_init,NULL)&&xrtMutexLock(xwork__output_gate);}
static bool xwork__output_local_active(const char* Id)
{for(xwork_process_output* O=xwork__output_active;O;O=O->ActiveNext)if(!strcmp(O->Id,Id))return true;return false;}

static bool xwork__output_name(const char* Name)
{
    size_t i;
    if (!Name || strlen(Name)!=32u) return false;
    for(i=0;i<32u;i++) if(!((Name[i]>='0'&&Name[i]<='9')||(Name[i]>='a'&&Name[i]<='f'))) return false;
    return true;
}

static bool xwork__output_put(xvalue* V,const char* Key,const char* Text)
{return xrtValueObjectSetNew(V,xrtStrView(Key),xrtValueString(xrtStrView(Text?Text:"")));}
static bool xwork__output_uint(xvalue* V,const char* Key,uint64_t N)
{return xrtValueObjectSetNew(V,xrtStrView(Key),xrtValueUInt(N));}
static bool xwork__output_bool(xvalue* V,const char* Key,bool B)
{return xrtValueObjectSetNew(V,xrtStrView(Key),xrtValueBool(B));}
static bool xwork__output_get_uint(const xvalue* V,const char* Key,uint64_t* N)
{
    const xvalue* P=xrtValueObjectGet(V,xrtStrView(Key));int64_t I=0;
    return xrtValueGetUInt(P,N)||(xrtValueGetInt(P,&I)&&I>=0&&(*N=(uint64_t)I,true));
}

static void xwork__output_error(xwork_process_output* O,const char* Reason)
{
    if(!O->Error[0]) {
        snprintf(O->Error,sizeof(O->Error),"%s",Reason);
        O->SystemCode=xrtGetError()?xrtErrorSystemCode(xrtGetError()):0;
    }
}

static xfile xwork__output_file(xroot Root,const char* Name,bool Exclusive)
{
    xfileoptions Opt;xrtFileOptionsInit(&Opt);
    Opt.Flags=XFILE_READ|XFILE_WRITE|XFILE_CREATE|XFILE_NOFOLLOW|(Exclusive?XFILE_EXCLUSIVE:0u);
    Opt.Mode=0600u;Opt.Share=XFILE_SHARE_READ|XFILE_SHARE_WRITE|XFILE_SHARE_DELETE;
    return xrtRootFileOpen(Root,Name,&Opt);
}

static xroot xwork__output_root(xroot Root)
{
    xfileinfo Info;
    if(!Root) return NULL;
    if(!xrtRootStat(Root,"process",false,&Info)) {
        if(xrtErrorKind(xrtGetError())!=XERR_NOT_FOUND) return NULL;
        if(!xrtRootDirCreate(Root,"process",0700u)&&xrtErrorKind(xrtGetError())!=XERR_EXISTS) return NULL;
    } else if(Info.Type!=XFILE_TYPE_DIRECTORY) return NULL;
    return xrtRootOpenIn(Root,"process");
}

/* A collector holds the quota lock. An active writer keeps a separate lifetime
 * lock, so an idle/quiet command cannot be mistaken for an expired log. */
typedef struct xwork_output_old {
    char Name[33];
    uint64_t Bytes;
    int64_t Ended;
} xwork_output_old;

static int xwork__output_old_compare(const void* A,const void* B)
{
    const xwork_output_old* X=A;const xwork_output_old* Y=B;
    return X->Ended<Y->Ended?-1:X->Ended>Y->Ended?1:strcmp(X->Name,Y->Name);
}

static bool xwork__output_receipt_read(xroot Run,uint64_t* Limit,int64_t* Ended)
{
    char Text[256];xfileoptions Opt;xrtFileOptionsInit(&Opt);Opt.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile F=xrtRootFileOpen(Run,".receipt.json",&Opt);if(!F)return false;
    size_t N=0;bool Read=xrtRead(F,Text,sizeof(Text)-1u,&N);xrtClose(F);if(!Read||!N||N==sizeof(Text)-1u)return false;
    Text[N]=0;xvalue* V=xrtJsonParse(xrtStrView(Text));uint64_t Stamp=0,Format=0;
    bool Ok=V&&xwork__output_get_uint(V,"format",&Format)&&Format==1u&&
        xwork__output_get_uint(V,"limit",Limit)&&xwork__output_get_uint(V,"ended_ms",&Stamp)&&Stamp<=INT64_MAX;
    if(Ok)*Ended=(int64_t)Stamp;xrtValueRelease(V);
    if(Ok) {
        F=xrtRootFileOpen(Run,".finished.json",&Opt);
        if(F) {
            N=0;Read=xrtRead(F,Text,sizeof(Text)-1u,&N);xrtClose(F);
            if(Read&&N&&N<sizeof(Text)-1u) {
                Text[N]=0;V=xrtJsonParse(xrtStrView(Text));
                if(V&&xwork__output_get_uint(V,"ended_ms",&Stamp)&&Stamp<=INT64_MAX)*Ended=(int64_t)Stamp;
                xrtValueRelease(V);
            }
        }
    }
    return Ok;
}

static uint64_t xwork__output_scan(xroot Root,uint64_t Target,uint64_t MaxAgeSeconds,bool* Ok)
{
    xwork_output_old* Old=NULL;size_t Count=0,Cap=0;uint64_t Total=0;int64_t Now=xllmUnixUs()/1000;
    size_t Visited=0;xdir Dir=xrtRootDirOpen(Root,".",0);xdirentry Entry;xdirnext Next=XDIR_NEXT_END;*Ok=Dir!=NULL;
    while(Dir&&(Next=xrtDirNext(Dir,&Entry))==XDIR_NEXT_ITEM) {
        if(++Visited>8192u){*Ok=false;break;}
        if(!(Entry.Flags&XDIR_ENTRY_UTF8)||Entry.Name.Size!=32u)continue;
        char Name[33];memcpy(Name,Entry.Name.Data,32u);Name[32]=0;
        if(!xwork__output_name(Name))continue;
        xfileinfo Info;if(!xrtRootStat(Root,Name,false,&Info)||Info.Type!=XFILE_TYPE_DIRECTORY)continue;
        xroot Run=xrtRootOpenIn(Root,Name);if(!Run){*Ok=false;continue;}
        uint64_t Limit=0,Bytes=0;int64_t Ended=0;
        if(!xwork__output_receipt_read(Run,&Limit,&Ended)||!Limit||Limit>XWORK_OUTPUT_STORE_MAX){xrtRootClose(Run);continue;}
        bool Local=xwork__output_local_active(Name);
        xfile Lease=Local?NULL:xwork__output_file(Run,".active.lock",false);
        bool Active=Local||!Lease||!xrtFileLock(Lease,XFILE_LOCK_EXCLUSIVE,false);
        if(Active) Bytes=Limit;
        else {
            /* Re-read completion only after the lifetime lock is acquired. A
             * finishing writer may have been publishing its immutable marker. */
            (void)xwork__output_receipt_read(Run,&Limit,&Ended);
            if(xrtRootStat(Run,"stdout.log",false,&Info)&&Info.Type==XFILE_TYPE_FILE)Bytes+=Info.Size;
            if(xrtRootStat(Run,"stderr.log",false,&Info)&&Info.Type==XFILE_TYPE_FILE)Bytes+=Info.Size;
            if(!Ended) { /* Unlocked unfinished receipt: process owner has gone. */
                if(xrtRootStat(Run,".receipt.json",false,&Info))Ended=Info.Modified;
                else Ended=Now;
            }
            if(Count==Cap) {size_t Next=Cap?Cap*2u:32u;void* P=realloc(Old,Next*sizeof(*Old));if(!P){*Ok=false;xrtClose(Lease);xrtRootClose(Run);break;}Old=P;Cap=Next;}
            memcpy(Old[Count].Name,Name,33u);Old[Count].Bytes=Bytes;Old[Count++].Ended=Ended;
        }
        if(UINT64_MAX-Total<Bytes){*Ok=false;Total=UINT64_MAX;}else Total+=Bytes;
        if(Lease)xrtClose(Lease);xrtRootClose(Run);
        if(Count>4096u){*Ok=false;break;}
    }
    if(Next==XDIR_NEXT_ERROR)*Ok=false;if(Dir)xrtDirClose(Dir);
    if(Count>1u)qsort(Old,Count,sizeof(*Old),xwork__output_old_compare);
    for(size_t i=0;i<Count;i++) {
        bool Expired=MaxAgeSeconds&&Now>=Old[i].Ended&&(uint64_t)(Now-Old[i].Ended)/1000u>=MaxAgeSeconds;
        if(Total<=Target&&!Expired)continue;
        xroot Run=xrtRootOpenIn(Root,Old[i].Name);if(!Run)continue;
        xfile Lease=xwork__output_file(Run,".active.lock",false);
        bool Removed=false;
        if(Lease&&xrtFileLock(Lease,XFILE_LOCK_EXCLUSIVE,false)) {
            /* Only our fixed files are removed, never recursive arbitrary content. */
            bool A=xrtRootRemove(Run,"stdout.log")||xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;
            bool B=xrtRootRemove(Run,"stderr.log")||xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;
            if(A&&B) {
                bool C=xrtRootRemove(Run,".finished.json")||xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;
                if(C)Removed=xrtRootRemove(Run,".receipt.json");
            }
        }
        if(Lease)xrtClose(Lease);
        if(Removed){(void)xrtRootRemove(Run,".active.lock");Total=Total>=Old[i].Bytes?Total-Old[i].Bytes:0;}
        xrtRootClose(Run);if(Removed)(void)xrtRootRemove(Root,Old[i].Name);
    }
    free(Old);return Total;
}

bool xworkProcessOutputCollect(xroot Directory,uint64_t MaxBytes,uint64_t MaxAgeSeconds)
{
    if(!xwork__output_local_lock())return false;
    xroot Root=Directory?xrtRootOpenIn(Directory,"process"):NULL;
    if(!Root){bool Missing=Directory&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;xrtMutexUnlock(xwork__output_gate);return Missing;}
    xfile Gate=xwork__output_file(Root,".quota.lock",false);bool Ok=false;
    if(Gate&&xrtFileLock(Gate,XFILE_LOCK_EXCLUSIVE,true)) {
        uint64_t Total=xwork__output_scan(Root,MaxBytes?MaxBytes:XWORK_OUTPUT_STORE_MAX,MaxAgeSeconds,&Ok);
        Ok=Ok&&Total<=(MaxBytes?MaxBytes:XWORK_OUTPUT_STORE_MAX);
    }
    if(Gate)xrtClose(Gate);xrtRootClose(Root);xrtMutexUnlock(xwork__output_gate);return Ok;
}

static bool xwork__output_receipt(xwork_process_output* O)
{
    if(!O->Run)return true;
    char Text[192];int N=snprintf(Text,sizeof(Text),"{\"format\":1,\"limit\":%llu,\"ended_ms\":%llu}\n",
        (unsigned long long)O->MaxBytes,(unsigned long long)(O->Finished?xllmUnixUs()/1000:0));
    xfileoptions Opt;xrtFileOptionsInit(&Opt);Opt.Flags=XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE|XFILE_NOFOLLOW;
    Opt.Mode=0600u;Opt.Share=XFILE_SHARE_READ;
    xfile F=xrtRootFileOpen(O->Run,O->Finished?".finished.json":".receipt.json",&Opt);
    bool Ok=F&&N>0&&xrtWriteFull(F,Text,(size_t)N,NULL);if(F)xrtClose(F);return Ok;
}

static bool xwork__output_reserve(xwork_process_output* O)
{
    if(O->Reserved)return true;
    if(!xwork__output_local_lock())return false;
    xroot Root=xwork__output_root(O->Root);if(!Root){xrtMutexUnlock(xwork__output_gate);return false;}
    xfile Gate=xwork__output_file(Root,".quota.lock",false);bool Ok=false;
    if(!Gate||!xrtFileLock(Gate,XFILE_LOCK_EXCLUSIVE,true))goto done;
    bool Scanned=false;uint64_t Total=xwork__output_scan(Root,XWORK_OUTPUT_STORE_MAX-O->MaxBytes,XWORK_OUTPUT_AGE,&Scanned);
    if(!Scanned||Total>XWORK_OUTPUT_STORE_MAX-O->MaxBytes){xwork__output_error(O,"store_limit");goto done;}
    uint8_t Random[16];if(!xrtSecureRandom(Random,sizeof(Random)))goto done;
    for(size_t i=0;i<sizeof(Random);i++) {static const char Hex[]="0123456789abcdef";O->Id[i*2u]=Hex[Random[i]>>4];O->Id[i*2u+1u]=Hex[Random[i]&15u];}
    if(!xrtRootDirCreate(Root,O->Id,0700u))goto done;
    O->Run=xrtRootOpenIn(Root,O->Id);if(!O->Run)goto done;
    O->Lease=xwork__output_file(O->Run,".active.lock",true);
    if(!O->Lease||!xrtFileLock(O->Lease,XFILE_LOCK_EXCLUSIVE,false)||!xwork__output_receipt(O))goto done;
    O->Reserved=true;O->ActiveNext=xwork__output_active;xwork__output_active=O;Ok=true;
done:
    if(Gate)xrtClose(Gate);xrtRootClose(Root);xrtMutexUnlock(xwork__output_gate);return Ok;
}

static bool xwork__output_open(xwork_process_output* O,size_t Index)
{
    xwork_output_stream* S=&O->Streams[Index];if(S->File)return true;
    if(S->Explicit) {
        xfileoptions Opt;xrtFileOptionsInit(&Opt);
        Opt.Flags=XFILE_READ|XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE|XFILE_NOFOLLOW;
        Opt.Mode=0600u;Opt.Share=XFILE_SHARE_READ;
        S->File=xrtFileOpen(S->Path,&Opt);return S->File!=NULL;
    }
    if(!xwork__output_reserve(O))return false;
    const char* Name=Index?"stderr.log":"stdout.log";
    S->File=xwork__output_file(O->Run,Name,true);if(!S->File)return false;
    char Relative[96];snprintf(Relative,sizeof(Relative),"process/%s/%s",O->Id,Name);
    char* Native=xrtPathJoin(xrtRootPath(O->Root),Relative);
    S->Path=Native?xwork__strdup(Native):NULL;if(Native)xrtFree(Native);
    char Locator[112];snprintf(Locator,sizeof(Locator),"@results/%s",Relative);S->Locator=xwork__strdup(Locator);
    return S->Path&&S->Locator;
}

void xworkProcessOutputOptionsInit(xwork_process_output_options* Options)
{
    if(!Options)return;memset(Options,0,sizeof(*Options));Options->Size=sizeof(*Options);
    Options->InlineBytes=XWORK_OUTPUT_INLINE;Options->MaxBytes=XWORK_OUTPUT_MAX;
}

xwork_process_output* xworkProcessOutputCreate(const xwork_process_output_options* Options,xwork_error* Error)
{
    xwork_process_output* O=NULL;xworkErrorInit(Error);
    if(!Options||Options->Size!=sizeof(*Options)||Options->InlineBytes<256u||Options->InlineBytes>1024u*1024u||
        !Options->MaxBytes||Options->MaxBytes>XWORK_OUTPUT_STORE_MAX||
        (Options->MergeStderr&&Options->StderrPath))goto invalid;
    if(Options->StdoutPath&&Options->StderrPath&&!strcmp(Options->StdoutPath,Options->StderrPath))goto invalid;
    O=calloc(1,sizeof(*O));if(!O)goto memory;
    O->Lock=xrtMutexCreate();if(!O->Lock)goto memory;
    O->Root=Options->Directory?xrtRootOpenIn(Options->Directory,"."):NULL;
    O->InlineBytes=Options->InlineBytes;O->MaxBytes=Options->MaxBytes;
    O->FilesFromStart=Options->FilesFromStart;O->MergeStderr=Options->MergeStderr;
    for(size_t i=0;i<2u;i++) {
        const char* Path=i?Options->StderrPath:Options->StdoutPath;
        if(Path) {O->Streams[i].Explicit=true;O->Streams[i].Path=xwork__strdup(Path);if(!O->Streams[i].Path)goto memory;}
        if(i&&O->MergeStderr)continue;
        if((O->FilesFromStart||Path)&&!xwork__output_open(O,i))goto io;
    }
    return O;
invalid:xwork__set_error(Error,XWORK_ERROR_INVALID_ARGUMENT,"invalid process output options or overlapping destinations");return NULL;
memory:xwork__set_error(Error,XWORK_ERROR_OUT_OF_MEMORY,"cannot allocate process output storage");goto fail;
io:xwork__set_error(Error,XWORK_ERROR_IO,"cannot exclusively create process output files (existing files are preserved)");
fail:
    if(O)for(size_t i=0;i<2u;i++)if(O->Streams[i].Explicit&&O->Streams[i].File){
        xrtClose(O->Streams[i].File);O->Streams[i].File=NULL;(void)xrtFileDelete(O->Streams[i].Path);
    }
    if(O)xworkProcessOutputDestroy(O);return NULL;
}

bool xworkProcessOutputWrite(xwork_process_output* O,bool Stderr,const void* Data,size_t Size)
{
    if(!O||(!Data&&Size)||!xrtMutexLock(O->Lock))return false;
    size_t Index=Stderr&&!O->MergeStderr?1u:0u;xwork_output_stream* S=&O->Streams[Index];
    if(O->Finished){xrtMutexUnlock(O->Lock);return false;}
    S->Received=UINT64_MAX-S->Received<Size?UINT64_MAX:S->Received+Size;
    size_t Keep=S->Lost?0u:Size;
    if((uint64_t)Keep>O->MaxBytes-O->Stored){Keep=(size_t)(O->MaxBytes-O->Stored);S->Lost=true;xwork__output_error(O,"output_limit");}
    if(S->Lost&&O->Stored==O->MaxBytes)Keep=0;
    if(!S->File&&S->Preview.iLen+Keep>O->InlineBytes/3u) {
        if(!xwork__output_open(O,Index)) {S->Lost=true;xwork__output_error(O,"output_open_failed");Keep=0;}
        else if(S->Preview.iLen) {
            size_t Written=0;
            bool Ok=xrtWriteAtFull(S->File,0,S->Preview.pData,S->Preview.iLen,&Written);
            S->FileBytes=Written;
            if(!Ok){S->Lost=true;xwork__output_error(O,"output_write_failed");Keep=0;}
        }
    }
    if(Keep&&S->File) {
        size_t Written=0;bool Ok=xrtWriteAtFull(S->File,S->Stored,Data,Keep,&Written);
        S->FileBytes+=Written;
        if(!Ok){S->Lost=true;xwork__output_error(O,"output_write_failed");Keep=Written;}
    }
    if(Keep) {
        size_t PreviewKeep=O->InlineBytes/3u>S->Preview.iLen?O->InlineBytes/3u-S->Preview.iLen:0;
        if(PreviewKeep>Keep)PreviewKeep=Keep;
        if(PreviewKeep&&!xwork__buf_append(&S->Preview,Data,PreviewKeep)) {
            S->Lost=true;xwork__output_error(O,"output_memory_failed");
            if(!S->File)Keep=0;
        }
        S->Stored+=Keep;O->Stored+=Keep;
    }
    xrtMutexUnlock(O->Lock);
    /* Continue draining on quota/IO failure: a stopped reader would deadlock a
     * writer. The snapshot makes any incomplete capture explicit. */
    return true;
}

void xworkProcessOutputFinish(xwork_process_output* O)
{
    if(!O||!O->Lock||!xrtMutexLock(O->Lock))return;
    if(!O->Finished) {
        O->Finished=true;
        for(size_t i=0;i<2u;i++)if(O->Streams[i].File){
            if(!xrtClose(O->Streams[i].File)){O->Streams[i].Lost=true;xwork__output_error(O,"output_close_failed");}
            O->Streams[i].File=NULL;
        }
        if(xwork__output_local_lock()) {
            if(!xwork__output_receipt(O))xwork__output_error(O,"receipt_write_failed");
            xwork_process_output** Link=&xwork__output_active;
            while(*Link&&*Link!=O)Link=&(*Link)->ActiveNext;
            if(*Link==O)*Link=O->ActiveNext;
            if(O->Lease){xrtClose(O->Lease);O->Lease=NULL;}
            xrtMutexUnlock(xwork__output_gate);
        }
    }
    xrtMutexUnlock(O->Lock);
}

static xvalue* xwork__output_stream_value(xwork_process_output* O,size_t Index,bool IncludeText)
{
    xwork_output_stream* S=&O->Streams[Index];xvalue* V=xrtValueObject();if(!V)return NULL;
    bool Ok=xwork__output_uint(V,"bytes",S->Received)&&xwork__output_uint(V,"saved_bytes",S->Path?S->FileBytes:S->Stored)&&
        xwork__output_bool(V,"complete",O->Finished&&!S->Lost)&&xwork__output_bool(V,"preview_omitted",S->Path?S->Stored>1024u:S->Stored>S->Preview.iLen)&&
        xwork__output_put(V,"path",S->Locator?S->Locator:S->Path)&&xwork__output_put(V,"native_path",S->Path);
    if(Ok&&IncludeText) {
        size_t Head=S->Path&&S->Stored>1024u&&S->Preview.iLen>512u?512u:S->Preview.iLen;
        xwork_buf Text={0};Ok=xwork__buf_append_process_text(&Text,S->Preview.pData,Head);
        if(Ok&&S->Stored>Head&&S->Path) {
            xfileoptions Opt;xrtFileOptionsInit(&Opt);Opt.Flags=XFILE_READ|XFILE_NOFOLLOW;
            xfile F=S->Explicit?xrtFileOpen(S->Path,&Opt):
                xrtRootFileOpen(O->Run,Index?"stderr.log":"stdout.log",&Opt);
            unsigned char Tail[512];size_t N=S->Stored-Head>sizeof(Tail)?sizeof(Tail):(size_t)(S->Stored-Head);
            size_t Got=0;
            if(F&&xrtReadAtFull(F,S->Stored-N,Tail,N,&Got))Ok=xwork__buf_append_cstr(&Text,"\n...[preview omitted; read file for full output]...\n")&&xwork__buf_append_process_text(&Text,Tail,Got);
            if(F)xrtClose(F);
        }
        if(Ok)Ok=xwork__output_put(V,"text",Text.pData);xwork__buf_unit(&Text);
    }
    if(!Ok){xrtValueRelease(V);return NULL;}return V;
}

xvalue* xworkProcessOutputSnapshot(xwork_process_output* O,bool IncludeText)
{
    if(!O||!xrtMutexLock(O->Lock))return NULL;
    xvalue* V=xrtValueObject(),*Out=xwork__output_stream_value(O,0u,IncludeText),*Err=xwork__output_stream_value(O,1u,IncludeText);
    bool Ok=V&&Out&&Err;
    if(Ok){Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("stdout"),Out);Out=NULL;}
    if(Ok){Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("stderr"),Err);Err=NULL;}
    if(Ok)Ok=xwork__output_bool(V,"output_complete",O->Finished&&!O->Error[0])&&
        xwork__output_bool(V,"stderr_merged",O->MergeStderr)&&xwork__output_put(V,"output_error",O->Error)&&
        xwork__output_uint(V,"output_system_code",O->SystemCode<0?0u:(uint64_t)O->SystemCode);
    xrtMutexUnlock(O->Lock);xrtValueRelease(Out);xrtValueRelease(Err);
    if(!Ok){xrtValueRelease(V);V=NULL;}return V;
}

void xworkProcessOutputDestroy(xwork_process_output* O)
{
    if(!O)return;xworkProcessOutputFinish(O);
    for(size_t i=0;i<2u;i++){xwork__buf_unit(&O->Streams[i].Preview);free(O->Streams[i].Path);free(O->Streams[i].Locator);}
    if(O->Lease)xrtClose(O->Lease);if(O->Run)xrtRootClose(O->Run);if(O->Root)xrtRootClose(O->Root);
    if(O->Lock)xrtMutexDestroy(O->Lock);free(O);
}
