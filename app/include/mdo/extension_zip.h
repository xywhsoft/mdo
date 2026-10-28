#ifndef MDO_EXTENSION_ZIP_H
#define MDO_EXTENSION_ZIP_H
/* Standard ZIP transport. Entries are streams, never JSON/Base64 values.
 * The caller owns file handles and supplies rooted extraction destinations. */
#include "extension_path.h"
#include <stdlib.h>

typedef struct MdoZipEntry {
    char *Path,*Key;
    uint64 Offset,Data,End,Packed,Bytes;
    uint32 Crc,Mode;
    uint16 Method,Flags;
    bool Directory;
} MdoZipEntry;
typedef struct MdoZip {
    xfile File;uint64 Bytes,Central,Total;size_t Count;MdoZipEntry* Entries;char Error[256];
} MdoZip;
typedef bool (*MdoZipSink)(const void*,size_t,void*);
static uint16 MdoZip16(const uint8* P){return (uint16)(P[0]|((uint16)P[1]<<8));}
static uint32 MdoZip32(const uint8* P){return (uint32)P[0]|((uint32)P[1]<<8)|((uint32)P[2]<<16)|((uint32)P[3]<<24);}
static uint64 MdoZip64(const uint8* P){return MdoZip32(P)|((uint64)MdoZip32(P+4)<<32);}
static void MdoZipSet16(uint8* P,uint16 V){P[0]=(uint8)V;P[1]=(uint8)(V>>8);}
static void MdoZipSet32(uint8* P,uint32 V){for(size_t I=0;I<4;I++)P[I]=(uint8)(V>>(I*8));}
static bool MdoZipFail(MdoZip* Z,cstr Why){snprintf(Z->Error,sizeof(Z->Error),"%s",Why);return false;}
static bool MdoZipReadAt(MdoZip* Z,uint64 At,void* Data,size_t N)
{return At<=Z->Bytes&&N<=Z->Bytes-At&&xrtReadAtFull(Z->File,At,Data,N,NULL);}
static void MdoZipClose(MdoZip* Z)
{if(!Z)return;for(size_t I=0;I<Z->Count;I++){xrtFree(Z->Entries[I].Path);xrtFree(Z->Entries[I].Key);}xrtFree(Z->Entries);Z->Entries=NULL;Z->Count=0;}
static int MdoZipKeyCompare(const void* A,const void* B)
{return strcmp((*(MdoZipEntry* const*)A)->Key,(*(MdoZipEntry* const*)B)->Key);}
static int MdoZipOffsetCompare(const void* A,const void* B)
{uint64 X=(*(MdoZipEntry* const*)A)->Offset,Y=(*(MdoZipEntry* const*)B)->Offset;return X<Y?-1:X>Y?1:0;}
static size_t MdoZipLower(MdoZipEntry** Order,size_t Count,cstr Key,size_t Length)
{
    size_t Low=0,High=Count;
    while(Low<High){size_t Mid=Low+(High-Low)/2;int C=strncmp(Order[Mid]->Key,Key,Length);
        if(!C&&Order[Mid]->Key[Length])C=1;if(C<0)Low=Mid+1;else High=Mid;}return Low;
}
static bool MdoZipPaths(MdoZip* Z)
{
    MdoZipEntry** Order=xrtMalloc(Z->Count*sizeof(*Order));if(!Order)return MdoZipFail(Z,"Cannot allocate ZIP index");
    for(size_t I=0;I<Z->Count;I++)Order[I]=&Z->Entries[I];qsort(Order,Z->Count,sizeof(*Order),MdoZipKeyCompare);
    bool Ok=true;
    for(size_t I=0;Ok&&I<Z->Count;I++){
        MdoZipEntry* E=Order[I];if(I&&!strcmp(Order[I-1]->Key,E->Key)){Ok=false;break;}
        cstr Raw=E->Path,Fold=E->Key;
        for(;;){Raw=strchr(Raw,'/');Fold=strchr(Fold,'/');if(!Raw||!Fold)break;
            size_t R=(size_t)(Raw-E->Path),N=(size_t)(Fold-E->Key),At=MdoZipLower(Order,Z->Count,E->Key,N);
            if(At<Z->Count&&strlen(Order[At]->Key)==N&&!strncmp(Order[At]->Key,E->Key,N)&&
                (!Order[At]->Directory||strlen(Order[At]->Path)!=R||strncmp(Order[At]->Path,E->Path,R))){Ok=false;break;}
            /* Include the slash in the search: a-b must not obscure a/a.
             * All descendants agree on the spelling of implicit parents. */
            At=MdoZipLower(Order,Z->Count,E->Key,N+1);
            if(At==Z->Count||strncmp(Order[At]->Key,E->Key,N+1)||strncmp(Order[At]->Path,E->Path,R)||Order[At]->Path[R]!='/'){Ok=false;break;}
            Raw++;Fold++;
        }
    }
    xrtFree(Order);return Ok||MdoZipFail(Z,"Duplicate, Unicode-alias or conflicting ZIP paths");
}
/* Parse recognized extended fields; do not let another ZIP implementation
 * extract a Unicode alternate name different from the reviewed name. */
static bool MdoZipExtra(const uint8* P,size_t N,cstr Name,uint64* Bytes,uint64* Packed,uint64* Offset)
{
    bool B=*Bytes==UINT32_MAX,C=*Packed==UINT32_MAX,O=Offset&&*Offset==UINT32_MAX;
    while(N){if(N<4)return false;uint16 Id=MdoZip16(P),L=MdoZip16(P+2);P+=4;N-=4;if(L>N)return false;
        if(Id==1){size_t At=0;if(B){if(L-At<8)return false;*Bytes=MdoZip64(P+At);At+=8;B=false;}
            if(C){if(L-At<8)return false;*Packed=MdoZip64(P+At);At+=8;C=false;}
            if(O){if(L-At<8)return false;*Offset=MdoZip64(P+At);O=false;}}
        if(Id==0x9901)return false;
        if(Id==0x7075&&(L<5||P[0]!=1||MdoZip32(P+1)!=xrtCrc32(Name,strlen(Name))||strlen(Name)!=L-5u||memcmp(P+5,Name,L-5u)))return false;
        P+=L;N-=L;
    }return !B&&!C&&!O;
}
static bool MdoZipLocal(MdoZip* Z,MdoZipEntry* E)
{
    uint8 H[30];if(E->Offset>=Z->Central||!MdoZipReadAt(Z,E->Offset,H,sizeof(H))||MdoZip32(H)!=0x04034b50u||
        MdoZip16(H+6)!=E->Flags||MdoZip16(H+8)!=E->Method)return MdoZipFail(Z,"ZIP local header mismatch");
    size_t Name=MdoZip16(H+26),Extra=MdoZip16(H+28),Expected=strlen(E->Path)+(E->Directory?1:0);
    if(Name!=Expected||E->Offset+30u+Name+Extra>Z->Central)return MdoZipFail(Z,"Invalid ZIP local path");
    uint8* Text=xrtMalloc(Name+Extra+1);if(!Text)return false;bool Ok=MdoZipReadAt(Z,E->Offset+30,Text,Name+Extra);
    if(!Ok){xrtFree(Text);return MdoZipFail(Z,"Cannot read ZIP local name");}
    if(Ok)Ok=!memcmp(Text,E->Path,strlen(E->Path))&&(!E->Directory||Text[Name-1]=='/');
    uint64 Bytes=MdoZip32(H+22),Packed=MdoZip32(H+18);char* Full=xrtMalloc(Name+1);
    if(Full){memcpy(Full,Text,Name);Full[Name]=0;}else Ok=false;
    if(Ok)Ok=MdoZipExtra(Text+Name,Extra,Full,&Bytes,&Packed,NULL);
    xrtFree(Full);xrtFree(Text);if(!Ok)return MdoZipFail(Z,"Invalid ZIP local extra fields");
    if((!(E->Flags&8)&&(MdoZip32(H+14)!=E->Crc||Bytes!=E->Bytes||Packed!=E->Packed))||
        ((E->Flags&8)&&((Bytes&&Bytes!=E->Bytes)||(Packed&&Packed!=E->Packed)||(MdoZip32(H+14)&&MdoZip32(H+14)!=E->Crc))))return MdoZipFail(Z,"ZIP local sizes differ from inventory");
    E->Data=E->Offset+30u+Name+Extra;
    if(E->Data>Z->Central||E->Packed>Z->Central-E->Data)return MdoZipFail(Z,"ZIP data lies outside archive");
    E->End=E->Data+E->Packed;
    if(E->Flags&8){uint8 D[16];size_t N=Z->Central-E->End>=16?16:Z->Central-E->End>=12?12:0;
        if(!N||!MdoZipReadAt(Z,E->End,D,N))return MdoZipFail(Z,"ZIP descriptor is missing");
        if(N==16&&MdoZip32(D)==0x08074b50u&&MdoZip32(D+4)==E->Crc&&MdoZip32(D+8)==E->Packed&&MdoZip32(D+12)==E->Bytes)E->End+=16;
        else if(MdoZip32(D)==E->Crc&&MdoZip32(D+4)==E->Packed&&MdoZip32(D+8)==E->Bytes)E->End+=12;
        else return MdoZipFail(Z,"ZIP descriptor mismatch");
    }return true;
}
static bool MdoZipOpenContainer(MdoZip* Z,xfile File,bool Apk)
{
    memset(Z,0,sizeof(*Z));Z->File=File;
    if(!File||!xrtFileSize(File,&Z->Bytes)||Z->Bytes<22||Z->Bytes>(Apk?UINT64_C(2147483648):MDO_EXTENSION_ARCHIVE_BYTES))return MdoZipFail(Z,"Invalid ZIP size");
    size_t TailSize=Z->Bytes<65557?(size_t)Z->Bytes:65557;uint8* Tail=xrtMalloc(TailSize);if(!Tail)return false;
    if(!MdoZipReadAt(Z,Z->Bytes-TailSize,Tail,TailSize)){xrtFree(Tail);return MdoZipFail(Z,"Cannot read ZIP directory");}
    size_t At=TailSize-22;bool Found=false;
    for(;;){if(MdoZip32(Tail+At)==0x06054b50u&&MdoZip16(Tail+At+20)==TailSize-At-22){Found=true;break;}if(!At)break;At--;}
    if(!Found){xrtFree(Tail);return MdoZipFail(Z,"ZIP directory is missing or has trailing bytes");}
    uint8* End=Tail+At;uint64 CentralSize=MdoZip32(End+12);Z->Central=MdoZip32(End+16);Z->Count=MdoZip16(End+10);
    bool Ok=!MdoZip16(End+4)&&!MdoZip16(End+6)&&MdoZip16(End+8)==Z->Count&&Z->Count&&Z->Count<=MDO_EXTENSION_DIRECTORY_ENTRIES&&
        CentralSize<=Z->Bytes&&Z->Central<=Z->Bytes-CentralSize&&Z->Central+CentralSize==Z->Bytes-TailSize+At;
    xrtFree(Tail);if(!Ok){Z->Count=0;return MdoZipFail(Z,"Unsupported or oversized ZIP directory");}
    Z->Entries=xrtCalloc(Z->Count,sizeof(*Z->Entries));if(!Z->Entries){Z->Count=0;return false;}
    uint64 Position=Z->Central;
    for(size_t I=0;I<Z->Count;I++){
        uint8 H[46];MdoZipEntry* E=&Z->Entries[I];
        if(Position>Z->Central+CentralSize||Z->Central+CentralSize-Position<46||!MdoZipReadAt(Z,Position,H,sizeof(H))||MdoZip32(H)!=0x02014b50u)goto bad;
        size_t Name=MdoZip16(H+28),Extra=MdoZip16(H+30),Comment=MdoZip16(H+32);
        if(!Name||Name>MDO_EXTENSION_PATH_BYTES+1||Name+Extra+Comment>Z->Central+CentralSize-Position-46||MdoZip16(H+34))goto bad;
        E->Flags=MdoZip16(H+8);E->Method=MdoZip16(H+10);E->Crc=MdoZip32(H+16);E->Packed=MdoZip32(H+20);E->Bytes=MdoZip32(H+24);E->Offset=MdoZip32(H+42);
        if((E->Flags&~(0x800u|8u|6u))||(E->Method!=0&&E->Method!=8)||(E->Method==0&&(E->Flags&6)))goto bad;
        uint8* Text=xrtMalloc(Name+Extra+1);if(!Text)goto bad;
        if(!MdoZipReadAt(Z,Position+46,Text,Name+Extra)||memchr(Text,0,Name)){xrtFree(Text);goto bad;}
        E->Path=xrtStrDupN((cstr)Text,Name);if(!E->Path){xrtFree(Text);goto bad;}
        Ok=MdoZipExtra(Text+Name,Extra,E->Path,&E->Bytes,&E->Packed,&E->Offset);xrtFree(Text);if(!Ok)goto bad;
        E->Directory=E->Path[Name-1]=='/';if(E->Directory)E->Path[Name-1]=0;
        uint32 Attributes=MdoZip32(H+38),Unix=Attributes>>16,Type=Unix&0170000;
        if((Type&&Type!=0100000&&Type!=0040000)||(Type==0040000&&!E->Directory)||(Type==0100000&&E->Directory)||((Attributes&0x10)&&!E->Directory))goto bad;
        E->Mode=E->Directory?0755:(Unix&0111)?0755:0644;
        E->Key=MdoExtensionPathKey(E->Path);if(!E->Key||E->Bytes>MDO_EXTENSION_FILE_BYTES||E->Packed>MDO_EXTENSION_ARCHIVE_BYTES||E->Bytes>MDO_EXTENSION_DIRECTORY_BYTES-Z->Total||
            (E->Directory&&(E->Bytes||E->Crc))||(E->Method==0&&E->Bytes!=E->Packed))goto bad;
        Z->Total+=E->Bytes;
        if(!MdoZipLocal(Z,E))goto failed;
        Position+=46u+Name+Extra+Comment;
    }
    if(Position!=Z->Central+CentralSize||!MdoZipPaths(Z))goto bad;
    {MdoZipEntry** Order=xrtMalloc(Z->Count*sizeof(*Order));if(!Order)goto bad;
        for(size_t I=0;I<Z->Count;I++)Order[I]=&Z->Entries[I];qsort(Order,Z->Count,sizeof(*Order),MdoZipOffsetCompare);
        uint64 Next=0;for(size_t I=0;I<Z->Count;I++){if(Order[I]->Offset!=Next){xrtFree(Order);goto bad;}Next=Order[I]->End;}
        xrtFree(Order);
        if(Next!=Z->Central){
            /* Only the signed APK container may have its standard signing
             * block immediately before the central directory. Extension ZIPs
             * remain strictly contiguous. PackageManager verifies signatures. */
            uint8 Head[8],Tail[24];uint64 Gap=Z->Central-Next;
            if(!Apk||Next>Z->Central||Gap<32||Gap>16u*1024u*1024u||
               !MdoZipReadAt(Z,Next,Head,8)||!MdoZipReadAt(Z,Z->Central-24,Tail,24)||
               MdoZip64(Head)!=Gap-8||MdoZip64(Tail)!=Gap-8||memcmp(Tail+8,"APK Sig Block 42",16))goto bad;
        }}
    return true;
bad:MdoZipFail(Z,"Invalid ZIP entries, paths, types, sizes or overlapping data");
failed:MdoZipClose(Z);return false;
}
static bool MdoZipOpen(MdoZip* Z,xfile File){return MdoZipOpenContainer(Z,File,false);}
typedef struct MdoZipDecode {MdoZipSink Sink;void* User;uint64 Bytes,Limit;uint32 Crc;xsha256 Sha;} MdoZipDecode;
static bool MdoZipDecoded(xbytesview B,void* User)
{
    MdoZipDecode* D=User;if(B.Size>D->Limit-D->Bytes)return false;
    D->Crc=xrtCrc32Update(D->Crc,B.Data,B.Size);D->Bytes+=B.Size;
    return xrtSha256Update(&D->Sha,B.Data,B.Size)&&(!D->Sink||D->Sink(B.Data,B.Size,D->User));
}
static bool MdoZipEntryRead(MdoZip* Z,size_t Index,MdoZipSink Sink,void* User,xcancel* Cancel,char Hash[65])
{
    if(Index>=Z->Count)return MdoZipFail(Z,"ZIP entry not found");MdoZipEntry* E=&Z->Entries[Index];
    MdoZipDecode D={0};D.Sink=Sink;D.User=User;D.Limit=E->Bytes;xrtSha256Init(&D.Sha);
    xinflate* Inflate=NULL;bool Ok=true;uint8 Buffer[65536];uint64 At=0;
    if(E->Method==8){xinflateconfig C;xrtInflateConfigInit(&C);C.Format=XINFLATE_RAW;C.OutputLimit=E->Bytes?E->Bytes:1;Inflate=xrtInflateCreate(&C);Ok=Inflate!=NULL;}
    while(Ok&&At<E->Packed){size_t N=E->Packed-At<sizeof(Buffer)?(size_t)(E->Packed-At):sizeof(Buffer);
        Ok=(!Cancel||!xrtCancelRequested(Cancel))&&MdoZipReadAt(Z,E->Data+At,Buffer,N);
        At+=N;if(Ok)Ok=Inflate?xrtInflateWrite(Inflate,(xbytesview){Buffer,N},At==E->Packed,MdoZipDecoded,&D):MdoZipDecoded((xbytesview){Buffer,N},&D);}
    if(Ok&&Inflate&&!E->Packed)Ok=xrtInflateWrite(Inflate,(xbytesview){NULL,0},true,MdoZipDecoded,&D);
    if(Inflate){Ok=Ok&&xrtInflateDone(Inflate);xrtInflateDestroy(Inflate);}
    uint8 Digest[32];Ok=Ok&&(!Cancel||!xrtCancelRequested(Cancel))&&D.Bytes==E->Bytes&&D.Crc==E->Crc&&xrtSha256Final(&D.Sha,Digest);
    if(Ok&&Hash){for(size_t I=0;I<32;I++)snprintf(Hash+2*I,3,"%02x",Digest[I]);}
    return Ok||MdoZipFail(Z,"ZIP file CRC, decoded size, compressed stream or cancellation check failed");
}
static bool MdoZipValidate(MdoZip* Z,xcancel* Cancel)
{for(size_t I=0;I<Z->Count;I++)if(!MdoZipEntryRead(Z,I,NULL,NULL,Cancel,NULL))return false;return true;}
static int64 MdoZipFind(const MdoZip* Z,cstr Path)
{for(size_t I=0;I<Z->Count;I++)if(!strcmp(Z->Entries[I].Path,Path))return (int64)I;return -1;}

typedef struct MdoZipWriter {MdoZip Index;size_t Capacity;uint64 Position;bool Failed;} MdoZipWriter;
static bool MdoZipWrite(MdoZipWriter* W,const void* P,size_t N)
{
    size_t Written=0;if(W->Failed||N>MDO_EXTENSION_ARCHIVE_BYTES-W->Position||!xrtWrite(W->Index.File,P,N,&Written)||Written!=N){W->Failed=true;return false;}
    W->Position+=N;return true;
}
static bool MdoZipCompressed(xbytesview B,void* User){return MdoZipWrite(User,B.Data,B.Size);}
static void MdoZipWriterInit(MdoZipWriter* W,xfile Output){memset(W,0,sizeof(*W));W->Index.File=Output;}
static bool MdoZipWriterAdd(MdoZipWriter* W,cstr Path,bool Directory,uint32 Mode,xfile Input,xcancel* Cancel)
{
    if(W->Failed||W->Index.Count>=MDO_EXTENSION_DIRECTORY_ENTRIES||!MdoExtensionRelativePath(Path))goto bad;
    uint64 Size=0;if((!Directory&&(!Input||!xrtFileSize(Input,&Size)))||Size>MDO_EXTENSION_FILE_BYTES||Size>MDO_EXTENSION_DIRECTORY_BYTES-W->Index.Total)goto bad;
    if(W->Index.Count==W->Capacity){size_t C=W->Capacity?W->Capacity*2:32;if(C>MDO_EXTENSION_DIRECTORY_ENTRIES)C=MDO_EXTENSION_DIRECTORY_ENTRIES;
        MdoZipEntry* New=xrtRealloc(W->Index.Entries,C*sizeof(*New));if(!New)goto bad;memset(New+W->Capacity,0,(C-W->Capacity)*sizeof(*New));W->Capacity=C;W->Index.Entries=New;}
    MdoZipEntry* E=&W->Index.Entries[W->Index.Count++];E->Path=xrtStrDup(Path);E->Key=MdoExtensionPathKey(Path);E->Directory=Directory;E->Bytes=Size;E->Mode=Directory?0755:(Mode&0111)?0755:0644;E->Offset=W->Position;E->Method=Directory?0:8;E->Flags=0x800|(Directory?0:8);
    if(!E->Path||!E->Key)goto bad;
    uint8 H[30]={0};MdoZipSet32(H,0x04034b50u);MdoZipSet16(H+4,20);MdoZipSet16(H+6,E->Flags);MdoZipSet16(H+8,E->Method);MdoZipSet16(H+12,33);MdoZipSet16(H+26,(uint16)(strlen(Path)+(Directory?1:0)));
    if(!MdoZipWrite(W,H,sizeof(H))||!MdoZipWrite(W,Path,strlen(Path))||(Directory&&!MdoZipWrite(W,"/",1)))goto bad;
    E->Data=W->Position;
    if(!Directory){xdeflateconfig C;xrtDeflateConfigInit(&C);C.Format=XDEFLATE_RAW;C.Level=6;C.OutputLimit=MDO_EXTENSION_ARCHIVE_BYTES-W->Position;
        xdeflate* Deflate=xrtDeflateCreate(&C);if(!Deflate)goto bad;uint8 Buffer[65536];uint64 At=0;bool Ok=true;
        while(Ok&&At<Size){size_t N=Size-At<sizeof(Buffer)?(size_t)(Size-At):sizeof(Buffer);
            Ok=(!Cancel||!xrtCancelRequested(Cancel))&&xrtReadAtFull(Input,At,Buffer,N,NULL);if(!Ok)break;
            E->Crc=xrtCrc32Update(E->Crc,Buffer,N);At+=N;Ok=xrtDeflateWrite(Deflate,(xbytesview){Buffer,N},At==Size?XDEFLATE_FLUSH_FINISH:XDEFLATE_FLUSH_NONE,MdoZipCompressed,W);}
        if(Ok&&!Size)Ok=xrtDeflateWrite(Deflate,(xbytesview){NULL,0},XDEFLATE_FLUSH_FINISH,MdoZipCompressed,W);
        uint64 After=0;Ok=Ok&&xrtDeflateDone(Deflate)&&xrtFileSize(Input,&After)&&After==Size&&(!Cancel||!xrtCancelRequested(Cancel));xrtDeflateDestroy(Deflate);if(!Ok)goto bad;
        E->Packed=W->Position-E->Data;uint8 D[16];MdoZipSet32(D,0x08074b50u);MdoZipSet32(D+4,E->Crc);MdoZipSet32(D+8,(uint32)E->Packed);MdoZipSet32(D+12,(uint32)E->Bytes);if(!MdoZipWrite(W,D,sizeof(D)))goto bad;
    }
    E->End=W->Position;W->Index.Total+=Size;return true;
bad:W->Failed=true;return MdoZipFail(&W->Index,"Cannot write ZIP entry: path, input, cancellation or size check failed");
}
static bool MdoZipWriterFinish(MdoZipWriter* W)
{
    if(W->Failed||!W->Index.Count||!MdoZipPaths(&W->Index))return false;uint64 Start=W->Position;
    for(size_t I=0;I<W->Index.Count;I++){MdoZipEntry* E=&W->Index.Entries[I];uint8 H[46]={0};MdoZipSet32(H,0x02014b50u);MdoZipSet16(H+4,(3u<<8)|20u);MdoZipSet16(H+6,20);MdoZipSet16(H+8,E->Flags);MdoZipSet16(H+10,E->Method);MdoZipSet16(H+14,33);
        MdoZipSet32(H+16,E->Crc);MdoZipSet32(H+20,(uint32)E->Packed);MdoZipSet32(H+24,(uint32)E->Bytes);MdoZipSet16(H+28,(uint16)(strlen(E->Path)+(E->Directory?1:0)));
        MdoZipSet32(H+38,((E->Mode|(E->Directory?0040000:0100000))<<16)|(E->Directory?0x10:0));MdoZipSet32(H+42,(uint32)E->Offset);
        if(!MdoZipWrite(W,H,sizeof(H))||!MdoZipWrite(W,E->Path,strlen(E->Path))||(E->Directory&&!MdoZipWrite(W,"/",1)))return false;
    }
    uint8 End[22]={0};MdoZipSet32(End,0x06054b50u);MdoZipSet16(End+8,(uint16)W->Index.Count);MdoZipSet16(End+10,(uint16)W->Index.Count);MdoZipSet32(End+12,(uint32)(W->Position-Start));MdoZipSet32(End+16,(uint32)Start);
    return MdoZipWrite(W,End,sizeof(End))&&xrtFlush(W->Index.File);
}
static void MdoZipWriterUnit(MdoZipWriter* W){MdoZipClose(&W->Index);}
#endif
