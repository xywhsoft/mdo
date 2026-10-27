/* The base service is a recovery loader. Core overrides must be compiled from
 * the verified view before any plugin Register/activate callback is entered.
 * The child TCC remains owned by this XS service until all requests retire. */
typedef void (*MdoPluginBootInit)(XS_HostInfo*);
typedef bool (*MdoPluginBootSwap)(XS_HostInfo*,xvalue**);
typedef XS_RequestResult (*MdoPluginBootRequest)(XS_HttpReq*);
typedef bool (*MdoPluginBootSnapshot)(MdoBootstrapSnapshot*);
typedef bool (*MdoPluginBootRecovery)(void);
static struct {
    bool Tried;
    TCCState* Tcc;
    MdoPluginBootInit Init,Unit;
    MdoPluginBootSwap Swap;
    MdoPluginBootRequest Request;
    MdoPluginBootSnapshot Snapshot;
    MdoPluginBootRecovery Recovery;
} MdoPluginBoot;

static void MdoPluginBootCompilerError(void* Data,const char* Message){
    /* Do not retain source excerpts or literals from a submitted plugin. */
    (void)Data;(void)Message;
}
static bool MdoPluginBootstrapTry(XS_HostInfo* Host){
#ifdef MDO_PLUGIN_CORE_COMPILED
    (void)Host;return false;
#else
    if(MdoPluginBoot.Tried)return MdoPluginBoot.Tcc!=NULL;
    /* A reload candidate waits for the previous exclusive Home owner first. */
    char DirectoryError[512];if(!MdoHomeInit()||!MdoExtensionsInit()||!MdoExtensionsRecover(DirectoryError))return false;
    MdoPluginBoot.Tried=true;
    if(!MdoApplicationPluginsInit()||!MdoApplicationPluginCoreRequired())return false;
    xs_tcc_config Config;xsTccConfigInit(&Config);
    Config.ApplicationVfs=MdoHomeApplicationVfs();
    Config.ApplicationRoot=MdoHomeApplicationRoot();Config.Flags=0;
    xerror* Error=NULL;TCCState* Tcc=xsCreateTCCEx(&Config,&Error);xrtErrorFree(Error);
    size_t Size=0;bytes Source=xrtVfsReadAllLimit(Config.ApplicationVfs,"/app/generated/mdo_unity.c",4194304,&Size);
    bool Ok=Tcc&&Source&&!memchr(Source,0,Size);
    if(Tcc){tcc_set_error_func(Tcc,NULL,MdoPluginBootCompilerError);tcc_define_symbol(Tcc,"MDO_PLUGIN_CORE_COMPILED","1");}
    Ok=Ok&&tcc_add_quote_include_path(Tcc,"/app/generated")>=0&&
        tcc_set_options(Tcc,"-std=c11")>=0&&tcc_compile_string(Tcc,(cstr)Source)>=0&&tcc_relocate(Tcc)>=0;
    xrtFree(Source);
#define MDO_PLUGIN_BOOT_SYMBOL(Member,Type,Name) do { union {void* Pointer;Type Function;} U;U.Pointer=tcc_get_symbol(Tcc,Name);MdoPluginBoot.Member=U.Function; }while(0)
    if(Ok){
        MDO_PLUGIN_BOOT_SYMBOL(Init,MdoPluginBootInit,"ServiceInit");
        MDO_PLUGIN_BOOT_SYMBOL(Unit,MdoPluginBootInit,"ServiceUnit");
        MDO_PLUGIN_BOOT_SYMBOL(Swap,MdoPluginBootSwap,"ServiceSwap");
        MDO_PLUGIN_BOOT_SYMBOL(Request,MdoPluginBootRequest,"RequestProc");
        MDO_PLUGIN_BOOT_SYMBOL(Snapshot,MdoPluginBootSnapshot,"MdoBootstrapGetSnapshot");
        MDO_PLUGIN_BOOT_SYMBOL(Recovery,MdoPluginBootRecovery,"MdoApplicationPluginsRecovery");
        Ok=MdoPluginBoot.Init&&MdoPluginBoot.Unit&&MdoPluginBoot.Swap&&MdoPluginBoot.Request&&MdoPluginBoot.Snapshot&&MdoPluginBoot.Recovery;
    }
#undef MDO_PLUGIN_BOOT_SYMBOL
    if(Ok){
        /* Candidate compilation has finished. Release the preparatory lease and
         * provider before the actual application takes its own lease and view. */
        MdoApplicationPluginsUnit();MdoHomeUnit();
        MdoPluginBoot.Tcc=Tcc;MdoPluginBoot.Init(Host);
        MdoBootstrapSnapshot Snapshot={0};Snapshot.Size=sizeof(Snapshot);
        if(MdoPluginBoot.Snapshot(&Snapshot)&&Snapshot.Ready&&!MdoPluginBoot.Recovery())return true;
        MdoPluginBoot.Unit(Host);MdoPluginBoot.Tcc=NULL;
        /* No plugin entry is retried in the base recovery application. */
        if(!MdoHomeInit()||!MdoApplicationPluginsInit()){xsDestroyTCC(Tcc);return false;}
    }
    if(Tcc)xsDestroyTCC(Tcc);
    (void)MdoApplicationPluginsRecover();
    fprintf(stderr,"[mdo] core plugin candidate rejected; starting the base recovery application\n");
    return false;
#endif
}
