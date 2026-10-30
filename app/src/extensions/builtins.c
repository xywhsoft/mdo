#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../generated/builtin-extensions/index.h"

bool MdoExtensionsEditableSource(cstr Kind,cstr Source)
{
    return Source&&(!strcmp(Source,"manual")||(!strcmp(Source,"builtin")&&Kind&&(!strcmp(Kind,"agents")||!strcmp(Kind,"subagents"))));
}
bool MdoExtensionsBuiltinRole(cstr Kind,cstr Directory,cstr Ref)
{
    if(!Kind||!Directory||!Ref||(strcmp(Kind,"agents")&&strcmp(Kind,"subagents")))return false;
    char Identity[170];snprintf(Identity,sizeof(Identity),"%s:%s",!strcmp(Kind,"agents")?"agent":"subagent",Directory);
    if(strcmp(Identity,Ref))return false;
    for(size_t I=0;I<sizeof(MdoBuiltinExtensions)/sizeof(*MdoBuiltinExtensions);I++)
        if(!strcmp(Kind,MdoBuiltinExtensions[I].Kind)&&!strcmp(Directory,MdoBuiltinExtensions[I].Id))return true;
    return false;
}
bool MdoExtensionsRestoreBuiltin(cstr Kind,cstr Directory,cstr Expected,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512])
{
    *Result=NULL;
    xvalue* Before=MdoExtensionsRead(Kind,Directory,Error);
    bool Allowed=Before&&MdoExtensionsEditableSource(Kind,MdoExtensionText(Before,"source",16))&&
        MdoExtensionsBuiltinRole(Kind,Directory,MdoExtensionText(Before,"id",MDO_EXTENSION_REF_BYTES));
    xrtValueRelease(Before);
    if(!Allowed){snprintf(Error,512,"Only the matching local builtin role can be restored");return false;}
    /* Called by the normal manager after revision, retirement and source checks.
     * The archive and digest come only from this executable's VFS. */
    for(size_t I=0;I<sizeof(MdoBuiltinExtensions)/sizeof(*MdoBuiltinExtensions);I++){
        if(strcmp(Kind,MdoBuiltinExtensions[I].Kind)||strcmp(Directory,MdoBuiltinExtensions[I].Id))continue;
        char* Packaged=xrtFormat("/app/generated/builtin-extensions/%s",MdoBuiltinExtensions[I].File);
        char* Task=xrtXidMakeString(),*Relative=Task?xrtFormat("data/extension-tasks/%s/builtin.zip",Task):NULL;
        xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ;
        xfile Input=Packaged?xrtVfsOpen(MdoHomeApplicationVfs(),Packaged,&Options):NULL;
        xfile Output=Input&&Relative?MdoHomeOpenWrite(Relative,XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE):NULL;
        bool Created=Output!=NULL,Ok=Input&&Output;unsigned char Buffer[65536];size_t N=0;
        while(Ok){Ok=(!Cancel||!xrtCancelRequested(Cancel))&&xrtRead(Input,Buffer,sizeof(Buffer),&N);if(!Ok||!N)break;Ok=xrtWriteFull(Output,Buffer,N,NULL);}
        if(Output&&!xrtClose(Output))Ok=false;
        if(Input&&!xrtClose(Input))Ok=false;
        xvalue* Credits=xrtValueObject();
        Ok=Ok&&Credits&&xrtValueObjectSetNew(Credits,XRT_STR_LITERAL("source"),xrtValueString(XRT_STR_LITERAL("builtin")))&&
            xrtValueObjectSetNew(Credits,XRT_STR_LITERAL("expected_revision"),xrtValueString(xrtStrView(Expected)))&&
            xrtValueObjectSetNew(Credits,XRT_STR_LITERAL("sha256"),xrtValueString(xrtStrView(MdoBuiltinExtensions[I].Sha256)))&&
            MdoExtensionsInstall(Kind,Directory,Relative,Credits,Trust,Cancel,Result,Error);
        xrtValueRelease(Credits);
        if(Created)(void)MdoHomeRemove(Relative,false);
        char* Parent=Relative?xrtPathParent(Relative):NULL;if(Parent)(void)MdoHomeRemoveEmptyDirectory(Parent);
        xrtFree(Parent);xrtFree(Packaged);xrtFree(Task);xrtFree(Relative);
        if(!Ok&&!Error[0])snprintf(Error,512,"Cannot restore the packaged builtin role");
        return Ok;
    }
    snprintf(Error,512,"No packaged builtin role matches this directory");return false;
}

/* A separate receipt remembers first installation even after uninstall. It
 * prevents a packaged fallback from reviving removed/disabled extensions. */
static bool MdoExtensionsSeedBuiltinsMode(bool Migrate,char Error[512])
{
    bool Missing=false;char* Text=MdoExtensionRead("data/extensions/builtins.json",true,128u*1024u,&Missing);
    xvalue* Receipts=Text?xrtJsonParse(xrtStrView(Text)):Missing?xrtValueObject():NULL;xrtFree(Text);
    if(!Receipts||xrtValueType(Receipts)!=XVALUE_OBJECT){xrtValueRelease(Receipts);snprintf(Error,512,"Cannot read builtin extension installation receipts");return false;}
    bool Ok=true;
    for(size_t I=0;Ok&&I<sizeof(MdoBuiltinExtensions)/sizeof(*MdoBuiltinExtensions);I++){
        cstr Kind=MdoBuiltinExtensions[I].Kind,Id=MdoBuiltinExtensions[I].Id,Sha=MdoBuiltinExtensions[I].Sha256;
        if(MdoExtensionsRecoveryBlocked(Kind,Id,NULL))continue;
        char* Key=xrtFormat("%s:%s",Kind,Id),*Root=MdoExtensionsRoot(Kind,Id);
        bool Exists=false;xfileinfo Info={0};Ok=Key&&Root&&MdoHomeExternalStat(Root,&Exists,&Info);
        const xvalue* Before=Key?xrtValueObjectGet(Receipts,xrtStrView(Key)):NULL;
        xvalue* Installed=Ok&&Exists?MdoExtensionsRead(Kind,Id,Error):NULL;
        if(Ok&&Exists&&!Installed){xvalue* Rows=MdoExtensionsList(Kind,Error);for(size_t J=0;J<xrtValueCount(Rows);J++){const xvalue* Row=xrtValueArrayGet(Rows,J);cstr Directory=MdoExtensionText(Row,"directory_id",96);if(Directory&&!strcmp(Directory,Id)){Installed=xrtValueClone(Row);break;}}xrtValueRelease(Rows);}
        cstr Source=Installed?MdoExtensionText(Installed,"source",16):NULL;
        const xvalue* Record=xrtValueObjectGet(Installed,XRT_STR_LITERAL("installation"));
        cstr InstalledSha=MdoExtensionText(Record,"sha256",64);
        bool Pending=false;xrtValueGetBool(xrtValueObjectGet(Record,XRT_STR_LITERAL("pending_remove")),&Pending);
        bool First=!Before&&!Exists,Update=Exists&&!Pending&&Source&&!strcmp(Source,"builtin")&&
            (!MdoExtensionRefValid(MdoExtensionText(Installed,"id",MDO_EXTENSION_REF_BYTES))||!InstalledSha||strcmp(InstalledSha,Sha));
        char ApprovedRevision[65]={0};
        if(Ok&&Update){
            /* The installed receipt, rather than the new manifest parser,
             * proves whether an old builtin is safe to upgrade. */
            cstr Approved=MdoExtensionText(Record,"directory_digest",64);char Why[512]={0};
            bool Unchanged=Approved&&MdoExtensionsFingerprint(Kind,Id,ApprovedRevision,NULL,Why)&&!strcmp(Approved,ApprovedRevision);
            if(!Unchanged){
                if(Migrate){Ok=false;snprintf(Error,512,"Builtin %.96s was modified or could not be verified; its files were preserved. Inspect or restore it before retrying",Id);}
                else Update=false; /* Settings and other builtins remain usable. */
            }
        }
        if(Ok&&(First||Update)){
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
            Ok=Ok&&Credits&&xrtValueObjectSetNew(Credits,XRT_STR_LITERAL("expected_revision"),xrtValueString(xrtStrView(Update?ApprovedRevision:"")))&&
                xrtValueObjectSetNew(Credits,XRT_STR_LITERAL("source"),xrtValueString(XRT_STR_LITERAL("builtin")))&&
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
    xrtFree(Text);xrtValueRelease(Receipts);if(Ok)Error[0]=0;else if(!Error[0])snprintf(Error,512,"Cannot initialize builtin extension directories");return Ok;
}
bool MdoExtensionsSeedBuiltins(char Error[512]){return MdoExtensionsSeedBuiltinsMode(false,Error);}
bool MdoExtensionsMigrateBuiltins(char Error[512]){return MdoExtensionsSeedBuiltinsMode(true,Error);}

bool MdoExtensionsBuiltinUpdatePending(cstr Kind,cstr Directory,const xvalue* Row)
{
    const xvalue* Record=xrtValueObjectGet(Row,XRT_STR_LITERAL("installation"));
    cstr Source=MdoExtensionText(Record,"source",16),Hash=MdoExtensionText(Record,"sha256",64);bool Removing=false;
    xrtValueGetBool(xrtValueObjectGet(Record,XRT_STR_LITERAL("pending_remove")),&Removing);
    if(!Source||strcmp(Source,"builtin")||Removing)return false;
    for(size_t I=0;I<sizeof(MdoBuiltinExtensions)/sizeof(*MdoBuiltinExtensions);I++)
        if(!strcmp(Kind,MdoBuiltinExtensions[I].Kind)&&Directory&&!strcmp(Directory,MdoBuiltinExtensions[I].Id))
        {
            char Why[512]={0};
            return !MdoExtensionRefValid(MdoExtensionText(Row,"id",MDO_EXTENSION_REF_BYTES))||!Hash||strcmp(Hash,MdoBuiltinExtensions[I].Sha256)||!MdoExtensionsVerify(Kind,Directory,NULL,Why);
        }
    return false;
}
