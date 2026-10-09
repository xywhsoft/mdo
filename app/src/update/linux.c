/* POSIX permits replacing the executable inode while its old image runs.
 * Prepare and sync a sibling first; rename is the only publication boundary.
 * The native host drains then execs itself, preserving systemd's MainPID. */
#include "../../include/mdo/update.h"
#include "../../include/mdo/home.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include <stdio.h>
#include <string.h>
/* Hosted POSIX ABI: filesystem structures/modes stay inside native xrt. */
extern int fsync(int);
static bool MdoUpdateLinuxCopy(cstr From,cstr To,uint32 Mode,bool Exclusive,bool* Created)
{
    xfile Input=xrtOpen(From,XFILE_READ|XFILE_NOFOLLOW);
    xfile Output=xrtOpen(To,XFILE_WRITE|XFILE_CREATE|XFILE_NOFOLLOW|(Exclusive?XFILE_EXCLUSIVE:XFILE_TRUNCATE));
    if(Created)*Created=Output!=NULL;
    bool Ok=Input&&Output;char Buffer[65536];size_t Count=0;
    while(Ok) {
        Ok=xrtRead(Input,Buffer,sizeof(Buffer),&Count);if(!Ok||!Count)break;
        size_t Written=0;Ok=xrtWriteFull(Output,Buffer,Count,&Written)&&Written==Count;
    }
    Ok=Ok&&xrtFlush(Output);xrtClose(Input);if(!xrtClose(Output))Ok=false;
    /* Old glibc fchmodat rejects AT_SYMLINK_NOFOLLOW even for a regular file.
     * The root API opens the leaf without following links and uses fchmod. */
    str Parent=xrtPathParent(To),Name=xrtPathName(To);
    xroot Directory=Ok&&Parent?xrtRootOpen(Parent):NULL;
    Ok=Directory&&Name&&xrtRootSetMode(Directory,Name,false,Mode);
    xrtRootClose(Directory);xrtFree(Parent);xrtFree(Name);return Ok;
}
static bool MdoUpdateLinuxSame(const xfileinfo* A,const xfileinfo* B)
{return (A->Available&B->Available&XFILE_INFO_IDENTITY)&&A->Device==B->Device&&A->Identity==B->Identity;}
bool MdoUpdateLinuxInstall(cstr Source,cstr Hash)
{
    xfileinfo Original,Running;char Swap[4096],Actual[65];bool Ok=false,Published=false,OwnSwap=false;
    str Download=MdoHomeExternalPath("data/update/new.bin"),Backup=MdoHomeExternalPath("data/update/previous.bin");
    str Parent=xrtPathParent(Source);xroot Directory=NULL;
    if(!Download||!Backup||!Parent||!xrtPathStat(Source,false,&Original)||Original.Type!=XFILE_TYPE_FILE||
       !xrtPathStat("/proc/self/exe",true,&Running)||!MdoUpdateLinuxSame(&Original,&Running))goto done;
    int n=snprintf(Swap,sizeof(Swap),"%s.mdo-update-%llu",Source,(unsigned long long)xrtClock());
    if(n<=0||(size_t)n>=sizeof(Swap))goto done;
    Directory=xrtRootOpen(Parent);if(!Directory)goto done;
    if(!MdoUpdateLinuxCopy(Download,Swap,Original.Mode&0777,true,&OwnSwap))goto cleanup;
    /* Verify the staged inode, then ensure its loader can run on this system. */
    if(!MdoUpdateFileHash(Swap,Actual,true)||strcmp(Actual,Hash))goto cleanup;
    const cstr Args[]={"--version"};xprocessconfig Config;xprocessrunoptions Options;xprocessresult Result={0};
    xrtProcessConfigInit(&Config);xrtProcessRunOptionsInit(&Options);Config.Program=Swap;Config.Args=Args;Config.ArgCount=1;
    Config.Stdin.Mode=Config.Stdout.Mode=Config.Stderr.Mode=XPROCESS_IO_NULL;Options.Deadline=xrtDeadlineAfter(UINT64_C(10000000));
    bool Runnable=xrtProcessRun(&Config,&Options,&Result)&&Result.Wait==XWAIT_OK&&Result.Status.Code==0;
    xrtProcessResultUnit(&Result);if(!Runnable)goto cleanup;
    /* Keep a durable previous version inside Home, including on another disk. */
    if(!MdoUpdateLinuxCopy(Source,Backup,Original.Mode&0777,false,NULL))goto cleanup;
    xfileinfo Current;
    if(!xrtPathStat(Source,false,&Current)||!MdoUpdateLinuxSame(&Current,&Original))goto cleanup;
    if(!xrtPathRename(Swap,Source,true))goto cleanup;Published=true;
    if(fsync((int)xrtRootNative(Directory)))goto cleanup;
    Ok=xsAppRequestRestart();
cleanup:
    if(!Ok&&Published) {
        /* The old process is still alive if restart was refused. */
        if(MdoUpdateLinuxCopy(Backup,Swap,Original.Mode&0777,true,&OwnSwap)) {
            if(xrtPathRename(Swap,Source,true))fsync((int)xrtRootNative(Directory));
        }
    }
    if(OwnSwap)xrtFileDelete(Swap);
done:
    xrtRootClose(Directory);xrtFree(Parent);xrtFree(Download);xrtFree(Backup);return Ok;
}
#else
bool MdoUpdateLinuxInstall(cstr Source,cstr Hash){(void)Source;(void)Hash;return false;}
#endif
