#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../generated/builtin-extensions/index.h"

/* A separate receipt remembers first installation even after uninstall. It
 * prevents a packaged fallback from reviving removed/disabled extensions. */
bool MdoExtensionsSeedBuiltins(char Error[512])
{
    bool Missing=false;char* Text=MdoExtensionRead("data/extensions/builtins.json",true,128u*1024u,&Missing);
    xvalue* Receipts=Text?xrtJsonParse(xrtStrView(Text)):Missing?xrtValueObject():NULL;xrtFree(Text);
    if(!Receipts||xrtValueType(Receipts)!=XVALUE_OBJECT){xrtValueRelease(Receipts);snprintf(Error,512,"Cannot read builtin extension installation receipts");return false;}
    bool Ok=true;
    for(size_t I=0;Ok&&I<sizeof(MdoBuiltinExtensions)/sizeof(*MdoBuiltinExtensions);I++){
        cstr Kind=MdoBuiltinExtensions[I].Kind,Id=MdoBuiltinExtensions[I].Id,Sha=MdoBuiltinExtensions[I].Sha256;
        char* Key=xrtFormat("%s:%s",Kind,Id),*Root=MdoExtensionsRoot(Kind,Id);
        bool Exists=false;xfileinfo Info={0};Ok=Key&&Root&&MdoHomeExternalStat(Root,&Exists,&Info);
        const xvalue* Before=Key?xrtValueObjectGet(Receipts,xrtStrView(Key)):NULL;
        xvalue* Installed=Ok&&Exists?MdoExtensionsRead(Kind,Id,Error):NULL;
        cstr Source=Installed?MdoExtensionText(Installed,"source",16):NULL;
        const xvalue* Record=xrtValueObjectGet(Installed,XRT_STR_LITERAL("installation"));
        cstr InstalledSha=MdoExtensionText(Record,"sha256",64);
        bool First=!Before&&!Exists,Update=Exists&&Source&&!strcmp(Source,"builtin")&&(!InstalledSha||strcmp(InstalledSha,Sha));
        if(Ok&&(First||Update)){
            /* Updating a builtin must never overwrite an edited directory. */
            Ok=!Update||MdoExtensionsVerify(Kind,Id,NULL,Error);
            char* Packaged=xrtFormat("/app/generated/builtin-extensions/%s",MdoBuiltinExtensions[I].File);
            char* Task=xrtXidMakeString(),*Relative=Task?xrtFormat("data/extension-tasks/%s/builtin.zip",Task):NULL;
            xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ;
            xfile Input=Ok&&Packaged?xrtVfsOpen(MdoHomeApplicationVfs(),Packaged,&Options):NULL;
            xfile Output=Input&&Relative?MdoHomeOpenWrite(Relative,XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE):NULL;
            bool Created=Output!=NULL;unsigned char Buffer[65536];size_t N=0;Ok=Ok&&Input&&Output;
            while(Ok){Ok=xrtRead(Input,Buffer,sizeof(Buffer),&N);if(!Ok||!N)break;Ok=xrtWriteFull(Output,Buffer,N,NULL);}
            if(Output&&!xrtClose(Output))Ok=false;
            if(Input&&!xrtClose(Input))Ok=false;
            xvalue* Credits=xrtValueObject(),*Result=NULL;
            Ok=Ok&&Credits&&xrtValueObjectSetNew(Credits,XRT_STR_LITERAL("source"),xrtValueString(XRT_STR_LITERAL("builtin")))&&
                xrtValueObjectSetNew(Credits,XRT_STR_LITERAL("sha256"),xrtValueString(xrtStrView(Sha)))&&
                MdoExtensionsInstall(Kind,Id,Relative,Credits,true,NULL,&Result,Error);
            xrtValueRelease(Result);xrtValueRelease(Credits);
            if(Created)(void)MdoHomeRemove(Relative,false);
            char* Parent=Relative?xrtPathParent(Relative):NULL;if(Parent)(void)MdoHomeRemoveEmptyDirectory(Parent);
            xrtFree(Parent);xrtFree(Packaged);xrtFree(Task);xrtFree(Relative);
        }
        if(Ok)Ok=xrtValueObjectSetNew(Receipts,xrtStrView(Key),xrtValueString(xrtStrView(Sha)));
        xrtValueRelease(Installed);xrtFree(Key);xrtFree(Root);
    }
    Text=Ok?xrtJsonStringify(Receipts,false,NULL):NULL;
    Ok=Text&&MdoHomeAtomicWrite("data/extensions/builtins.json",Text,strlen(Text),false);
    xrtFree(Text);xrtValueRelease(Receipts);if(!Ok&&!Error[0])snprintf(Error,512,"Cannot initialize builtin extension directories");return Ok;
}
