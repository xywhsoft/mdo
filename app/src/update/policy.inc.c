/* Only fully validated manifests replace a known policy. The cache is local
 * continuity across offline restarts, not a substitute for HTTPS authenticity. */
static bool MdoUpdatePolicyParse(xvalue* Root,MdoUpdateStatus* Status)
{
    char Platform[32], Download[160], Expected[160]; uint64 Size = 0; int64 Signed;
    xvalue* Number = xrtValueObjectGet(Root,XRT_STR_LITERAL("size"));
    xvalue* Required = xrtValueObjectGet(Root,XRT_STR_LITERAL("required"));
    bool Force = false;
    bool Ok = xrtValueGetUInt(Number,&Size) ||
        (xrtValueGetInt(Number,&Signed) && Signed > 0 && (Size=(uint64)Signed)!=0);
    Ok = Ok && Root && xrtValueType(Root) == XVALUE_OBJECT && Size && Size <= MDO_UPDATE_LIMIT &&
        (!Required || xrtValueGetBool(Required,&Force)) &&
        MdoUpdateText(Root,"platform",Platform,sizeof(Platform)) && !strcmp(Platform,Status->Platform) &&
        MdoUpdateText(Root,"sha256",Status->Hash,sizeof(Status->Hash)) && MdoUpdateHashValid(Status->Hash) &&
        MdoUpdateText(Root,"url",Download,sizeof(Download)) &&
        MdoUpdateText(Root,"notes",Status->Notes,sizeof(Status->Notes));
    if (!Ok) return false;
    snprintf(Expected,sizeof(Expected),"/update/download/%s/%s",Status->Platform,Status->Hash);
    if (strcmp(Expected,Download)) {
        snprintf(Expected,sizeof(Expected),"/mdo/blob/%s",Status->Hash);
        if(strcmp(Expected,Download))return false;
    }
    snprintf(Status->DownloadPath,sizeof(Status->DownloadPath),"%s",Download);
    Status->BuildId=0;
    xvalue* Build=xrtValueObjectGet(Root,XRT_STR_LITERAL("build_id"));
    if(Build) {if(!xrtValueGetUInt(Build,&Status->BuildId)) {int64 Id=0; if(!xrtValueGetInt(Build,&Id)||Id<10000000)return false; Status->BuildId=(uint64)Id;} if(Status->BuildId<10000000||Status->BuildId>99999999)return false;}
    char Edition[16];
    if(xrtValueObjectGet(Root,XRT_STR_LITERAL("edition"))) {
        if(!MdoUpdateText(Root,"edition",Edition,sizeof(Edition)) ||
           (!strcmp(Status->Platform,"windows-x86_64")?strcmp(Edition,"desktop"):(strcmp(Edition,"lite")&&strcmp(Edition,"full"))))return false;
        snprintf(Status->Edition,sizeof(Status->Edition),"%s",Edition);
    }
    Status->Required = Force; Status->Bytes = Size; return true;
}
static bool MdoUpdatePolicySave(const MdoUpdateStatus* Status)
{
    bool Exists = false; xfileinfo Info;
    if (!MdoHomeExternalStat("data/update/policy.json",&Exists,&Info)) return false;
    /* No filesystem writes for ordinary startup checks. Once cached, persist
     * explicit revocations as well, so stale required=true cannot reappear. */
    if (!Status->Required && !Exists) return true;
    xvalue* Root = xrtValueObject(); size_t Size = 0; char Url[128]; str Json = NULL;
    snprintf(Url,sizeof(Url),"%s",Status->DownloadPath);
    bool Ok = Root &&
        xrtValueObjectSetNew(Root,XRT_STR_LITERAL("platform"),xrtValueString(xrtStrView(Status->Platform))) &&
        xrtValueObjectSetNew(Root,XRT_STR_LITERAL("edition"),xrtValueString(xrtStrView(Status->Edition))) &&
        xrtValueObjectSetNew(Root,XRT_STR_LITERAL("sha256"),xrtValueString(xrtStrView(Status->Hash))) &&
        xrtValueObjectSetNew(Root,XRT_STR_LITERAL("url"),xrtValueString(xrtStrView(Url))) &&
        xrtValueObjectSetNew(Root,XRT_STR_LITERAL("notes"),xrtValueString(xrtStrView(Status->Notes))) &&
        xrtValueObjectSetNew(Root,XRT_STR_LITERAL("size"),xrtValueUInt(Status->Bytes)) &&
        (!Status->BuildId || xrtValueObjectSetNew(Root,XRT_STR_LITERAL("build_id"),xrtValueUInt(Status->BuildId))) &&
        xrtValueObjectSetNew(Root,XRT_STR_LITERAL("required"),xrtValueBool(Status->Required));
    if (Ok) Json = xrtJsonStringify(Root,false,&Size);
    Ok = Json && MdoHomeAtomicWrite("data/update/policy.json",Json,Size,false);
    xrtFree(Json); xrtValueRelease(Root); return Ok;
}
static void MdoUpdatePolicyLoad(MdoUpdateStatus* Status)
{
    xfile File = MdoHomeOpenRead("data/update/policy.json");
    if (!File) return;
    char Text[8192]; uint64 Size = 0; size_t Got = 0; xvalue* Root = NULL;
    MdoUpdateStatus Next = *Status; xjsonreadconfig Config;
    xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = sizeof(Text);
    Config.MaxDepth = 4; Config.MaxValues = 64;
    if (xrtFileSize(File,&Size) && Size && Size <= sizeof(Text) &&
        xrtRead(File,Text,(size_t)Size,&Got) && Got == Size)
        Root = xrtJsonRead(xrtStrViewN(Text,Got),&Config);
    if (MdoUpdatePolicyParse(Root,&Next)) {
        bool HasHash = MdoUpdateFileHash(g_MdoUpdate.Source,Next.LocalHash,
            !strcmp(Next.Platform,"windows-x86_64"));
        Next.Available = (!HasHash || strcmp(Next.LocalHash,Next.Hash) != 0) && (!Next.BuildId || Next.BuildId>MdoBuildId());
        /* Reuse a downloaded package only after independently checking disk. */
        str Path = MdoHomeExternalPath(!strcmp(Next.Platform,"windows-x86_64") ?
            "data/update/new.exe" : "data/update/new.apk"); char Hash[65];
        Next.Ready = Next.Available && Path && MdoUpdateFileHash(Path,Hash,false) && !strcmp(Hash,Next.Hash);
        xrtFree(Path); *Status = Next;
        MdoUpdateComplete(Status,Next.Available ? (Next.Ready ? "ready" : "available") : "current","");
    } else MdoUpdateComplete(Status,"error","Invalid cached update policy; check again online");
    xrtValueRelease(Root); xrtClose(File);
}
