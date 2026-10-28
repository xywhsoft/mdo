#ifndef MDO_EXTENSION_PATH_H
#define MDO_EXTENSION_PATH_H
#include <xsbase.h>
#include <string.h>
#include <stdio.h>
#include "extension_casefold.h"

#define MDO_EXTENSION_PATH_BYTES 4096u
#define MDO_EXTENSION_ARCHIVE_BYTES (512u*1024u*1024u)
#define MDO_EXTENSION_DIRECTORY_BYTES (1024u*1024u*1024u)
#define MDO_EXTENSION_FILE_BYTES (512u*1024u*1024u)
#define MDO_EXTENSION_DIRECTORY_ENTRIES 50000u
static const char* const MdoExtensionKinds[]={"agents","subagents","tools","mcp","skills","commands","external-tools","knowledge","plugins"};
static bool MdoExtensionDirectoryKind(cstr Kind)
{for(size_t I=0;I<9;I++)if(Kind&&!strcmp(Kind,MdoExtensionKinds[I]))return true;return false;}

/* Archive paths are relative UTF-8 names. Dotfiles are ordinary files; dot
 * components, Win32 aliases, streams and escape paths are not. */
static bool MdoExtensionRelativePath(cstr Path)
{
    size_t N=Path?strlen(Path):0;
    if(!N||N>MDO_EXTENSION_PATH_BYTES||Path[0]=='/'||!xrtUtf8Valid(xrtStrViewN(Path,N),NULL))return false;
    cstr P=Path;
    for(;;){cstr End=strchr(P,'/');size_t L=End?(size_t)(End-P):strlen(P);
        if(!L||L>255||(L==1&&P[0]=='.')||(L==2&&!memcmp(P,"..",2))||P[L-1]=='.'||P[L-1]==' ')return false;
        for(size_t I=0;I<L;I++){unsigned char C=P[I];if(C<32||C==127||strchr("\\:<>\"|?*",C))return false;}
        size_t B=0;while(B<L&&P[B]!='.')B++;while(B&&P[B-1]==' ')B--;char Name[9]={0};
        if(B<sizeof(Name)){for(size_t I=0;I<B;I++)Name[I]=P[I]>='A'&&P[I]<='Z'?P[I]+32:P[I];
            if(!strcmp(Name,"conin$")||!strcmp(Name,"conout$"))return false;}
        if(B==5&&(((P[0]=='c'||P[0]=='C')&&(P[1]=='o'||P[1]=='O')&&(P[2]=='m'||P[2]=='M'))||
            ((P[0]=='l'||P[0]=='L')&&(P[1]=='p'||P[1]=='P')&&(P[2]=='t'||P[2]=='T')))&&
            (unsigned char)P[3]==0xc2&&((unsigned char)P[4]==0xb9||(unsigned char)P[4]==0xb2||(unsigned char)P[4]==0xb3))return false;
        if(B<=4){for(size_t I=0;I<B;I++)Name[I]=P[I]>='A'&&P[I]<='Z'?P[I]+32:P[I];
            if(!strcmp(Name,"con")||!strcmp(Name,"prn")||!strcmp(Name,"aux")||!strcmp(Name,"nul")||
                (B==4&&(!memcmp(Name,"com",3)||!memcmp(Name,"lpt",3))&&Name[3]>='1'&&Name[3]<='9'))return false;}
        if(!End)return true;
        P=End+1;
    }
}
static bool MdoExtensionDirectoryId(cstr Id)
{return Id&&strlen(Id)<=96&&!strchr(Id,'/')&&MdoExtensionRelativePath(Id);}
static char* MdoExtensionDirectoryPath(cstr Kind,cstr Id)
{return MdoExtensionDirectoryKind(Kind)&&MdoExtensionDirectoryId(Id)?xrtFormat("%s/%s",Kind,Id):NULL;}

/* Conservative Unicode collision identity, shared by all platforms. Upper
 * then full fold also catches Win32 aliases such as I/dotless-i. */
static char* MdoExtensionPathKey(cstr Path)
{
    if(!MdoExtensionRelativePath(Path))return NULL;
    size_t N=strlen(Path),At=0;
    char* Key=xrtMalloc(N*3+1);
    if(!Key)return NULL;
    const unsigned char* P=(const unsigned char*)Path;
    while(*P){uint32 C=*P++;if(C>=0xc0){unsigned More=C<0xe0?1:C<0xf0?2:3;C&=(1u<<(6-More))-1u;while(More--)C=(C<<6)|(*P++&63);}
        size_t Low=0,High=sizeof(MdoExtensionFolds)/sizeof(*MdoExtensionFolds);
        while(Low<High){size_t Mid=Low+(High-Low)/2;if(MdoExtensionFolds[Mid].From<C)Low=Mid+1;else High=Mid;}
        uint32 One[3]={C,0,0};const uint32* To=Low<sizeof(MdoExtensionFolds)/sizeof(*MdoExtensionFolds)&&MdoExtensionFolds[Low].From==C?MdoExtensionFolds[Low].To:One;
        for(size_t I=0;I<3&&To[I];I++){uint32 V=To[I];unsigned char B[4];size_t L;
            if(V<0x80){B[0]=(uint8)V;L=1;}else if(V<0x800){B[0]=0xc0|(V>>6);B[1]=0x80|(V&63);L=2;}
            else if(V<0x10000){B[0]=0xe0|(V>>12);B[1]=0x80|((V>>6)&63);B[2]=0x80|(V&63);L=3;}
            else{B[0]=0xf0|(V>>18);B[1]=0x80|((V>>12)&63);B[2]=0x80|((V>>6)&63);B[3]=0x80|(V&63);L=4;}
            if(L>N*3-At){xrtFree(Key);return NULL;}memcpy(Key+At,B,L);At+=L;
        }
    }Key[At]=0;return Key;
}
#endif
