#include "markdown.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"
#include <string.h>
#include <stdio.h>

static bool MdoMarkdownId(xstrview Value,char* Out,size_t Capacity) {
    if(!Value.Size||Value.Size>=Capacity||Value.Data[0]=='.')return false;
    for(size_t i=0;i<Value.Size;i++){char C=Value.Data[i];if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_'||C=='-'||C=='.'))return false;}
    memcpy(Out,Value.Data,Value.Size);Out[Value.Size]=0;return true;
}
/* Open under an anchored root: both lexical traversal and intermediate links
 * remain constrained to the session workspace or its own artifact directory. */
static xfile MdoMarkdownOpen(cstr Base,cstr Requested,char** Native) {
    if(!Base||!Base[0])return NULL;
    char* Joined=xrtPathIsRooted(Requested)?xrtStrDup(Requested):xrtPathJoin(Base,Requested);
    char* Absolute=Joined?xrtPathAbs(Joined):NULL;xrtFree(Joined);
    char* Relative=Absolute?xrtPathRel(Base,Absolute):NULL;
    if(!Relative||xrtPathIsRooted(Relative)||!strcmp(Relative,"..")||!strncmp(Relative,"../",3)||!strncmp(Relative,"..\\",3)){
        xrtFree(Absolute);xrtFree(Relative);return NULL;
    }
    xroot Root=xrtRootOpen(Base);xfile File=NULL;
    if(Root){xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
        File=xrtRootFileOpen(Root,Relative,&Options);xrtRootClose(Root);}
    xrtFree(Relative);
    if(File)*Native=Absolute;else xrtFree(Absolute);
    return File;
}
static cstr MdoMarkdownImageType(const uint8* Bytes,size_t Size) {
    if(Size>=8&&!memcmp(Bytes,"\x89PNG\r\n\x1a\n",8))return "image/png";
    if(Size>=3&&Bytes[0]==0xff&&Bytes[1]==0xd8&&Bytes[2]==0xff)return "image/jpeg";
    if(Size>=6&&(!memcmp(Bytes,"GIF87a",6)||!memcmp(Bytes,"GIF89a",6)))return "image/gif";
    if(Size>=12&&!memcmp(Bytes,"RIFF",4)&&!memcmp(Bytes+8,"WEBP",4))return "image/webp";
    return NULL;
}
bool MdoApiMarkdownResourceRoute(MdoApiContext* C) {
    char Project[MDO_PROJECT_ID_CAPACITY],Session[MDO_SESSION_ID_CAPACITY],Path[1025];
    bool Image=false;size_t Encoded=0,Decoded=0;const xstrview Query=C->Target.Query;
    if(C->ParamCount!=2||!MdoMarkdownId(C->Params[0],Project,sizeof(Project))||!MdoMarkdownId(C->Params[1],Session,sizeof(Session)))
        return MdoApiReplyError(C,400,"markdown_resource_invalid","Invalid conversation resource identity",NULL);
    const char* Suffix=NULL;
    if(Query.Size>11&&!memcmp(Query.Data+Query.Size-11,"&mode=image",11)){Image=true;Suffix="&mode=image";}
    else if(Query.Size>10&&!memcmp(Query.Data+Query.Size-10,"&mode=file",10))Suffix="&mode=file";
    if(!Suffix||Query.Size>3200||memcmp(Query.Data,"path=",5))goto invalid;
    Encoded=Query.Size-5-strlen(Suffix);
    if(!Encoded||memchr(Query.Data+5,'&',Encoded)||!xrtPercentDecode(xrtStrViewN(Query.Data+5,Encoded),Path,sizeof(Path)-1,&Decoded)||
        !Decoded||memchr(Path,0,Decoded)||!xrtUtf8Valid(xrtStrViewN(Path,Decoded),NULL))goto invalid;
    Path[Decoded]=0;
    for(size_t i=0;i<Decoded;i++){if((unsigned char)Path[i]<32u||(unsigned char)Path[i]==127u)goto invalid;if(Path[i]=='\\')Path[i]='/';}
    MdoSession* Handle;MdoSessionInfo Info={0};xwork_error Error={0};Info.Size=sizeof(Info);
    Handle=MdoSessionLoad(Project,Session,&Error);
    if(!Handle)return MdoApiReplyError(C,404,"markdown_session_missing","Conversation is unavailable",NULL);
    bool Ok=MdoSessionGetInfo(Handle,&Info);MdoSessionRelease(Handle);
    if(!Ok)return MdoApiReplyError(C,404,"markdown_session_missing","Conversation is unavailable",NULL);
    char* Workspace=MdoPathResolve(Info.WorkspaceRoot),*Native=NULL;
    char Relative[320];snprintf(Relative,sizeof(Relative),"sessions/%s/%s",Project,Session);
    char* SessionRoot=MdoHomeExternalPath(Relative);
    bool UploadAlias=!strncmp(Path,"@uploads/",9);
    xfile File=UploadAlias?MdoMarkdownOpen(SessionRoot,Path+1,&Native):MdoMarkdownOpen(Workspace,Path,&Native);
    if(!File&&!UploadAlias){xrtClearError();File=MdoMarkdownOpen(SessionRoot,Path,&Native);}
    xrtFree(Workspace);xrtFree(SessionRoot);
    if(!File){xrtClearError();return MdoApiReplyError(C,404,"markdown_resource_missing","File is missing or outside the conversation workspace and artifacts",NULL);}
    xfileinfo Stat={0};Ok=xrtFileStat(File,&Stat)&&Stat.Type==XFILE_TYPE_FILE;
    if(!Ok){xrtClose(File);xrtFree(Native);return MdoApiReplyError(C,404,"markdown_resource_missing","Resource is not a regular file",NULL);}
    if(!Image){xvalue* Data=xrtValueObject();MdoApiValueSetString(Data,"path",Native);MdoApiValueSetUInt(Data,"bytes",Stat.Size);
        xrtClose(File);xrtFree(Native);return MdoApiReplySuccessTake(C,200,Data,NULL);}
    xrtFree(Native);
    if(!Stat.Size||Stat.Size>MDO_API_IMAGE_MAX_BYTES){xrtClose(File);return MdoApiReplyError(C,413,"markdown_image_limit","Image must contain 1 byte to 8 MiB",NULL);}
    uint8* Bytes=xrtMalloc((size_t)Stat.Size);size_t At=0;
    while(Bytes&&At<(size_t)Stat.Size){size_t Read=0;if(!xrtRead(File,Bytes+At,(size_t)Stat.Size-At,&Read)||!Read)break;At+=Read;}
    xfileinfo After={0};Ok=Bytes&&At==Stat.Size&&xrtFileStat(File,&After)&&After.Size==Stat.Size&&After.Modified==Stat.Modified;
    xrtClose(File);cstr Type=Ok?MdoMarkdownImageType(Bytes,At):NULL;
    bool Result=Type?MdoApiReplyImage(C,Bytes,At,Type):MdoApiReplyError(C,415,"markdown_image_invalid","Only PNG, JPEG, GIF and WebP images can be previewed",NULL);
    xrtFree(Bytes);return Result;
invalid:
    return MdoApiReplyError(C,400,"markdown_resource_invalid","A bounded UTF-8 resource path and mode are required",NULL);
}
