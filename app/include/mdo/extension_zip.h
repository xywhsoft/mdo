#ifndef MDO_EXTENSION_ZIP_H
#define MDO_EXTENSION_ZIP_H
/* Standard ZIP transport. Entries are streams, never JSON/Base64 values.
 * The caller owns file handles and supplies rooted extraction destinations. */
#include "extension_path.h"
#include <stdlib.h>

/* ZIP CRC is local to the transport so an installed website host need not
 * export optional XRT checksum functions. Immutable table; concurrent safe. */
static uint32 MdoZipCrcUpdate(uint32 C,const void* Data,size_t N){
 static const uint32 Table[256]={
  0x00000000u,0x77073096u,0xee0e612cu,0x990951bau,0x076dc419u,0x706af48fu,0xe963a535u,0x9e6495a3u,
  0x0edb8832u,0x79dcb8a4u,0xe0d5e91eu,0x97d2d988u,0x09b64c2bu,0x7eb17cbdu,0xe7b82d07u,0x90bf1d91u,
  0x1db71064u,0x6ab020f2u,0xf3b97148u,0x84be41deu,0x1adad47du,0x6ddde4ebu,0xf4d4b551u,0x83d385c7u,
  0x136c9856u,0x646ba8c0u,0xfd62f97au,0x8a65c9ecu,0x14015c4fu,0x63066cd9u,0xfa0f3d63u,0x8d080df5u,
  0x3b6e20c8u,0x4c69105eu,0xd56041e4u,0xa2677172u,0x3c03e4d1u,0x4b04d447u,0xd20d85fdu,0xa50ab56bu,
  0x35b5a8fau,0x42b2986cu,0xdbbbc9d6u,0xacbcf940u,0x32d86ce3u,0x45df5c75u,0xdcd60dcfu,0xabd13d59u,
  0x26d930acu,0x51de003au,0xc8d75180u,0xbfd06116u,0x21b4f4b5u,0x56b3c423u,0xcfba9599u,0xb8bda50fu,
  0x2802b89eu,0x5f058808u,0xc60cd9b2u,0xb10be924u,0x2f6f7c87u,0x58684c11u,0xc1611dabu,0xb6662d3du,
  0x76dc4190u,0x01db7106u,0x98d220bcu,0xefd5102au,0x71b18589u,0x06b6b51fu,0x9fbfe4a5u,0xe8b8d433u,
  0x7807c9a2u,0x0f00f934u,0x9609a88eu,0xe10e9818u,0x7f6a0dbbu,0x086d3d2du,0x91646c97u,0xe6635c01u,
  0x6b6b51f4u,0x1c6c6162u,0x856530d8u,0xf262004eu,0x6c0695edu,0x1b01a57bu,0x8208f4c1u,0xf50fc457u,
  0x65b0d9c6u,0x12b7e950u,0x8bbeb8eau,0xfcb9887cu,0x62dd1ddfu,0x15da2d49u,0x8cd37cf3u,0xfbd44c65u,
  0x4db26158u,0x3ab551ceu,0xa3bc0074u,0xd4bb30e2u,0x4adfa541u,0x3dd895d7u,0xa4d1c46du,0xd3d6f4fbu,
  0x4369e96au,0x346ed9fcu,0xad678846u,0xda60b8d0u,0x44042d73u,0x33031de5u,0xaa0a4c5fu,0xdd0d7cc9u,
  0x5005713cu,0x270241aau,0xbe0b1010u,0xc90c2086u,0x5768b525u,0x206f85b3u,0xb966d409u,0xce61e49fu,
  0x5edef90eu,0x29d9c998u,0xb0d09822u,0xc7d7a8b4u,0x59b33d17u,0x2eb40d81u,0xb7bd5c3bu,0xc0ba6cadu,
  0xedb88320u,0x9abfb3b6u,0x03b6e20cu,0x74b1d29au,0xead54739u,0x9dd277afu,0x04db2615u,0x73dc1683u,
  0xe3630b12u,0x94643b84u,0x0d6d6a3eu,0x7a6a5aa8u,0xe40ecf0bu,0x9309ff9du,0x0a00ae27u,0x7d079eb1u,
  0xf00f9344u,0x8708a3d2u,0x1e01f268u,0x6906c2feu,0xf762575du,0x806567cbu,0x196c3671u,0x6e6b06e7u,
  0xfed41b76u,0x89d32be0u,0x10da7a5au,0x67dd4accu,0xf9b9df6fu,0x8ebeeff9u,0x17b7be43u,0x60b08ed5u,
  0xd6d6a3e8u,0xa1d1937eu,0x38d8c2c4u,0x4fdff252u,0xd1bb67f1u,0xa6bc5767u,0x3fb506ddu,0x48b2364bu,
  0xd80d2bdau,0xaf0a1b4cu,0x36034af6u,0x41047a60u,0xdf60efc3u,0xa867df55u,0x316e8eefu,0x4669be79u,
  0xcb61b38cu,0xbc66831au,0x256fd2a0u,0x5268e236u,0xcc0c7795u,0xbb0b4703u,0x220216b9u,0x5505262fu,
  0xc5ba3bbeu,0xb2bd0b28u,0x2bb45a92u,0x5cb36a04u,0xc2d7ffa7u,0xb5d0cf31u,0x2cd99e8bu,0x5bdeae1du,
  0x9b64c2b0u,0xec63f226u,0x756aa39cu,0x026d930au,0x9c0906a9u,0xeb0e363fu,0x72076785u,0x05005713u,
  0x95bf4a82u,0xe2b87a14u,0x7bb12baeu,0x0cb61b38u,0x92d28e9bu,0xe5d5be0du,0x7cdcefb7u,0x0bdbdf21u,
  0x86d3d2d4u,0xf1d4e242u,0x68ddb3f8u,0x1fda836eu,0x81be16cdu,0xf6b9265bu,0x6fb077e1u,0x18b74777u,
  0x88085ae6u,0xff0f6a70u,0x66063bcau,0x11010b5cu,0x8f659effu,0xf862ae69u,0x616bffd3u,0x166ccf45u,
  0xa00ae278u,0xd70dd2eeu,0x4e048354u,0x3903b3c2u,0xa7672661u,0xd06016f7u,0x4969474du,0x3e6e77dbu,
  0xaed16a4au,0xd9d65adcu,0x40df0b66u,0x37d83bf0u,0xa9bcae53u,0xdebb9ec5u,0x47b2cf7fu,0x30b5ffe9u,
  0xbdbdf21cu,0xcabac28au,0x53b39330u,0x24b4a3a6u,0xbad03605u,0xcdd70693u,0x54de5729u,0x23d967bfu,
  0xb3667a2eu,0xc4614ab8u,0x5d681b02u,0x2a6f2b94u,0xb40bbe37u,0xc30c8ea1u,0x5a05df1bu,0x2d02ef8du,
 };
 const uint8* P=Data;C^=UINT32_MAX;while(N--)C=Table[(C^*P++)&255u]^(C>>8);return C^UINT32_MAX;
}
static uint32 MdoZipCrc(const void* Data,size_t N){return MdoZipCrcUpdate(0,Data,N);}

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
        if(Id==0x7075&&(L<5||P[0]!=1||MdoZip32(P+1)!=MdoZipCrc(Name,strlen(Name))||strlen(Name)!=L-5u||memcmp(P+5,Name,L-5u)))return false;
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
    D->Crc=MdoZipCrcUpdate(D->Crc,B.Data,B.Size);D->Bytes+=B.Size;
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
            E->Crc=MdoZipCrcUpdate(E->Crc,Buffer,N);At+=N;Ok=xrtDeflateWrite(Deflate,(xbytesview){Buffer,N},At==Size?XDEFLATE_FLUSH_FINISH:XDEFLATE_FLUSH_NONE,MdoZipCompressed,W);}
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
