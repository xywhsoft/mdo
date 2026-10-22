#ifndef MDO_SCOPED_FILES_H
#define MDO_SCOPED_FILES_H
#include "extension_files.h"
/* Small rooted source-editor helpers. No link traversal or shell commands. */
static inline bool MdoScopedParents(xroot Root,cstr Path) {
    char Copy[512];if(strlen(Path)>=sizeof(Copy))return false;strcpy(Copy,Path);
    for(char* P=Copy;*P;P++)if(*P=='/'){
        *P=0;xfileinfo Info={0};
        if(!xrtRootStat(Root,Copy,false,&Info)){
            if(!xrtGetError()||xrtErrorKind(xrtGetError())!=XERR_NOT_FOUND)return false;
            xrtClearError();if(!xrtRootDirCreate(Root,Copy,0700))return false;
        }else if(Info.Type!=XFILE_TYPE_DIRECTORY)return false;
        *P='/';
    }
    return true;
}
static inline bool MdoScopedWrite(xroot Root,cstr Path,cstr Text) {
    unsigned char Bytes[16];char Nonce[33],Temp[560];
    if(!Text||!xrtSecureRandom(Bytes,sizeof(Bytes))||!MdoScopedParents(Root,Path))return false;
    for(size_t i=0;i<16;i++)snprintf(Nonce+2*i,3,"%02x",Bytes[i]);
    snprintf(Temp,sizeof(Temp),"%s.%s.tmp",Path,Nonce);
    xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE|XFILE_NOFOLLOW|XFILE_SYNC;O.Mode=0600;
    xfile F=xrtRootFileOpen(Root,Temp,&O);if(!F)return false;
    bool Ok=xrtWriteFull(F,Text,strlen(Text),NULL)&&xrtFlush(F);if(!xrtClose(F))Ok=false;
    char* From=xrtPathJoin(xrtRootPath(Root),Temp),*To=xrtPathJoin(xrtRootPath(Root),Path);
    if(Ok)Ok=From&&To&&xrtPathRename(From,To,true);
    if(!Ok)(void)xrtRootRemove(Root,Temp);
    xrtFree(From);xrtFree(To);return Ok;
}
static inline xfile MdoScopedLock(xroot Root,cstr Path) {
    if(!MdoScopedParents(Root,Path))return NULL;
    xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ|XFILE_WRITE|XFILE_CREATE|XFILE_NOFOLLOW;O.Mode=0600;
    xfile F=xrtRootFileOpen(Root,Path,&O);if(F&&!xrtFileLock(F,XFILE_LOCK_EXCLUSIVE,false)){xrtClose(F);return NULL;}return F;
}
#endif
