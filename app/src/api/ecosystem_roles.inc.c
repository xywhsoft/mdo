#include "../../include/mdo/models.h"
/* Role resources are an atomic profile + optional C program + owned documents.
 * No rendered context, memory, sessions or machine credentials are exported. */
static bool EcoRoleAvailable(cstr Kind,cstr Id)
{
    bool Found=false;
    if(!strcmp(Kind,"models")){MdoModelCatalog* C=MdoModelCatalogSnapshot();MdoModelInfo I={0};I.Size=sizeof(I);Found=MdoModelCatalogModelFind(C,Id,&I);MdoModelCatalogRelease(C);}
    else if(!strcmp(Kind,"skills")){MdoSkillCatalog* C=MdoSkillCatalogSnapshot();MdoSkillInfo I={0};I.Size=sizeof(I);Found=MdoSkillCatalogFind(C,Id,&I);MdoSkillCatalogRelease(C);}
    else if(!strcmp(Kind,"mcp")){MdoMcpCatalog* C=MdoMcpCatalogSnapshot();MdoMcpServerInfo I={0};I.Size=sizeof(I);Found=MdoMcpCatalogFind(C,Id,&I)&&I.Enabled;MdoMcpCatalogRelease(C);}
    else if(MdoPackageRoleKind(Kind)){char Full[96];snprintf(Full,sizeof(Full),"%s.%s",!strcmp(Kind,"agents")?"agent":"subagent",Id);
        MdoModuleCatalog* C=MdoModuleCatalogSnapshot();MdoModuleAgentInfo I={0};I.Size=sizeof(I);Found=MdoModuleCatalogAgentFind(C,Full,&I);MdoModuleCatalogRelease(C);}
    else if(!strcmp(Kind,"roles")){
        MdoModuleCatalog* C=MdoModuleCatalogSnapshot();size_t N=strlen(Id);bool Wild=N&&Id[N-1]=='*';
        for(size_t J=0;!Found&&J<MdoModuleCatalogAgentCount(C);J++){
            MdoModuleAgentInfo I={0};I.Size=sizeof(I);
            if(MdoModuleCatalogAgentAt(C,J,&I)&&(I.Flags&MDO_AGENT_SUBAGENT))Found=Wild?!strncmp(Id,I.Id,N-1):!strcmp(Id,I.Id);
        }MdoModuleCatalogRelease(C);
    }
    /* Optional knowledge/CLI selectors are resolved by the task's scoped
     * catalog. A global install preview must not report them as absent. */
    return Found;
}
static xvalue* EcoRoleDependencies(const xvalue* Package,bool* Ready)
{
    xvalue* Out=xrtValueArray();*Ready=true;bool Ok=Out!=NULL;
    const xvalue* Rs=xrtValueObjectGet(Package,XRT_STR_LITERAL("resources"));
    for(size_t I=0;Ok&&I<xrtValueCount(Rs);I++){
        const xvalue* R=xrtValueArrayGet(Rs,I);cstr Kind=MdoPackageText(R,"kind",16);if(!MdoPackageRoleKind(Kind))continue;
        xvalue* D=MdoPackageRoleDocument(MdoPackageText(R,"content",131072),NULL,0);
        xvalue* Deps=D?MdoPackageRoleDependencies(D,Kind):NULL;Ok=Deps!=NULL;
        for(size_t J=0;Ok&&J<xrtValueCount(Deps);J++){
            xvalue* Dep=(xvalue*)xrtValueArrayGet(Deps,J);cstr K=MdoPackageText(Dep,"kind",32),Id=MdoPackageText(Dep,"id",256);
            bool Found=EcoRoleAvailable(K,Id);
            /* Legacy combined packages can provide their declared dependency. */
            for(size_t N=0;!Found&&N<xrtValueCount(Rs);N++){
                const xvalue* Other=xrtValueArrayGet(Rs,N);Found=!strcmp(K,MdoPackageText(Other,"kind",16))&&!strcmp(Id,MdoPackageText(Other,"id",64));
            }
            if(!Found&&MdoAgentConfigBool(Dep,"required",false))*Ready=false;
            bool Checked=strcmp(K,"knowledge")&&strcmp(K,"external-tools");
            Ok=MdoApiValueSetBool(Dep,"checked",Checked)&&MdoApiValueSetBool(Dep,"available",Found)&&xrtValueArrayAppend(Out,Dep);
        }
        xrtValueRelease(Deps);xrtValueRelease(D);
    }
    if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
static bool EcoRoleFiles(xvalue* Files,const xvalue* Resource,char Error[1024])
{
    cstr Kind=MdoPackageText(Resource,"kind",16),Id=MdoPackageText(Resource,"id",64),Text=MdoPackageText(Resource,"content",131072);char Path[256];
    snprintf(Path,sizeof(Path),"%s/%s.md",Kind,Id);
    bool Ok=EcoLocalAppend(Files,Path,Text,strlen(Text));const xvalue* Extras=xrtValueObjectGet(Resource,XRT_STR_LITERAL("files"));
    for(size_t I=0;Ok&&I<xrtValueCount(Extras);I++){
        const xvalue* F=xrtValueArrayGet(Extras,I);cstr Name=MdoPackageText(F,"path",220),B64=MdoPackageText(F,"base64",90u*1024u*1024u);size_t N=0;
        void* Bytes=xrtBase64DecodeNew(B64,strlen(B64),&N,NULL);
        int W=!strcmp(Name,"program.c")?snprintf(Path,sizeof(Path),"modules/%s/%s.c",Kind,Id):snprintf(Path,sizeof(Path),"%s/%s/%s",Kind,Id,Name);
        Ok=Bytes&&W>0&&(size_t)W<sizeof(Path)&&strlen(Path)<=220&&EcoLocalAppend(Files,Path,Bytes,N);xrtFree(Bytes);
    }
    if(!Ok)snprintf(Error,1024,"Role files have conflicting or overlong installation paths");return Ok;
}
static bool EcoRolePublished(const xvalue* Package,char Error[1024])
{
    const xvalue* Rs=xrtValueObjectGet(Package,XRT_STR_LITERAL("resources"));bool Ok=true;
    for(size_t I=0;Ok&&I<xrtValueCount(Rs);I++){
        const xvalue* R=xrtValueArrayGet(Rs,I);cstr K=MdoPackageText(R,"kind",16),Id=MdoPackageText(R,"id",64);if(!MdoPackageRoleKind(K))continue;
        bool Enabled=true;Ok=MdoExtensionEnabled(K,Id,&Enabled);
        /* Disabled profiles are intentionally absent from the published roster,
         * but their file references and inheritance must still be valid. */
        MdoAgentFile F={0};Ok=Ok&&MdoAgentFileParse(Id,MdoPackageText(R,"content",131072),!strcmp(K,"agents"),&F,Error,1024);MdoAgentFileUnit(&F);
        if(Ok&&Enabled&&!EcoRoleAvailable(K,Id)){snprintf(Error,1024,"Installed role was not registered: %s/%s",K,Id);Ok=false;}
    }return Ok;
}
