#include "internal.h"
#include "../../include/mdo/sessions.h"
#include <string.h>
#include <stdio.h>

#define MDO_SHARE_FILE_LIMIT (64u * 1024u * 1024u)

static bool MdoShareText(const xvalue* Object, const char* Key, char* Out, size_t Capacity)
{
    xstrview Text;
    if (!xrtValueGetString(xrtValueObjectGet(Object,xrtStrView(Key)),&Text) ||
        !Text.Size || Text.Size >= Capacity || memchr(Text.Data,0,Text.Size) || !xrtUtf8Valid(Text,NULL)) return false;
    memcpy(Out,Text.Data,Text.Size); Out[Text.Size]=0; return true;
}
static bool MdoShareHex(const char* Value)
{
    if (strlen(Value)!=32u) return false;
    for (size_t i=0;i<32u;i++) if (!((Value[i]>='0'&&Value[i]<='9')||(Value[i]>='a'&&Value[i]<='f'))) return false;
    return true;
}
static bool MdoShareDirectory(xroot Root, const char* Path)
{
    xfileinfo Info;
    if (!xrtRootStat(Root,Path,false,&Info)) {
        xrtClearError();
        if (!xrtRootDirCreate(Root,Path,0700u) && !xrtRootStat(Root,Path,false,&Info)) return false;
    }
    return xrtRootStat(Root,Path,false,&Info) && Info.Type==XFILE_TYPE_DIRECTORY;
}
static bool MdoShareDigest(xfile Input, xfile Output, uint64 Expected, const char* Hash)
{
    uint8 Buffer[65536], Digest[32]; char Hex[65]; uint64 Total=0;
    xsha256 Sha; xrtSha256Init(&Sha); xfileinfo Before,After;
    if (!Input || !xrtFileStat(Input,&Before) || Before.Type!=XFILE_TYPE_FILE || Before.Size!=Expected) return false;
    for (;;) {
        size_t Read=0, Written=0;
        if (!xrtRead(Input,Buffer,sizeof(Buffer),&Read)) return false;
        if (!Read) break;
        if (Read>Expected-Total || !xrtSha256Update(&Sha,Buffer,Read) ||
            (Output && (!xrtWrite(Output,Buffer,Read,&Written)||Written!=Read))) return false;
        Total+=Read;
    }
    if (Total!=Expected || !xrtFileStat(Input,&After) || Before.Identity!=After.Identity ||
        Before.Device!=After.Device || Before.Size!=After.Size || Before.Modified!=After.Modified ||
        Before.Changed!=After.Changed || !xrtSha256Final(&Sha,Digest) || (Output&&!xrtFlush(Output))) return false;
    for (size_t i=0;i<32u;i++) { Hex[i*2u]="0123456789abcdef"[Digest[i]>>4u]; Hex[i*2u+1u]="0123456789abcdef"[Digest[i]&15u]; }
    Hex[64]=0; return !strcmp(Hex,Hash);
}
/* The Java shell supplies only private, fixed inbox identities. No caller-
 * supplied source path or destination path reaches a native file operation. */
bool MdoApiSharedFilesRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body={0}; MdoApiBodyStatus Status;
    char Project[MDO_PROJECT_ID_CAPACITY],SessionId[MDO_SESSION_ID_CAPACITY];
    char Share[33],FileId[33],OwnerProject[MDO_PROJECT_ID_CAPACITY],OwnerSession[MDO_SESSION_ID_CAPACITY];
    char Name[181],Hash[65],Source[80],Directory[64],Destination[320],Temporary[128];
    str Inbox=xrtEnvGet("MDO_ANDROID_SHARE_ROOT"),Random=NULL;
    xroot Root=NULL,Workspace=NULL; xfile Input=NULL,Output=NULL;
    xvalue* Manifest=NULL; MdoSession* Session=NULL; MdoSessionInfo Info={0}; xwork_error Error;
    uint64 Bytes=0; bool Ok=false; unsigned Code=409u;
    const char* Message="The shared file could not be imported; check its source and destination";
    Temporary[0]=0;
    if (!Inbox || !Inbox[0]) { xrtFree(Inbox); return MdoApiReplyError(Context,404u,"share_unavailable","Android sharing is unavailable on this host",NULL); }
    if (Context->ParamCount!=2u || Context->Params[0].Size>=sizeof(Project) || Context->Params[1].Size>=sizeof(SessionId)) goto done;
    memcpy(Project,Context->Params[0].Data,Context->Params[0].Size); Project[Context->Params[0].Size]=0;
    memcpy(SessionId,Context->Params[1].Data,Context->Params[1].Size); SessionId[Context->Params[1].Size]=0;
    Status=MdoApiJsonBodyRead(Context,&Body);
    if (Status!=MDO_API_BODY_OK) { xrtFree(Inbox); return MdoApiReplyBodyError(Context,Status); }
    Ok=xrtValueCount(Body.Value)==2u && MdoShareText(Body.Value,"share_id",Share,sizeof(Share)) &&
        MdoShareText(Body.Value,"file_id",FileId,sizeof(FileId)) && MdoShareHex(Share) && MdoShareHex(FileId);
    MdoApiJsonBodyUnit(&Body);
    if (!Ok) { Ok=false; Code=422u; Message="Invalid shared file identity"; goto done; }
    Ok=false; Message="The private share inbox is unavailable";
    Root=xrtRootOpen(Inbox); if (!Root) goto done;
    Message="The share manifest is missing, changed or invalid";
    snprintf(Source,sizeof(Source),"%s/ready.json",Share);
    {
        xfileoptions Options; xrtFileOptionsInit(&Options); Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
        Input=xrtRootFileOpen(Root,Source,&Options); xfileinfo Stat; size_t Read=0;
        if (!Input || !xrtFileStat(Input,&Stat) || Stat.Type!=XFILE_TYPE_FILE || !Stat.Size || Stat.Size>1200000u) goto done;
        char* Json=(char*)xrtMalloc((size_t)Stat.Size);
        if (Json && xrtRead(Input,Json,(size_t)Stat.Size,&Read) && Read==Stat.Size) Manifest=xrtJsonParse(xrtStrViewN(Json,Read));
        xrtFree(Json); xrtClose(Input); Input=NULL;
    }
    char ManifestId[33];
    if (!Manifest || !MdoShareText(Manifest,"id",ManifestId,sizeof(ManifestId)) || strcmp(ManifestId,Share)) goto done;
    const xvalue* Owner=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("destination"));
    if (!MdoShareText(Owner,"project_id",OwnerProject,sizeof(OwnerProject)) ||
        !MdoShareText(Owner,"session_id",OwnerSession,sizeof(OwnerSession)) || strcmp(Project,OwnerProject) || strcmp(SessionId,OwnerSession)) {
        Message="This share belongs to another conversation"; goto done;
    }
    const xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files")); const xvalue* Row=NULL;
    if (xrtValueCount(Files)>32u) goto done;
    for (size_t i=0;i<xrtValueCount(Files);i++) {
        char Id[33]; const xvalue* Candidate=xrtValueArrayGet(Files,i);
        if (MdoShareText(Candidate,"id",Id,sizeof(Id)) && !strcmp(Id,FileId)) { Row=Candidate; break; }
    }
    Message="The shared file metadata or filename is invalid";
    if (!Row || !MdoShareText(Row,"name",Name,sizeof(Name)) || !MdoShareText(Row,"sha256",Hash,sizeof(Hash)) ||
        strlen(Hash)!=64u || !strcmp(Name,".") || !strcmp(Name,"..")) goto done;
    for (size_t i=0;i<64u;i++) if (!((Hash[i]>='0'&&Hash[i]<='9')||(Hash[i]>='a'&&Hash[i]<='f'))) goto done;
    Message="The shared file byte size is invalid or exceeds 64 MiB";
    const xvalue* SizeValue=xrtValueObjectGet(Row,XRT_STR_LITERAL("bytes")); int64 SignedSize=0;
    if (!xrtValueGetUInt(SizeValue,&Bytes)) {
        if (!xrtValueGetInt(SizeValue,&SignedSize) || SignedSize<0) goto done;
        Bytes=(uint64)SignedSize;
    }
    if (Bytes>MDO_SHARE_FILE_LIMIT) goto done;
    for (size_t i=0;Name[i];i++) if ((uint8)Name[i]<32u || (uint8)Name[i]==127u || strchr("/\\:",Name[i])) goto done;
    xworkErrorInit(&Error); Session=MdoSessionLoad(Project,SessionId,&Error); Info.Size=sizeof(Info);
    if (!Session || !MdoSessionGetInfo(Session,&Info) || Info.Status!=MDO_SESSION_ACTIVE) { Message="Select an active conversation to receive this share"; goto done; }
    Message="The receiving workspace is unavailable";
    Workspace=xrtRootOpen(Info.WorkspaceRoot); if (!Workspace) goto done;
    snprintf(Directory,sizeof(Directory),"shared-files/%s",Share);
    snprintf(Destination,sizeof(Destination),"%s/%s-%s",Directory,FileId,Name);
    Message="Cannot create the shared-files directory; check its type and permissions";
    if (!MdoShareDirectory(Workspace,"shared-files") || !MdoShareDirectory(Workspace,Directory)) goto done;
    xfileoptions Options; xrtFileOptionsInit(&Options); Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfileinfo Existing;
    if (xrtRootStat(Workspace,Destination,false,&Existing)) {
        Message="The receiving file already exists with different content; it was not overwritten";
        Input=xrtRootFileOpen(Workspace,Destination,&Options); Ok=MdoShareDigest(Input,NULL,Bytes,Hash); goto done;
    }
    xrtClearError(); Random=xrtXidMakeString(); if (!Random) goto done;
    snprintf(Temporary,sizeof(Temporary),"%s/.%s.part",Directory,Random);
    snprintf(Source,sizeof(Source),"%s/%s.bin",Share,FileId);
    Message="The shared source file is unavailable or is a symbolic link";
    Input=xrtRootFileOpen(Root,Source,&Options); if (!Input) goto done;
    Options.Flags=XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE|XFILE_NOFOLLOW; Options.Mode=0600u;
    Message="Cannot write the receiving file; check free space and permissions";
    Output=xrtRootFileOpen(Workspace,Temporary,&Options); if (!Output) goto done;
    Message="The shared file changed, failed its integrity check or could not be fully written";
    if (!Output || !MdoShareDigest(Input,Output,Bytes,Hash)) goto done;
    xrtClose(Input); Input=NULL; xrtClose(Output); Output=NULL;
    Message="Cannot publish the shared file without overwriting an existing file";
    Ok=xsRootRenameNoReplace(Workspace,Temporary,Destination);
    if (!Ok) { Options.Flags=XFILE_READ|XFILE_NOFOLLOW; Input=xrtRootFileOpen(Workspace,Destination,&Options); Ok=MdoShareDigest(Input,NULL,Bytes,Hash); }
done:
    xrtClose(Input); xrtClose(Output);
    if (Workspace && Temporary[0]) (void)xrtRootRemove(Workspace,Temporary);
    xrtRootClose(Workspace); xrtRootClose(Root); MdoSessionRelease(Session); xrtValueRelease(Manifest); xrtFree(Inbox); xrtFree(Random);
    if (Ok) {
        xvalue* Data=xrtValueObject();
        if (Data && MdoApiValueSetString(Data,"path",Destination) && MdoApiValueSetUInt(Data,"bytes",Bytes))
            return MdoApiReplySuccessTake(Context,200u,Data,NULL);
        xrtValueRelease(Data);
    }
    return MdoApiReplyError(Context,Code,"share_import_failed",Message,NULL);
}
