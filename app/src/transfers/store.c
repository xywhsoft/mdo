#include "../../include/mdo/time.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "internal.h"

MdoDtManager g_MdoDt;
bool MdoDtId(cstr Text) { return MdoAccountHex(Text,32u); }
bool MdoDtSessionId(cstr Text) {
    return Text && *Text && strlen(Text)<=32u && *Text!='.' && MdoDtPath(Text) && !strchr(Text,'/');
}
bool MdoDtKeys(const xvalue* Value,const char* const* Keys,size_t Count) {
    if (xrtValueType(Value)!=XVALUE_OBJECT) return false;
    for (size_t i=0;i<xrtValueCount(Value);i++) {
        xstrview Key; (void)xrtValueObjectAt(Value,i,&Key); bool Known=false;
        for (size_t j=0;j<Count;j++) if (Key.Size==strlen(Keys[j]) && !memcmp(Key.Data,Keys[j],Key.Size)) Known=true;
        if (!Known) return false;
    }
    return true;
}
xvalue* MdoDtQueue(bool Incoming) {
    xvalue* Root=MdoDtRead(MDO_DT_BASE "/queue.json",8192u);
    xvalue* Items=Root?xrtValueClone(xrtValueObjectGet(Root,xrtStrView(Incoming?"incoming":"outgoing"))):xrtValueArray();
    xrtValueRelease(Root); return Items;
}
/* Caller holds the short transfer coordinator lock. Task admission is not
 * acknowledged until both its fixed identity and this runnable index flush. */
bool MdoDtQueueChange(bool Incoming,cstr Id,bool Add) {
    if (!MdoDtId(Id)) return false;
    xvalue* Root=MdoDtRead(MDO_DT_BASE "/queue.json",8192u); bool Exists=false;
    if (!Root) {
        if (!MdoHomeExternalStat(MDO_DT_BASE "/queue.json",&Exists,NULL) || Exists) return false;
        Root=xrtValueObject(); xvalue* A=xrtValueArray(),*B=xrtValueArray();
        MdoApiValueSetTake(Root,"incoming",&A); MdoApiValueSetTake(Root,"outgoing",&B);
    }
    xvalue* Items=xrtValueObjectEdit(Root,xrtStrView(Incoming?"incoming":"outgoing"));
    bool Found=false,Ok=false;
    if (xrtValueType(Items)!=XVALUE_ARRAY || xrtValueCount(Items)>MDO_DT_TASKS) goto done;
    for (size_t i=0;i<xrtValueCount(Items);i++) {
        xstrview Value;
        if (!xrtValueGetString(xrtValueArrayGet(Items,i),&Value) || Value.Size!=32u) goto done;
        if (!memcmp(Value.Data,Id,32u)) { Found=true; if (!Add) { xrtValueArrayRemove(Items,i,1u); break; } }
    }
    if (Add && !Found && (xrtValueCount(Items)>=MDO_DT_TASKS || !MdoApiValueAppendString(Items,Id))) goto done;
    Ok=MdoDtSave(MDO_DT_BASE "/queue.json",Root);
done:
    xrtValueRelease(Root); return Ok;
}
bool MdoDtUnsigned(xstrview Text,uint64 Max,uint64* Value) {
    uint64 Number=0;
    if (!Text.Size || Text.Size>20u) return false;
    for (size_t i=0;i<Text.Size;i++) {
        unsigned char Ch=Text.Data[i];
        if (Ch<'0' || Ch>'9' || (uint64)(Ch-'0')>Max || Number>(Max-(Ch-'0'))/10u) return false;
        Number=Number*10u+(Ch-'0');
    }
    *Value=Number; return true;
}
bool MdoDtRandom(char Id[33]) {
    uint8 Bytes[16]; static const char Hex[]="0123456789abcdef";
    if (!xrtSecureRandom(Bytes,sizeof(Bytes))) return false;
    for (size_t i=0;i<16;i++) { Id[2*i]=Hex[Bytes[i]>>4]; Id[2*i+1]=Hex[Bytes[i]&15]; }
    Id[32]=0; return true;
}
bool MdoDtPath(cstr Path) {
    if (!Path || !*Path || strlen(Path)>2048u || !xrtUtf8Valid(xrtStrView(Path),NULL)) return false;
    const char* Part=Path;
    for (const char* p=Path;;p++) {
        unsigned char c=(unsigned char)*p;
        if (c && (c<32u || c==127u || strchr("\\:*?\"<>|",c))) return false;
        if (!c || c=='/') {
            size_t n=(size_t)(p-Part);
            if (!n || n>255u || (n==1u && *Part=='.') || (n==2u && !memcmp(Part,"..",2u)) ||
                Part[n-1]=='.' || Part[n-1]==' ') return false;
            char Base[9]; size_t BaseSize=0;
            while (BaseSize<n && Part[BaseSize]!='.' && BaseSize<8u) {
                char Ch=Part[BaseSize]; Base[BaseSize]=(Ch>='a' && Ch<='z')?(char)(Ch-'a'+'A'):Ch; BaseSize++;
            }
            Base[BaseSize]=0;
            if (!strcmp(Base,"CON") || !strcmp(Base,"PRN") || !strcmp(Base,"AUX") || !strcmp(Base,"NUL") ||
                (BaseSize==4u && (!strncmp(Base,"COM",3u) || !strncmp(Base,"LPT",3u)) && Base[3]>='1' && Base[3]<='9')) return false;
            if (!c) break;
            Part=p+1;
        }
    }
    return true;
}
bool MdoDtHex(const void* Data,size_t Bytes,char Hash[65]) {
    uint8 Digest[32];
    if (!xrtSha256(Data,Bytes,Digest)) return false;
    for (size_t i=0;i<32;i++) snprintf(Hash+2*i,3,"%02x",Digest[i]);
    return true;
}
bool MdoDtHash(xfile File,char Hash[65],uint64* Bytes,xcancel* Cancel) {
    uint8 Buffer[65536],Digest[32]; size_t Read; xsha256 Sha;
    *Bytes=0; xrtSha256Init(&Sha);
    if (!File || !xrtSeek(File,0,XSEEK_START,NULL)) return false;
    while (!xrtCancelRequested(Cancel)) {
        if (!xrtRead(File,Buffer,sizeof(Buffer),&Read)) return false;
        if (!Read) break;
        if (!xrtSha256Update(&Sha,Buffer,Read)) return false;
        *Bytes+=Read;
        if (Cancel) MdoDtKeepAwake(); /* HTTP sealing holds the coordinator lock. */
    }
    if (xrtCancelRequested(Cancel) || !xrtSha256Final(&Sha,Digest)) return false;
    for (size_t i=0;i<32;i++) snprintf(Hash+2*i,3,"%02x",Digest[i]);
    return true;
}
char* MdoDtBytes(cstr Path,size_t Limit,size_t* Bytes) {
    xfile File=MdoHomeOpenRead(Path); xfileinfo Info; char* Data=NULL; size_t Read=0;
    *Bytes=0;
    if (!File || !xrtFileStat(File,&Info) || Info.Type!=XFILE_TYPE_FILE || Info.Size>Limit) goto done;
    Data=xrtMalloc((size_t)Info.Size+1u);
    if (!Data || !xrtRead(File,Data,(size_t)Info.Size,&Read) || Read!=Info.Size) { xrtFree(Data); Data=NULL; goto done; }
    Data[Read]=0; *Bytes=Read;
done:
    xrtClose(File); return Data;
}
xvalue* MdoDtRead(cstr Path,size_t Limit) {
    size_t Bytes=0; char* Data=MdoDtBytes(Path,Limit,&Bytes); xvalue* Value=NULL;
    if (Data) {
        xjsonreadconfig Config; xrtJsonReadConfigInit(&Config);
        Config.MaxInputBytes=Limit; Config.MaxDepth=16u; Config.MaxValues=1600000u;
        Config.MaxContainerItems=MDO_DT_FILES+32u; Config.MaxStringBytes=4096u;
        Value=xrtJsonRead(xrtStrViewN(Data,Bytes),&Config);
    }
    xrtFree(Data); return Value;
}
bool MdoDtWrite(cstr Path,const void* Data,size_t Bytes) {
    xfile File=MdoHomeOpenWrite(Path,XFILE_WRITE|XFILE_CREATE|XFILE_TRUNCATE);
    size_t Written=0; bool Ok=File && xrtWrite(File,Data,Bytes,&Written) && Written==Bytes && xrtFlush(File);
    if (!xrtClose(File)) Ok=false;
    return Ok;
}
bool MdoDtSave(cstr Path,const xvalue* Value) {
    size_t Bytes=0; str Json=xrtJsonStringify(Value,false,&Bytes);
    bool Ok=Json && MdoHomeAtomicWrite(Path,Json,Bytes,false);
    xrtFree(Json); return Ok;
}
void MdoDtTaskPath(char Path[256],bool Incoming,cstr Id,cstr Leaf) {
    snprintf(Path,256,"%s/%s/%s/%s",MDO_DT_BASE,Incoming?"incoming":"outgoing",Id,Leaf);
}
xvalue* MdoDtTaskRead(bool Incoming,cstr Id) {
    char Path[256]; if (!MdoDtId(Id)) return NULL;
    MdoDtTaskPath(Path,Incoming,Id,"task.json"); return MdoDtRead(Path,256u*1024u);
}
bool MdoDtTaskSave(bool Incoming,const xvalue* Task) {
    cstr Id=MdoAccountText(Task,"id",32u); char Path[256];
    if (!MdoDtId(Id)) return false;
    MdoDtTaskPath(Path,Incoming,Id,"task.json"); return MdoDtSave(Path,Task);
}
bool MdoDtTaskState(xvalue* Task,cstr State,cstr Error) {
    return MdoAccountSetString(Task,"state",State) && MdoAccountSetString(Task,"error",Error?Error:"") &&
        MdoApiValueSetInt(Task,"updated_at",MdoUtcUs());
}
bool MdoDtReceiverId(char Id[33]) {
    xvalue* Value=MdoDtRead(MDO_DT_BASE "/identity.json",4096u); cstr Stored=MdoAccountText(Value,"id",32u);
    if (MdoDtId(Stored)) { strcpy(Id,Stored); xrtValueRelease(Value); return true; }
    xrtValueRelease(Value); bool Exists=false;
    if (!MdoHomeExternalStat(MDO_DT_BASE "/identity.json",&Exists,NULL) || Exists || !MdoDtRandom(Id)) return false;
    Value=xrtValueObject(); bool Ok=Value && MdoAccountSetString(Value,"id",Id) && MdoDtSave(MDO_DT_BASE "/identity.json",Value);
    xrtValueRelease(Value); return Ok;
}
bool MdoDtPrunePayload(bool Incoming,xvalue* Task) {
    cstr Id=MdoAccountText(Task,"id",32u); char Path[256]; bool Pruned=false;
    (void)xrtValueGetBool(xrtValueObjectGet(Task,XRT_STR_LITERAL("payload_pruned")),&Pruned);
    if (Pruned || !MdoDtId(Id)) return Pruned;
    MdoDtTaskPath(Path,Incoming,Id,"files"); xdir Directory=MdoHomeOpenDirectory(Path,XDIR_STAT);
    bool Ok=true; size_t Count=0; xdirentry Entry; xdirnext Next=XDIR_NEXT_END;
    if (Directory) {
        while ((Next=xrtDirNext(Directory,&Entry))==XDIR_NEXT_ITEM) {
            char Name[32],Child[256];
            if (++Count>MDO_DT_FILES*2u || Entry.Info.Type!=XFILE_TYPE_FILE || Entry.Name.Size>=sizeof(Name)) { Ok=false; break; }
            memcpy(Name,Entry.Name.Data,Entry.Name.Size); Name[Entry.Name.Size]=0;
            bool Valid=Entry.Name.Size==8u || (Entry.Name.Size==13u && !strcmp(Name+8u,".part"));
            for (size_t i=0;i<8u && Valid;i++) if (Name[i]<'0' || Name[i]>'9') Valid=false;
            if (!Valid) { Ok=false; break; }
            snprintf(Child,sizeof(Child),"%s/%s",Path,Name);
            if (!MdoHomeRemove(Child,false)) { Ok=false; break; }
        }
        if (Next==XDIR_NEXT_ERROR) Ok=false;
        if (!xrtDirClose(Directory)) Ok=false;
    }
    if (Ok) { (void)MdoHomeRemoveEmptyDirectory(Path); MdoAccountSetBool(Task,"payload_pruned",true); return MdoDtTaskSave(Incoming,Task); }
    MdoAccountSetBool(Task,"cleanup_pending",true); (void)MdoDtTaskSave(Incoming,Task); return false;
}
bool MdoDtMember(uint64 Member) {
    bool Ok=false;
    xrtMutexLock(g_MdoAccount.Lock);
    Ok=g_MdoAccount.Initialized && !g_MdoAccount.Stopping &&
        g_MdoAccount.Tokens.MemberId==Member && Member!=0u;
    xrtMutexUnlock(g_MdoAccount.Lock); return Ok;
}
bool MdoDtReceiveAllowed(void) {
    bool Allow=false; xvalue* Remote=MdoRemoteSnapshot();
    (void)xrtValueGetBool(xrtValueObjectGet(Remote,XRT_STR_LITERAL("allow_remote")),&Allow);
    xrtValueRelease(Remote);
    xvalue* Policy=MdoDtRead("config/device-transfer.json",4096u); bool Receive=false; uint64 Member=0;
    (void)xrtValueGetBool(xrtValueObjectGet(Policy,XRT_STR_LITERAL("allow_receive")),&Receive);
    (void)MdoAccountGetUInt(xrtValueObjectGet(Policy,XRT_STR_LITERAL("member_id")),&Member);
    xrtValueRelease(Policy); return Allow && Receive && MdoDtMember(Member);
}
