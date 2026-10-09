static void MdoDistProgress(uint64 Done,uint64 Total,cstr Stage)
{
    xrtMutexLock(g_MdoDistribution.Lock);g_MdoDistribution.Done=Done;g_MdoDistribution.Total=Total;
    snprintf(g_MdoDistribution.Stage,sizeof(g_MdoDistribution.Stage),"%s",Stage);xrtMutexUnlock(g_MdoDistribution.Lock);
}
static void MdoDistDownloadProgress(uint64 Done,uint64 Total,void* Data)
{(void)Data;MdoDistProgress(Done,Total,"downloading");}
static bool MdoDistCompatible(xvalue* Package,xvalue* Active)
{
    if(!Package||!strcmp(MdoDistText(Package,"status"),"withdrawn"))return false;
    uint64 Min=MdoDistNumber(Package,"min_build"),Max=MdoDistNumber(Package,"max_build");
    if((Min&&MdoBuildId()<Min)||(Max&&MdoBuildId()>=Max))return false;
    xvalue* Deps=xrtValueObjectGet(Package,XRT_STR_LITERAL("dependencies"));
    if(Deps&&xrtValueType(Deps)!=XVALUE_ARRAY)return false;
    for(size_t i=0;i<xrtValueCount(Deps);i++) {
        xvalue* Dep=xrtValueArrayGet(Deps,i);cstr Id=MdoDistText(Dep,"id");char Key[32];
        if(strcmp(Id,"core")&&strcmp(Id,"python"))return false;
        snprintf(Key,sizeof(Key),"%s_revision",Id);if(!MdoDistText(Active,Id)[0]||MdoDistNumber(Active,Key)<MdoDistNumber(Dep,"revision"))return false;
    }return true;
}
static bool MdoDistGeneration(cstr Path)
{return MdoDistValidRelative(Path)&&(!strncmp(Path,"data/toolpacks/core/",20)||!strncmp(Path,"data/toolpacks/python/",22));}
static xvalue* MdoDistMetadata(xvalue* Package)
{
    /* Retain compatibility and declared versions without repeated descriptions. */
    xvalue* Metadata=xrtValueObject();const cstr Keys[]={"id","revision","min_build","max_build","dependencies","tool_versions"};
    for(size_t i=0;i<6;i++){xvalue* Value=xrtValueObjectGet(Package,xrtStrView(Keys[i]));if(Value)xrtValueObjectSetNew(Metadata,xrtStrView(Keys[i]),xrtValueClone(Value));}
    xvalue* Rows=xrtValueObjectGet(Package,XRT_STR_LITERAL("tools"));
    if(xrtValueType(Rows)==XVALUE_ARRAY){xvalue* Versions=xrtValueObject();for(size_t i=0;i<xrtValueCount(Rows);i++){xvalue* Tool=xrtValueArrayGet(Rows,i);MdoDistSet(Versions,MdoDistText(Tool,"id"),MdoDistText(Tool,"version"));}xrtValueObjectSetNew(Metadata,XRT_STR_LITERAL("tool_versions"),Versions);}
    return Metadata;
}
static bool MdoDistInstalledCompatible(xvalue* Active)
{const cstr Ids[]={"core","python"};for(size_t i=0;i<2;i++)if(MdoDistText(Active,Ids[i])[0]){char Key[32];snprintf(Key,sizeof(Key),"%s_metadata",Ids[i]);xvalue* Metadata=xrtValueObjectGet(Active,xrtStrView(Key));if(Metadata&&!MdoDistCompatible(Metadata,Active))return false;}return true;}
static void MdoDistRetire(xvalue* Active,cstr Id)
{
    cstr Path=MdoDistText(Active,Id);if(!Path[0]||!MdoDistGeneration(Path))return;
    xvalue* Rows=xrtValueObjectGet(Active,XRT_STR_LITERAL("retired"));
    if(xrtValueType(Rows)!=XVALUE_ARRAY){Rows=xrtValueArray();xrtValueObjectSetNew(Active,XRT_STR_LITERAL("retired"),Rows);}
    for(size_t i=0;i<xrtValueCount(Rows);i++)if(!strcmp(Path,MdoDistText(xrtValueArrayGet(Rows,i),"path")))return;
    xvalue* Row=xrtValueObject();char Key[32];snprintf(Key,sizeof(Key),"%s_revision",Id);
    MdoDistSet(Row,"id",Id);MdoDistSet(Row,"path",Path);xrtValueObjectSetNew(Row,XRT_STR_LITERAL("revision"),xrtValueUInt(MdoDistNumber(Active,Key)));xrtValueArrayAppendNew(Rows,Row);
    snprintf(Key,sizeof(Key),"%s_metadata",Id);xvalue* Metadata=xrtValueObjectGet(Active,xrtStrView(Key));if(Metadata)xrtValueObjectSetNew(Row,XRT_STR_LITERAL("metadata"),MdoDistMetadata(Metadata));
}
static bool MdoDistReceipt(cstr Path)
{
    if(!MdoDistGeneration(Path))return false;
    char Relative[384];snprintf(Relative,sizeof(Relative),"%s/toolpack.receipt.json",Path);
    xvalue* Manifest=MdoDistRead(Relative,1048576);xvalue* Files=xrtValueObjectGet(Manifest,XRT_STR_LITERAL("files"));bool Ok=xrtValueType(Files)==XVALUE_ARRAY&&xrtValueCount(Files)>0;
    for(size_t i=0;Ok&&i<xrtValueCount(Files);i++) {
        xvalue* Entry=xrtValueArrayGet(Files,i);cstr File=MdoDistText(Entry,"path");cstr Hash=MdoDistText(Entry,"sha256");
        if(!MdoDistValidRelative(File)||strlen(Hash)!=64){Ok=false;break;}
        snprintf(Relative,sizeof(Relative),"%s/%s",Path,File);xfile Input=MdoHomeOpenRead(Relative);uint8 Buffer[65536],Digest[32];xsha256 Sha;uint64 Size=0;char Hex[65];xrtSha256Init(&Sha);Ok=Input!=NULL;
        while(Ok){size_t Got=0;Ok=!xrtCancelRequested(g_MdoDistribution.Operation)&&xrtRead(Input,Buffer,sizeof(Buffer),&Got);if(!Ok||!Got)break;Size+=Got;Ok=Size<=MdoDistNumber(Entry,"size")&&xrtSha256Update(&Sha,Buffer,Got);}
        Ok=Ok&&Size==MdoDistNumber(Entry,"size")&&xrtSha256Final(&Sha,Digest);xrtClose(Input);
        if(Ok){for(size_t j=0;j<32;j++)snprintf(Hex+j*2,3,"%02x",Digest[j]);Ok=!strcmp(Hash,Hex);}
    }xrtValueRelease(Manifest);return Ok;
}
static bool MdoDistRemoveTree(cstr Path,unsigned Depth,size_t* Count)
{
    bool Exists=false;xfileinfo Info={0};if(Depth>24||++*Count>20000||!MdoDistGeneration(Path)||!MdoHomeExternalStat(Path,&Exists,&Info))return false;
    if(!Exists)return true;
    if(Info.Type==XFILE_TYPE_FILE)return MdoHomeRemove(Path,false);
    if(Info.Type!=XFILE_TYPE_DIRECTORY)return false; /* never follow a substituted link */
    xdir Dir=MdoHomeOpenDirectory(Path,XDIR_STAT);if(!Dir)return false;xdirentry Entry;xdirnext Next;bool Ok=true;
    while((Next=xrtDirNext(Dir,&Entry))==XDIR_NEXT_ITEM){if(!(Entry.Flags&XDIR_ENTRY_UTF8)||!Entry.Name.Size||Entry.Name.Size>220){Ok=false;break;}char Child[512];int Length=snprintf(Child,sizeof(Child),"%s/%.*s",Path,(int)Entry.Name.Size,Entry.Name.Data);Ok=Length>0&&(size_t)Length<sizeof(Child)&&MdoDistRemoveTree(Child,Depth+1,Count);if(!Ok)break;}
    Ok=Ok&&Next==XDIR_NEXT_END;xrtDirClose(Dir);return Ok&&MdoHomeRemoveEmptyDirectory(Path);
}
static bool MdoDistMaintain(unsigned Command,cstr Id)
{
    xrtMutexLock(g_MdoDistribution.Lock);xvalue* Active=xrtValueClone(g_MdoDistribution.Active);xvalue* Eligible=xrtValueClone(g_MdoDistribution.CleanupEligible);xrtMutexUnlock(g_MdoDistribution.Lock);
    bool Ok=false;char Key[32];snprintf(Key,sizeof(Key),"%s_revision",Id);xvalue* Retired=xrtValueObjectGet(Active,XRT_STR_LITERAL("retired"));
    if(Command==3) {
        /* Uninstall deactivates atomically. Existing runs retain their immutable paths. */
        MdoDistRetire(Active,Id);xrtValueObjectRemove(Active,xrtStrView(Id));xrtValueObjectRemove(Active,xrtStrView(Key));snprintf(Key,sizeof(Key),"%s_metadata",Id);xrtValueObjectRemove(Active,xrtStrView(Key));Ok=MdoDistInstalledCompatible(Active);
    }else if(Command==4) {
        xvalue* Chosen=NULL;
        for(size_t i=0;i<xrtValueCount(Retired);i++)if(!strcmp(Id,MdoDistText(xrtValueArrayGet(Retired,i),"id")))Chosen=xrtValueArrayGet(Retired,i);
        if(Chosen&&MdoDistReceipt(MdoDistText(Chosen,"path"))){str Path=xrtStrDup(MdoDistText(Chosen,"path"));uint64 Revision=MdoDistNumber(Chosen,"revision");xvalue* Metadata=xrtValueClone(xrtValueObjectGet(Chosen,XRT_STR_LITERAL("metadata")));xvalue* Keep=xrtValueArray();
            for(size_t i=0;i<xrtValueCount(Retired);i++)if(strcmp(Path,MdoDistText(xrtValueArrayGet(Retired,i),"path")))xrtValueArrayAppendNew(Keep,xrtValueClone(xrtValueArrayGet(Retired,i)));
            xrtValueObjectSetNew(Active,XRT_STR_LITERAL("retired"),Keep);MdoDistRetire(Active,Id);MdoDistSet(Active,Id,Path);xrtValueObjectSetNew(Active,xrtStrView(Key),xrtValueUInt(Revision));snprintf(Key,sizeof(Key),"%s_metadata",Id);if(Metadata)xrtValueObjectSetNew(Active,xrtStrView(Key),Metadata);else xrtValueObjectRemove(Active,xrtStrView(Key));xrtFree(Path);Ok=MdoDistInstalledCompatible(Active);}
    }else if(Command==5) {
        xvalue* Keep=xrtValueArray();Ok=true;MdoDistProgress(0,xrtValueCount(Retired),"cleaning");
        for(size_t i=0;i<xrtValueCount(Retired);i++){xvalue* Row=xrtValueArrayGet(Retired,i);cstr Path=MdoDistText(Row,"path");bool Allowed=false;
            for(size_t j=0;j<xrtValueCount(Eligible);j++)if(!strcmp(Path,MdoDistText(xrtValueArrayGet(Eligible,j),"path")))Allowed=true;
            if(!strcmp(Path,MdoDistText(Active,"core"))||!strcmp(Path,MdoDistText(Active,"python")))Allowed=false;
            size_t Count=0;bool Removed=Allowed&&!xrtCancelRequested(g_MdoDistribution.Operation)&&MdoDistRemoveTree(Path,0,&Count);
            if(!Removed)xrtValueArrayAppendNew(Keep,xrtValueClone(Row));
            if(Allowed&&!Removed)Ok=false;
            MdoDistProgress(i+1,xrtValueCount(Retired),"cleaning");}
        xrtValueObjectSetNew(Active,XRT_STR_LITERAL("retired"),Keep);
        /* Persist any successful removals even when a later directory could not be removed. */
        bool Saved=MdoDistSave("data/toolpacks/active.json",Active);Ok=Saved&&Ok;
        if(Saved){xvalue* Remaining=xrtValueArray();for(size_t i=0;i<xrtValueCount(Keep);i++)for(size_t j=0;j<xrtValueCount(Eligible);j++)if(!strcmp(MdoDistText(xrtValueArrayGet(Keep,i),"path"),MdoDistText(xrtValueArrayGet(Eligible,j),"path")))xrtValueArrayAppendNew(Remaining,xrtValueClone(xrtValueArrayGet(Keep,i)));
            xrtMutexLock(g_MdoDistribution.Lock);xrtValueRelease(g_MdoDistribution.Active);g_MdoDistribution.Active=xrtValueClone(Active);xrtValueRelease(g_MdoDistribution.CleanupEligible);g_MdoDistribution.CleanupEligible=Remaining;xrtMutexUnlock(g_MdoDistribution.Lock);}
    }
    if(Ok&&Command!=5){MdoDistProgress(0,1,"activating");xvalue* Tools=MdoDistInventory(Active);
        xrtValueObjectSetNew(Active,XRT_STR_LITERAL("capability_revision"),xrtValueUInt(MdoDistNumber(Active,"capability_revision")+1));
        Ok=Tools&&!xrtCancelRequested(g_MdoDistribution.Operation)&&MdoDistSave("data/toolpacks/active.json",Active);
        if(Ok){xrtMutexLock(g_MdoDistribution.Lock);xrtValueRelease(g_MdoDistribution.Active);g_MdoDistribution.Active=Active;Active=NULL;xrtValueRelease(g_MdoDistribution.Tools);g_MdoDistribution.Tools=Tools;Tools=NULL;xrtMutexUnlock(g_MdoDistribution.Lock);}xrtValueRelease(Tools);
    }
    xrtValueRelease(Active);xrtValueRelease(Eligible);return Ok;
}
