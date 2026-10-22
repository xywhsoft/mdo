/* Local authoring is available without a service session. Export exact files,
 * never machine credentials, absolute roots or executable installation hooks. */
static bool EcoNativeSourceList(xvalue* Items)
{
    const char* Dirs[]={"modules/tools","data/ecosystem/toolpacks"};
    for(size_t k=0;k<2;k++){
        xdir Dir=MdoHomeOpenDirectory(Dirs[k],XDIR_STAT);if(!Dir){xrtClearError();continue;}xdirentry E;xdirnext Next;bool Ok=true;
        while((Next=xrtDirNext(Dir,&E))==XDIR_NEXT_ITEM){
            size_t Suffix=k?5:2,N=E.Name.Size;if(E.Info.Type!=XFILE_TYPE_FILE||N<=Suffix||N>64+Suffix||memcmp(E.Name.Data+N-Suffix,k?".json":".c",Suffix))continue;
            char Id[65];memcpy(Id,E.Name.Data,N-Suffix);Id[N-Suffix]=0;if(!MdoExtensionIdValid(Id))continue;
            xvalue* R=xrtValueObject();Ok=R&&MdoApiValueSetString(R,"kind",k?"toolpacks":"modules")&&MdoApiValueSetString(R,"id",Id)&&MdoApiValueSetString(R,"name",Id)&&xrtValueArrayAppend(Items,R);xrtValueRelease(R);if(!Ok)break;
        }
        xrtDirClose(Dir);if(!Ok||Next==XDIR_NEXT_ERROR)return false;
    }
    xvalue* Config=NULL;char Error[256];if(!MdoApplicationPluginsStoreBegin(&Config,Error,sizeof(Error)))return false;MdoApplicationPluginsStoreEnd();
    bool Ok=true;for(size_t i=0;Ok&&i<xrtValueCount(Config);i++){
        cstr Id=MdoPackageText(xrtValueArrayGet(Config,i),"id",64);xvalue* R=xrtValueObject();
        Ok=Id&&R&&MdoApiValueSetString(R,"kind","plugins")&&MdoApiValueSetString(R,"id",Id)&&MdoApiValueSetString(R,"name",Id)&&xrtValueArrayAppend(Items,R);xrtValueRelease(R);
    }xrtValueRelease(Config);return Ok;
}
static bool EcoNativeExportFile(xvalue* Extras,cstr Root,cstr Name,size_t* Total)
{
    if(!MdoPackagePath(Name))return false;
    char Path[512];int N=snprintf(Path,sizeof(Path),"%s/%s",Root,Name);if(N<=0||(size_t)N>=sizeof(Path))return false;
    xfile F=MdoHomeOpenRead(Path);xfileinfo Info={0};bool Ok=F&&xrtFileStat(F,&Info)&&Info.Type==XFILE_TYPE_FILE&&Info.Size<=1048576u&&*Total+Info.Size<=4u*1024u*1024u;
    bytes Data=Ok?xrtMalloc((size_t)Info.Size+1):NULL;Ok=Ok&&Data&&xrtReadFull(F,Data,(size_t)Info.Size,NULL);if(F)xrtClose(F);
    char* B64=Ok?xrtBase64EncodeNew(Data,(size_t)Info.Size,NULL):NULL;xvalue* E=xrtValueObject();
    Ok=Ok&&B64&&E&&MdoApiValueSetString(E,"path",Name)&&MdoApiValueSetString(E,"base64",B64)&&xrtValueArrayAppend(Extras,E);
    if(Ok)*Total+=(size_t)Info.Size;
    xrtFree(B64);xrtFree(Data);xrtValueRelease(E);return Ok;
}
static bool EcoNativeExport(cstr Kind,cstr Id,xvalue* Resource)
{
    if(!MdoExtensionIdValid(Id))return false;
    char Path[256],Root[256];xvalue *Doc=NULL,*Config=NULL;bool Missing=false;char* Raw=NULL;bool Plugin=!strcmp(Kind,"plugins");
    if(Plugin){char Error[256];if(!MdoApplicationPluginsStoreBegin(&Config,Error,sizeof(Error)))return false;MdoApplicationPluginsStoreEnd();
        const xvalue* M=EcoNativeFind(Config,Id);cstr Revision=MdoPackageText(M,"revision",64);
        if(Revision){snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Revision);snprintf(Root,sizeof(Root),"plugins/%s/revisions/%s/files",Id,Revision);}
        else {xrtValueRelease(Config);return false;}
    }else snprintf(Path,sizeof(Path),"data/ecosystem/toolpacks/%s.json",Id);
    Raw=MdoExtensionRead(Path,false,1048576u,&Missing);Doc=Raw?xrtJsonParse(xrtStrView(Raw)):NULL;xrtFree(Raw);xrtValueRelease(Config);
    if(!Plugin){cstr R=MdoPackageText(Doc,"root",220);if(!R||strncmp(R,"data/ecosystem/toolpacks/",24)||!MdoPackagePath(R)){xrtValueRelease(Doc);return false;}snprintf(Root,sizeof(Root),"%s",R);}
    const xvalue* Inventory=xrtValueObjectGet(Doc,XRT_STR_LITERAL("files"));xvalue* Extras=xrtValueArray();bool Ok=Doc&&Extras&&xrtValueType(Inventory)==XVALUE_ARRAY&&xrtValueCount(Inventory)<=128;size_t Total=0;
    for(size_t i=0;Ok&&i<xrtValueCount(Inventory);i++)Ok=EcoNativeExportFile(Extras,Root,MdoPackageText(xrtValueArrayGet(Inventory,i),"path",220),&Total);
    xvalue* Descriptor=Plugin?xrtValueObject():xrtValueClone(Doc);
    if(Plugin){const xvalue* F=xrtValueObjectGet(Doc,XRT_STR_LITERAL("frontend"));if(F)Ok=Ok&&xrtValueObjectSet(Descriptor,XRT_STR_LITERAL("frontend"),F);}
    else {const char* Keys[]={"id","root","revision","enabled","files"};for(size_t i=0;i<5;i++)xrtValueObjectRemove(Descriptor,xrtStrView(Keys[i]));}
    char* Content=Descriptor?xrtJsonStringify(Descriptor,false,NULL):NULL;
    Ok=Ok&&Content&&MdoApiValueSetString(Resource,"kind",Kind)&&MdoApiValueSetString(Resource,"id",Id)&&MdoApiValueSetString(Resource,"content",Content)&&xrtValueObjectSet(Resource,XRT_STR_LITERAL("files"),Extras);
    xrtFree(Content);xrtValueRelease(Descriptor);xrtValueRelease(Extras);xrtValueRelease(Doc);return Ok;
}
static bool EcoNativeExportTree(cstr Root,cstr Prefix,xvalue* Extras,size_t* Total,size_t Depth)
{
    if(Depth>8)return false;
    char Directory[512];snprintf(Directory,sizeof(Directory),"%s%s%s",Root,Prefix[0]?"/":"",Prefix);
    xdir Dir=MdoHomeOpenDirectory(Directory,XDIR_STAT);if(!Dir)return false;xdirentry E;xdirnext Next;bool Ok=true;
    while(Ok&&(Next=xrtDirNext(Dir,&E))==XDIR_NEXT_ITEM){
        if(E.Name.Size>64||memchr(E.Name.Data,0,E.Name.Size)){Ok=false;break;}char Name[65],Relative[256];memcpy(Name,E.Name.Data,E.Name.Size);Name[E.Name.Size]=0;
        if(!Prefix[0]&&!strcmp(Name,"SKILL.md"))continue;
        int N=snprintf(Relative,sizeof(Relative),"%s%s%s",Prefix,Prefix[0]?"/":"",Name);if(N<0||(size_t)N>=sizeof(Relative)||!MdoPackagePath(Relative)){Ok=false;break;}
        if(E.Info.Type==XFILE_TYPE_DIRECTORY)Ok=EcoNativeExportTree(Root,Relative,Extras,Total,Depth+1);
        else if(E.Info.Type==XFILE_TYPE_FILE)Ok=xrtValueCount(Extras)<128&&EcoNativeExportFile(Extras,Root,Relative,Total);
        else Ok=false;
    }xrtDirClose(Dir);return Ok&&Next!=XDIR_NEXT_ERROR;
}
