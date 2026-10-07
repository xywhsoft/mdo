/* Included by extensions.c to reuse its validators, file policy and manager
 * ownership. A durable rollback journal precedes every multi-file write. */
#define ECO_RECEIPTS "data/extensions/installed.json"
#define ECO_JOURNAL "data/extensions/pending.json"
static bool EcoLocalSave(cstr path,const xvalue* value)
{size_t n=0;char* json=xrtJsonStringify(value,false,&n);size_t limit=!strcmp(path,ECO_JOURNAL)?4u*1024u*1024u:1024u*1024u;bool ok=json&&n<=limit&&MdoHomeAtomicWrite(path,json,n,false);xrtFree(json);return ok;}
static xvalue* EcoLocalRead(cstr path,size_t limit,bool empty)
{bool missing=false;char* text=MdoExtensionRead(path,true,limit,&missing);xvalue* doc=text?xrtJsonParse(xrtStrView(text)):missing&&empty?xrtValueObject():NULL;xrtFree(text);return doc;}
static bool EcoLocalPath(cstr kind,cstr id,char path[256])
{
    if(!MdoPackageKind(kind)||!MdoExtensionIdValid(id)||(!strcmp(kind,"agents")&&!strcmp(id,"default")))return false;
    if(!strcmp(kind,"c-agents")||!strcmp(kind,"c-subagents"))snprintf(path,256,"modules/%s/%s.c",!strcmp(kind,"c-agents")?"agents":"subagents",id);
    else ExtensionPath(kind,id,path);return true;
}
static bool EcoLocalAllowed(cstr path)
{
    if(!MdoPackagePath(path))return false;
    return !strncmp(path,"agents/",7)||!strncmp(path,"subagents/",10)||!strncmp(path,"tools/",6)||
        !strncmp(path,"skills/",7)||!strncmp(path,"mcp/",4)||!strncmp(path,"commands/",9)||
        !strncmp(path,"modules/agents/",15)||!strncmp(path,"modules/subagents/",18);
}
static bool EcoLocalPut(cstr path,const xvalue* encoded)
{
    xstrview text;if(!EcoLocalAllowed(path)||!xrtValueGetString(encoded,&text)||memchr(text.Data,0,text.Size))return false;
    size_t n=0;bytes b=xrtBase64DecodeNew(text.Data,text.Size,&n,NULL);bool ok=b&&n<=MDO_EXTENSION_TEXT_LIMIT&&MdoHomeAtomicWrite(path,b,n,false);xrtFree(b);return ok;
}
bool MdoEcosystemRecover(void)
{
    bool missing=false;char* text=MdoExtensionRead(ECO_JOURNAL,true,4u*1024u*1024u,&missing);
    if(!text)return missing;
    xvalue* journal=xrtJsonParse(xrtStrView(text));xrtFree(text);
    const xvalue* files=xrtValueObjectGet(journal,XRT_STR_LITERAL("files"));bool ok=journal&&xrtValueType(files)==XVALUE_ARRAY&&xrtValueCount(files)<=5000;
    /* Validate all journal destinations before restoring any of them. */
    for(size_t i=0;ok&&i<xrtValueCount(files);i++)ok=EcoLocalAllowed(MdoPackageText(xrtValueArrayGet(files,i),"path",255));
    for(size_t i=0;ok&&i<xrtValueCount(files);i++){const xvalue* f=xrtValueArrayGet(files,i);cstr path=MdoPackageText(f,"path",255);
        const xvalue* old=xrtValueObjectGet(f,XRT_STR_LITERAL("old"));ok=old&&xrtValueType(old)!=XVALUE_NULL?EcoLocalPut(path,old):MdoHomeRemove(path,false);}
    if(ok){const xvalue* old=xrtValueObjectGet(journal,XRT_STR_LITERAL("receipts"));ok=old&&xrtValueType(old)==XVALUE_OBJECT&&EcoLocalSave(ECO_RECEIPTS,old);}
    if(ok)ok=MdoHomeRemove(ECO_JOURNAL,false);xrtValueRelease(journal);return ok;
}
static bool EcoLocalReload(void)
{return MdoModuleManagerReload()&&MdoSkillManagerReload()&&MdoMcpManagerReload();}
static xvalue* EcoLocalFile(cstr path,const void* data,size_t size)
{
    char hash[65];char* b64=xrtBase64EncodeNew(data,size,NULL);xvalue* f=xrtValueObject();
    bool ok=b64&&f&&MdoExtensionHashBytes(data,size,hash)&&MdoApiValueSetString(f,"path",path)&&MdoApiValueSetString(f,"new",b64)&&MdoApiValueSetString(f,"sha256",hash);
    xrtFree(b64);if(!ok){xrtValueRelease(f);return NULL;}return f;
}
static bool EcoLocalAppend(xvalue* files,cstr path,const void* data,size_t size)
{
    for(size_t i=0;i<xrtValueCount(files);i++)if(MdoPackageSamePath(path,MdoPackageText(xrtValueArrayGet(files,i),"path",255)))return false;
    xvalue* f=EcoLocalFile(path,data,size);bool ok=f&&xrtValueArrayAppend(files,f);xrtValueRelease(f);return ok;
}
static xvalue* EcoLocalFiles(const xvalue* package,char error[1024])
{
    if(!MdoPackageValidate(package,error,1024))return NULL;
    xvalue* files=xrtValueArray();const xvalue* resources=xrtValueObjectGet(package,XRT_STR_LITERAL("resources"));bool ok=files!=NULL;
    for(size_t i=0;ok&&i<xrtValueCount(resources);i++){const xvalue* r=xrtValueArrayGet(resources,i);cstr kind=MdoPackageText(r,"kind",16),id=MdoPackageText(r,"id",64),content=MdoPackageText(r,"content",131072);char path[256];
        ok=EcoLocalPath(kind,id,path);
        if(ok&&strcmp(kind,"c-agents")&&strcmp(kind,"c-subagents")){
            /* MCP requires local setup after installation. Input references
             * stay placeholders; save/test in the existing MCP editor. */
            if(strcmp(kind,"mcp"))ok=ExtensionValidate(kind,id,content,error);
            else {xvalue* doc=xrtJsonParse(xrtStrView(content));ok=doc&&xrtValueObjectSetNew(doc,XRT_STR_LITERAL("enabled"),xrtValueBool(false));
                const xvalue* transport=xrtValueObjectGet(doc,XRT_STR_LITERAL("transport"));const char* keys[]={"headers","environment"};
                for(size_t k=0;ok&&k<2;k++){const xvalue* list=xrtValueObjectGet(transport,xrtStrView(keys[k]));for(size_t j=0;ok&&j<xrtValueCount(list);j++){xvalue* e=(xvalue*)xrtValueArrayGet(list,j);cstr ref=MdoPackageText(e,"secret_ref",256);char name[128];snprintf(name,sizeof(name),"env:MDO_MCP_%s_%s",id,ref?ref+6:"TOKEN");ok=xrtValueObjectSetNew(e,XRT_STR_LITERAL("secret_ref"),xrtValueString(xrtStrView(name)));}}
                char* normalized=ok?xrtJsonStringify(doc,true,NULL):NULL;ok=normalized&&ExtensionValidate(kind,id,normalized,error)&&EcoLocalAppend(files,path,normalized,strlen(normalized));xrtFree(normalized);xrtValueRelease(doc);continue;}
        }
        if(ok)ok=EcoLocalAppend(files,path,content,strlen(content));
        const xvalue* extras=xrtValueObjectGet(r,XRT_STR_LITERAL("files"));
        for(size_t j=0;ok&&j<xrtValueCount(extras);j++){const xvalue* e=xrtValueArrayGet(extras,j);cstr relative=MdoPackageText(e,"path",220),b64=MdoPackageText(e,"base64",180000);size_t n=0;bytes b=xrtBase64DecodeNew(b64,strlen(b64),&n,NULL);
            int count=snprintf(path,sizeof(path),"skills/%s/%s",id,relative);ok=count>0&&(size_t)count<sizeof(path)&&b&&EcoLocalAppend(files,path,b,n);xrtFree(b);}
    }
    if(!ok){if(!error[0])snprintf(error,1024,"Invalid resource file or duplicate destination");xrtValueRelease(files);return NULL;}return files;
}
static const xvalue* EcoLocalFind(const xvalue* files,cstr path)
{for(size_t i=0;i<xrtValueCount(files);i++){const xvalue* f=xrtValueArrayGet(files,i);if(MdoPackageSamePath(path,MdoPackageText(f,"path",255)))return f;}return NULL;}
static int64 EcoLocalNumber(const xvalue* object,cstr key)
{int64 n=0;(void)MdoPackageNumber(object,key,&n);return n;}
static bool EcoLocalUnchanged(const xvalue* owned)
{
    cstr path=MdoPackageText(owned,"path",255),expected=MdoPackageText(owned,"sha256",64);
    if(!path||!expected)return false;
    bool exists=false;xfileinfo info;
    if(!MdoHomeExternalStat(path,&exists,&info)||!exists||info.Type!=XFILE_TYPE_FILE||!(info.Available&XFILE_INFO_SIZE)||info.Size>MDO_EXTENSION_TEXT_LIMIT)return false;
    xfile file=MdoHomeOpenRead(path);size_t size=(size_t)info.Size;bytes data=xrtMalloc(size+1);char hash[65];
    bool same=file&&data&&xrtReadFull(file,data,size,NULL)&&MdoExtensionHashBytes(data,size,hash)&&!strcmp(hash,expected);
    if(file)xrtClose(file);xrtFree(data);return same;
}
static bool EcoLocalSkillPrefix(cstr path,char prefix[256])
{
    if(!path||strncmp(path,"skills/",7))return false;
    cstr end=strchr(path+7,'/');if(!end)return false;
    size_t size=(size_t)(end-path)+1;memcpy(prefix,path,size);prefix[size]=0;return true;
}
static bool EcoLocalCurrent(cstr path,xvalue* change,const xvalue* owned,char error[1024])
{
    if(!EcoLocalAllowed(path))return false;
    bool exists=false;xfileinfo info;if(!MdoHomeExternalStat(path,&exists,&info))return false;
    if(!exists){if(owned){snprintf(error,1024,"Installed file is missing: %s",path);return false;}
        xfile builtin=MdoResourceOpenRead(path);if(builtin){xrtClose(builtin);snprintf(error,1024,"Built-in resource cannot be replaced: %s",path);return false;}
        xrtClearError();return xrtValueObjectSetNew(change,XRT_STR_LITERAL("old"),xrtValueNull());}
    if(!owned||info.Type!=XFILE_TYPE_FILE||!(info.Available&XFILE_INFO_SIZE)||info.Size>MDO_EXTENSION_TEXT_LIMIT){snprintf(error,1024,"Resource already exists: %s",path);return false;}
    xfile f=MdoHomeOpenRead(path);size_t n=(size_t)info.Size;bytes b=xrtMalloc(n+1);char hash[65];bool ok=f&&b&&xrtReadFull(f,b,n,NULL)&&MdoExtensionHashBytes(b,n,hash)&&!strcmp(hash,MdoPackageText(owned,"sha256",64));if(f)xrtClose(f);
    if(!ok)snprintf(error,1024,"Resource was edited locally: %s. Keep it or export/remove it before updating.",path);
    char* encoded=ok?xrtBase64EncodeNew(b,n,NULL):NULL;ok=encoded&&MdoApiValueSetString(change,"old",encoded);xrtFree(encoded);xrtFree(b);return ok;
}
static bool EcoLocalApply(const xvalue* entry,bool uninstall,char error[1024])
{
    const xvalue* package=xrtValueObjectGet(entry,XRT_STR_LITERAL("package"));char key[80];int64 id=0;
    cstr source=MdoPackageText(entry,"source",16);bool local=source&&!strcmp(source,"local");
    if(local){cstr slug=MdoPackageText(entry,"slug",64);if(!MdoPackageId(slug))return false;snprintf(key,sizeof(key),"local:%s",slug);}
    else {if(!MdoPackageNumber(entry,"id",&id)||id<=0)return false;snprintf(key,sizeof(key),"%lld",(long long)id);}
    xvalue* receipts=EcoLocalRead(ECO_RECEIPTS,1024u*1024u,true);if(!receipts||xrtValueType(receipts)!=XVALUE_OBJECT){xrtValueRelease(receipts);return false;}
    /* Versions have distinct server IDs; updates replace the receipt owned by
     * the same authenticated author and slug, preserving canonical paths. */
    const xvalue* prior=NULL;char priorKey[80]={0};xvalueiter it={0};xvaluekey k;xvalue* value;
    if(xrtValueIterBegin(receipts,&it)){while((value=xrtValueIterNext(&it,&k))){cstr storedSource=MdoPackageText(value,"source",16);bool storedLocal=storedSource&&!strcmp(storedSource,"local");bool same=uninstall?xrtStrEqual(k.String,xrtStrView(key)):local==storedLocal&&
        MdoPackageText(value,"slug",64)&&MdoPackageText(entry,"slug",64)&&!strcmp(MdoPackageText(value,"slug",64),MdoPackageText(entry,"slug",64))&&
        EcoLocalNumber(value,"owner")==EcoLocalNumber(entry,"owner");
        if(same&&k.String.Size<sizeof(priorKey)){prior=value;memcpy(priorKey,k.String.Data,k.String.Size);priorKey[k.String.Size]=0;break;}}xrtValueIterEnd(&it);}
    if(uninstall&&!prior){snprintf(error,1024,"Package is not installed");xrtValueRelease(receipts);return false;}
    xvalue* files=uninstall?xrtValueArray():EcoLocalFiles(package,error);
    const xvalue* oldFiles=xrtValueObjectGet(prior,XRT_STR_LITERAL("files"));bool ok=files!=NULL;
    /* An edited Skill is retained as a complete local resource. Removing its
     * unchanged SKILL.md while retaining an edited attachment would orphan it. */
    char keptSkills[16][256];size_t keptCount=0;
    for(size_t i=0;uninstall&&i<xrtValueCount(oldFiles);i++){
        const xvalue* old=xrtValueArrayGet(oldFiles,i);char prefix[256];
        if(EcoLocalSkillPrefix(MdoPackageText(old,"path",255),prefix)&&!EcoLocalUnchanged(old)){
            bool seen=false;for(size_t j=0;j<keptCount;j++)if(!strcmp(keptSkills[j],prefix))seen=true;
            if(!seen&&keptCount<16)strcpy(keptSkills[keptCount++],prefix);
        }
    }
    for(size_t i=0;ok&&i<xrtValueCount(files);i++){xvalue* f=(xvalue*)xrtValueArrayGet(files,i);cstr path=MdoPackageText(f,"path",255);ok=EcoLocalCurrent(path,f,EcoLocalFind(oldFiles,path),error);}
    for(size_t i=0;ok&&i<xrtValueCount(oldFiles);i++){const xvalue* old=xrtValueArrayGet(oldFiles,i);cstr path=MdoPackageText(old,"path",255);if(EcoLocalFind(files,path))continue;
        if(uninstall){bool keep=!EcoLocalUnchanged(old);for(size_t j=0;path&&j<keptCount;j++)if(!strncmp(path,keptSkills[j],strlen(keptSkills[j])))keep=true;if(keep)continue;}
        xvalue* f=xrtValueObject();bool current=f&&MdoApiValueSetString(f,"path",path)&&EcoLocalCurrent(path,f,old,error);
        if(uninstall&&!current&&strstr(error,"Resource was edited locally:")){error[0]=0;xrtValueRelease(f);continue;}
        ok=current&&xrtValueObjectSetNew(f,XRT_STR_LITERAL("new"),xrtValueNull())&&xrtValueArrayAppend(files,f);xrtValueRelease(f);}
    xvalue* journal=xrtValueObject(),*next=xrtValueClone(receipts),*record=xrtValueObject(),*inventory=xrtValueArray();
    if(ok)ok=journal&&next&&record&&inventory&&xrtValueObjectSet(journal,XRT_STR_LITERAL("files"),files)&&xrtValueObjectSet(journal,XRT_STR_LITERAL("receipts"),receipts);
    if(ok&&priorKey[0])xrtValueObjectRemove(next,xrtStrView(priorKey));
    if(ok&&!uninstall){const char* keys[]={"id","owner","slug","name","version","author","sha256"};for(size_t i=0;ok&&i<7;i++)ok=xrtValueObjectSet(record,xrtStrView(keys[i]),xrtValueObjectGet(entry,xrtStrView(keys[i])));
        const xvalue* manifest=xrtValueObjectGet(package,XRT_STR_LITERAL("manifest"));
        ok=ok&&MdoApiValueSetString(record,"key",key)&&MdoApiValueSetString(record,"source",local?"local":"store")&&xrtValueObjectSet(record,XRT_STR_LITERAL("manifest"),manifest);
        xvalue* refs=xrtValueArray();const xvalue* rs=xrtValueObjectGet(package,XRT_STR_LITERAL("resources"));
        for(size_t i=0;ok&&i<xrtValueCount(rs);i++){const xvalue* r=xrtValueArrayGet(rs,i);xvalue* ref=xrtValueObject();ok=ref&&xrtValueObjectSet(ref,XRT_STR_LITERAL("kind"),xrtValueObjectGet(r,XRT_STR_LITERAL("kind")))&&xrtValueObjectSet(ref,XRT_STR_LITERAL("id"),xrtValueObjectGet(r,XRT_STR_LITERAL("id")))&&xrtValueArrayAppend(refs,ref);xrtValueRelease(ref);}
        if(ok)ok=xrtValueObjectSet(record,XRT_STR_LITERAL("resources"),refs);xrtValueRelease(refs);
        for(size_t i=0;ok&&i<xrtValueCount(files);i++){const xvalue* f=xrtValueArrayGet(files,i);if(xrtValueType(xrtValueObjectGet(f,XRT_STR_LITERAL("new")))==XVALUE_NULL)continue;
            xvalue* item=xrtValueObject();ok=item&&xrtValueObjectSet(item,XRT_STR_LITERAL("path"),xrtValueObjectGet(f,XRT_STR_LITERAL("path")))&&xrtValueObjectSet(item,XRT_STR_LITERAL("sha256"),xrtValueObjectGet(f,XRT_STR_LITERAL("sha256")))&&xrtValueArrayAppend(inventory,item);xrtValueRelease(item);}
        if(ok)ok=xrtValueObjectSet(record,XRT_STR_LITERAL("files"),inventory)&&xrtValueObjectSet(next,xrtStrView(key),record);}
    bool begun=ok&&EcoLocalSave(ECO_JOURNAL,journal);ok=begun;
    for(size_t i=0;ok&&i<xrtValueCount(files);i++){const xvalue* f=xrtValueArrayGet(files,i);const xvalue* incoming=xrtValueObjectGet(f,XRT_STR_LITERAL("new"));cstr path=MdoPackageText(f,"path",255);
        ok=xrtValueType(incoming)==XVALUE_NULL?MdoHomeRemove(path,false):EcoLocalPut(path,incoming);}
    if(ok)ok=EcoLocalReload()&&EcoLocalSave(ECO_RECEIPTS,next)&&MdoHomeRemove(ECO_JOURNAL,false);
    if(!ok&&begun){if(!MdoEcosystemRecover()||!EcoLocalReload())(void)MdoHomeRequireRestart("Extension package rollback failed; restart required");}
    if(!ok&&!error[0])snprintf(error,1024,"Package installation failed; previous resources restored. Check configuration and tool references.");
    xrtValueRelease(receipts);xrtValueRelease(files);xrtValueRelease(journal);xrtValueRelease(next);xrtValueRelease(record);xrtValueRelease(inventory);return ok;
}
#include "ecosystem_drafts.inc.c"
bool MdoApiEcosystemLocal(MdoApiContext* c,const xvalue* input)
{
    cstr action=MdoPackageText(input,"action",16);char error[1024]={0};bool ok=false;
    xrtMutexLock(g_MdoExtensionLock);
    if(action&&(!strcmp(action,"drafts")||!strcmp(action,"draft_read")||!strcmp(action,"draft_save")||!strcmp(action,"draft_delete")))ok=EcoDraftAction(c,input);
    else if(action&&!strcmp(action,"installed")){xvalue* records=EcoLocalRead(ECO_RECEIPTS,1024u*1024u,true);ok=records?MdoApiReplySuccessTake(c,200,records,NULL):MdoApiReplyError(c,503,"ecosystem_receipts","Cannot read installed packages",NULL);}
    else if(action&&!strcmp(action,"import")){
        const xvalue* package=xrtValueObjectGet(input,XRT_STR_LITERAL("package"));
        ok=MdoEcosystemPackageCompatible(package,input,error,sizeof(error));
        xvalue* entry=xrtValueObject();const xvalue* manifest=xrtValueObjectGet(package,XRT_STR_LITERAL("manifest"));
        size_t n=0;char* json=ok?xrtJsonStringify(package,false,&n):NULL;char hash[65];
        ok=ok&&entry&&json&&n<=MDO_PACKAGE_LIMIT&&MdoExtensionHashBytes(json,n,hash)&&
           xrtValueObjectSet(entry,XRT_STR_LITERAL("package"),package)&&MdoApiValueSetString(entry,"source","local")&&
           MdoApiValueSetString(entry,"author","")&&MdoApiValueSetUInt(entry,"id",0)&&MdoApiValueSetUInt(entry,"owner",0)&&MdoApiValueSetString(entry,"sha256",hash);
        const char* keys[]={"slug","name","version"};for(size_t i=0;ok&&i<3;i++)ok=xrtValueObjectSet(entry,xrtStrView(keys[i]),xrtValueObjectGet(manifest,xrtStrView(keys[i])));
        if(ok)ok=EcoLocalApply(entry,false,error);xrtFree(json);xrtValueRelease(entry);
        if(!ok&&!error[0])snprintf(error,sizeof(error),"Invalid local package");
        ok=ok?MdoApiReplySuccessTake(c,200,xrtValueObject(),NULL):MdoApiReplyError(c,409,"ecosystem_import_failed",error,NULL);
    }
    else if(action&&!strcmp(action,"c_sources")){
        xvalue* items=xrtValueArray();const char* directories[]={"modules/agents","modules/subagents"};
        ok=items!=NULL;
        for(size_t k=0;ok&&k<2;k++){xdir dir=MdoHomeOpenDirectory(directories[k],XDIR_STAT);if(!dir){xrtClearError();continue;}xdirentry entry;xdirnext next;
            while((next=xrtDirNext(dir,&entry))==XDIR_NEXT_ITEM){size_t n=entry.Name.Size;if(entry.Info.Type!=XFILE_TYPE_FILE||n<3||n>66||memcmp(entry.Name.Data+n-2,".c",2))continue;
                char id[65];memcpy(id,entry.Name.Data,n-2);id[n-2]=0;if(!MdoExtensionIdValid(id))continue;
                xvalue* item=xrtValueObject();ok=item&&MdoApiValueSetString(item,"kind",k?"c-subagents":"c-agents")&&MdoApiValueSetString(item,"id",id)&&MdoApiValueSetString(item,"name",id)&&xrtValueArrayAppend(items,item);xrtValueRelease(item);if(!ok)break;}
            if(next==XDIR_NEXT_ERROR)ok=false;xrtDirClose(dir);
        }
        if(ok){xvalue* data=xrtValueObject();xrtValueObjectSet(data,XRT_STR_LITERAL("items"),items);ok=MdoApiReplySuccessTake(c,200,data,NULL);}
        else ok=MdoApiReplyError(c,503,"ecosystem_export_failed","Cannot enumerate custom C profiles",NULL);
        xrtValueRelease(items);
    }
    else if(action&&!strcmp(action,"export")){
        const xvalue* refs=xrtValueObjectGet(input,XRT_STR_LITERAL("references"));xvalue* resources=xrtValueArray();
        ok=xrtValueType(refs)==XVALUE_ARRAY&&xrtValueCount(refs)>0&&xrtValueCount(refs)<=16&&resources;
        for(size_t i=0;ok&&i<xrtValueCount(refs);i++){const xvalue* ref=xrtValueArrayGet(refs,i);cstr kind=MdoPackageText(ref,"kind",16),id=MdoPackageText(ref,"id",64);char path[256];
            ok=EcoLocalPath(kind,id,path);bool missing=false;char* content=ok?MdoExtensionRead(path,false,MDO_EXTENSION_TEXT_LIMIT,&missing):NULL;xvalue* resource=xrtValueObject();
            ok=content&&resource&&MdoApiValueSetString(resource,"kind",kind)&&MdoApiValueSetString(resource,"id",id);
            if(ok&&!strcmp(kind,"mcp")){xvalue* doc=xrtJsonParse(xrtStrView(content));xvalue* transport=(xvalue*)xrtValueObjectGet(doc,XRT_STR_LITERAL("transport"));const char* keys[]={"environment","headers"};size_t index=0;
                ok=doc&&transport;for(size_t k=0;ok&&k<2;k++){const xvalue* values=xrtValueObjectGet(transport,xrtStrView(keys[k]));for(size_t j=0;ok&&j<xrtValueCount(values);j++){xvalue* v=(xvalue*)xrtValueArrayGet(values,j);char placeholder[24];snprintf(placeholder,sizeof(placeholder),"input:%zu",index++);ok=MdoApiValueSetString(v,"secret_ref",placeholder);}}
                if(ok)xrtValueObjectRemove(transport,XRT_STR_LITERAL("working_directory"));char* redacted=ok?xrtJsonStringify(doc,true,NULL):NULL;xrtValueRelease(doc);xrtFree(content);content=redacted;ok=content!=NULL;
            }
            if(ok)ok=MdoApiValueSetString(resource,"content",content);
            if(ok&&!strcmp(kind,"skills")){xvalue* extras=xrtValueArray();char directory[160];bool external=false;xfileinfo info;size_t total=0;snprintf(directory,sizeof(directory),"skills/%s",id);
                ok=extras&&MdoHomeExternalStat(directory,&external,&info)&&ExtensionBundleFiles(directory,"",external,extras,&total,0)&&xrtValueObjectSet(resource,XRT_STR_LITERAL("files"),extras);xrtValueRelease(extras);}
            if(ok)ok=xrtValueArrayAppend(resources,resource);xrtFree(content);xrtValueRelease(resource);
        }
        if(ok){xvalue* out=xrtValueObject();xrtValueObjectSet(out,XRT_STR_LITERAL("resources"),resources);ok=MdoApiReplySuccessTake(c,200,out,NULL);}
        else ok=MdoApiReplyError(c,422,"ecosystem_export_failed","Select 1–16 valid resources. Default Agent must be duplicated before publishing; Skill export is limited to 128 KiB of attachments.",NULL);
        xrtValueRelease(resources);
    }
    else if(action&&(!strcmp(action,"install")||!strcmp(action,"uninstall"))){
        ok=EcoLocalApply(xrtValueObjectGet(input,XRT_STR_LITERAL("entry")),!strcmp(action,"uninstall"),error);
        ok=ok?MdoApiReplySuccessTake(c,200,xrtValueObject(),NULL):MdoApiReplyError(c,409,"ecosystem_install_failed",error,NULL);
    }else ok=MdoApiReplyError(c,400,"ecosystem_action","Unknown local action",NULL);
    xrtMutexUnlock(g_MdoExtensionLock);return ok;
}
