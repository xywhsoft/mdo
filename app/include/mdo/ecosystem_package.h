#ifndef MDO_ECOSYSTEM_PACKAGE_H
#define MDO_ECOSYSTEM_PACKAGE_H

/* Portable source package. Shared by the native client and website; neither
 * validation nor preview compiles C, connects MCP, or executes resource files. */
#include <stdio.h>
#include <string.h>
#define MDO_PACKAGE_LIMIT (100u * 1024u * 1024u)
#define MDO_PACKAGE_V1_LIMIT (1024u * 1024u)
#define MDO_PACKAGE_LARGE_MIN_BUILD 50000009u
#define MDO_PACKAGE_FORMAT "mdo.extension.v1"
#define MDO_PACKAGE_FORMAT_V2 "mdo.extension.v2"
#define MDO_PACKAGE_V2_MIN_BUILD 30000126u
static bool MdoPackageNumber(const xvalue* object,const char* key,int64* result)
{
    const xvalue* value=xrtValueObjectGet(object,xrtStrView(key));uint64 n;
    if(xrtValueType(value)==XVALUE_INT)return xrtValueGetInt(value,result);
    if(xrtValueType(value)!=XVALUE_UINT||!xrtValueGetUInt(value,&n)||n>INT64_MAX)return false;
    *result=(int64)n;return true;
}
static const char* MdoPackageText(const xvalue* object, const char* key, size_t maximum)
{
    xstrview s;
    if (!xrtValueGetString(xrtValueObjectGet(object,xrtStrView(key)),&s) ||
        s.Size>maximum || memchr(s.Data,0,s.Size) || !xrtUtf8Valid(s,NULL)) return NULL;
    return s.Data;
}
static bool MdoPackageId(const char* id)
{
    size_t n=id?strlen(id):0;
    if(!n||n>64||id[0]=='.'||id[n-1]=='.'||strstr(id,".."))return false;
    for(size_t i=0;i<n;i++)if(!((id[i]>='a'&&id[i]<='z')||(id[i]>='0'&&id[i]<='9')||strchr("._-",id[i])))return false;
    char base[65];size_t b=strcspn(id,".");memcpy(base,id,b);base[b]=0;
    return strcmp(base,"con")&&strcmp(base,"prn")&&strcmp(base,"aux")&&strcmp(base,"nul")&&
        !(b==4&&(!strncmp(base,"com",3)||!strncmp(base,"lpt",3))&&base[3]>='1'&&base[3]<='9');
}
static bool MdoPackageKind(const char* kind)
{
    return kind&&(!strcmp(kind,"agents")||!strcmp(kind,"subagents")||!strcmp(kind,"tools")||
        !strcmp(kind,"skills")||!strcmp(kind,"mcp")||!strcmp(kind,"commands")||
        !strcmp(kind,"c-agents")||!strcmp(kind,"c-subagents")||!strcmp(kind,"modules")||
        !strcmp(kind,"toolpacks")||!strcmp(kind,"plugins"));
}
static const char* MdoPackageSystem(const char* kind)
{return kind&&!strcmp(kind,"c-agents")?"agents":kind&&!strcmp(kind,"c-subagents")?"subagents":kind;}
static bool MdoPackageV2(const xvalue* package)
{const char* f=MdoPackageText(package,"format",32);return f&&!strcmp(f,MDO_PACKAGE_FORMAT_V2);}
/* Public versions use three bounded numeric components, compared numerically.
 * Legacy v1 version strings keep their historical parsing contract. */
static bool MdoPackageVersion(const char* version,uint32 parts[3])
{
    if(!version||!*version)return false;
    const char* p=version;
    for(size_t i=0;i<3;i++){
        uint64 n=0;const char* first=p;
        while(*p>='0'&&*p<='9'){n=n*10u+(unsigned)(*p++-'0');if(n>UINT32_MAX)return false;}
        if(p==first||(p-first>1&&*first=='0'))return false;
        if(parts)parts[i]=(uint32)n;
        if(i<2){if(*p++!='.')return false;}else if(*p)return false;
    }return true;
}
static int MdoPackageVersionCompare(const char* a,const char* b)
{
    uint32 x[3],y[3];if(!MdoPackageVersion(a,x)||!MdoPackageVersion(b,y))return strcmp(a?a:"",b?b:"");
    for(size_t i=0;i<3;i++){if(x[i]!=y[i])return x[i]<y[i]?-1:1;}
    return 0;
}
static bool MdoPackagePath(const char* path)
{
    if(!path||!path[0]||strlen(path)>220||strchr(path,'\\')||strchr(path,':')||path[0]=='/')return false;
    const char* p=path;
    for(;;){const char* slash=strchr(p,'/');size_t n=slash?(size_t)(slash-p):strlen(p);char component[221];
        if(!n||n>64||p[0]=='.'||p[n-1]=='.'||p[n-1]==' ')return false;
        memcpy(component,p,n);component[n]=0;
        /* Case-insensitive filesystem aliases and control bytes are rejected. */
        for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)component[i];if(c<32||c==127||strchr("<>\"|?*",c))return false;}
        char lower[221];for(size_t i=0;i<=n;i++)lower[i]=(component[i]>='A'&&component[i]<='Z')?component[i]+32:component[i];
        size_t b=strcspn(lower,".");lower[b]=0;
        if(!strcmp(lower,"con")||!strcmp(lower,"prn")||!strcmp(lower,"aux")||!strcmp(lower,"nul")||
           (b==4&&(!strncmp(lower,"com",3)||!strncmp(lower,"lpt",3))&&lower[3]>='1'&&lower[3]<='9'))return false;
        if(!slash)break;
        p=slash+1;
    }return true;
}
static bool MdoPackageSamePath(const char* a,const char* b)
{
    for(;*a&&*b;a++,b++){unsigned char x=*a,y=*b;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}return *a==*b;
}
static bool MdoPackageValidate(const xvalue* package,char* error,size_t capacity)
{
    const char* reason="Invalid extension package";
    const xvalue* m=xrtValueObjectGet(package,XRT_STR_LITERAL("manifest"));
    const xvalue* resources=xrtValueObjectGet(package,XRT_STR_LITERAL("resources"));
    const char* format=MdoPackageText(package,"format",32),*slug=MdoPackageText(m,"slug",64);
    const char* name=MdoPackageText(m,"name",100),*version=MdoPackageText(m,"version",32);
    const char* description=MdoPackageText(m,"description",600),*readme=MdoPackageText(m,"readme",32768),*license=MdoPackageText(m,"license",80);
    if(xrtValueObjectGet(m,XRT_STR_LITERAL("changelog"))&&!MdoPackageText(m,"changelog",8192))goto bad;
    bool v2=MdoPackageV2(package);size_t sourceLimit=v2?64u*1024u*1024u:524288u;
    if(!format||(strcmp(format,MDO_PACKAGE_FORMAT)&&!v2)||!MdoPackageId(slug)||!name||!name[0]||!version||!version[0]||
       !description||!description[0]||!readme||!readme[0]||!license||!license[0]||
       xrtValueType(resources)!=XVALUE_ARRAY||!xrtValueCount(resources)||xrtValueCount(resources)>16)goto bad;
    for(const char* p=version;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||strchr(".-_",*p)))goto bad;
    if(v2){int64 build=0;reason="v2 packages need x.y.z and a compatible minimum build";
        if(!MdoPackageVersion(version,NULL)||!MdoPackageNumber(m,"min_build_id",&build)||build<MDO_PACKAGE_V2_MIN_BUILD||build>99999999)goto bad;}
    const xvalue* platforms=xrtValueObjectGet(m,XRT_STR_LITERAL("platforms"));
    if(xrtValueType(platforms)!=XVALUE_ARRAY||!xrtValueCount(platforms)||xrtValueCount(platforms)>8)goto bad;
    for(size_t i=0;i<xrtValueCount(platforms);i++){xstrview p;if(!xrtValueGetString(xrtValueArrayGet(platforms,i),&p)||
        !(xrtStrEqual(p,XRT_STR_LITERAL("windows-x86_64"))||xrtStrEqual(p,XRT_STR_LITERAL("android-arm64-v8a"))||
          xrtStrEqual(p,XRT_STR_LITERAL("linux-x86_64"))||xrtStrEqual(p,XRT_STR_LITERAL("linux-arm64"))||
          xrtStrEqual(p,XRT_STR_LITERAL("linux-x86_64-glibc"))||xrtStrEqual(p,XRT_STR_LITERAL("linux-x86_64-musl"))||
          xrtStrEqual(p,XRT_STR_LITERAL("macos-arm64"))))goto bad;}
    size_t total=0;
    for(size_t i=0;i<xrtValueCount(resources);i++){
        const xvalue* r=xrtValueArrayGet(resources,i);const char* kind=MdoPackageText(r,"kind",16),*id=MdoPackageText(r,"id",64),*content=MdoPackageText(r,"content",131072);
        reason="Invalid resource kind, ID or content";
        if(!MdoPackageKind(kind)||!MdoPackageId(id)||!content||!content[0]||(!strcmp(kind,"agents")&&!strcmp(id,"default")))goto bad;
        bool module=!strcmp(kind,"modules"),plugin=!strcmp(kind,"plugins"),toolpack=!strcmp(kind,"toolpacks");
        if((module||plugin||toolpack)&&!v2){reason="This resource needs the v2 package format";goto bad;}
        for(size_t j=0;j<i;j++){const xvalue* q=xrtValueArrayGet(resources,j);if(!strcmp(kind,MdoPackageText(q,"kind",16))&&!strcmp(id,MdoPackageText(q,"id",64))){reason="Duplicate resource ID";goto bad;}}
        total+=strlen(content);if(total>sourceLimit){reason="Package decoded content exceeds its limit";goto bad;}
        const xvalue* files=xrtValueObjectGet(r,XRT_STR_LITERAL("files"));
        if(files){reason="This resource cannot contain attached files";if((strcmp(kind,"skills")&&!module&&!plugin&&!toolpack)||xrtValueType(files)!=XVALUE_ARRAY||xrtValueCount(files)>128)goto bad;
            for(size_t j=0;j<xrtValueCount(files);j++){const xvalue* f=xrtValueArrayGet(files,j);const char* path=MdoPackageText(f,"path",220),*base64=MdoPackageText(f,"base64",v2?90u*1024u*1024u:180000u);
                reason="Invalid attached file path or size";if(!MdoPackagePath(path)||(!strcmp(kind,"skills")&&MdoPackageSamePath(path,"SKILL.md"))||!base64)goto bad;
                size_t prefix=plugin?8u+strlen(id)+11u+64u+7u:module?14u+strlen(id)+1u:toolpack?106u:8u+strlen(id);
                if(prefix+strlen(path)>220u){reason="Installed file path exceeds the portable limit";goto bad;}
                for(size_t k=0;k<j;k++)if(MdoPackageSamePath(path,MdoPackageText(xrtValueArrayGet(files,k),"path",220))){reason="Duplicate file path";goto bad;}
                size_t size=0;void* bytes=xrtBase64DecodeNew(base64,strlen(base64),&size,NULL);bool ok=bytes&&size<=(v2?64u*1024u*1024u:131072u);xrtFree(bytes);if(!ok)goto bad;
                total+=size;if(total>sourceLimit){reason="Package decoded content exceeds its limit";goto bad;}
            }
        }
        if(plugin||toolpack){
            xvalue* doc=xrtJsonParse(xrtStrView(content));bool valid=xrtValueType(doc)==XVALUE_OBJECT;
            if(plugin){const char* frontend=MdoPackageText(doc,"frontend",220);
                valid=valid&&files&&xrtValueCount(files)&&(!frontend||!frontend[0]||(MdoPackagePath(frontend)&&!strncmp(frontend,"web/",4)));
                if(valid&&frontend&&frontend[0]){bool found=false;for(size_t j=0;j<xrtValueCount(files);j++)if(!strcmp(frontend,MdoPackageText(xrtValueArrayGet(files,j),"path",220)))found=true;valid=found;}
            }else{
                const xvalue* programs=xrtValueObjectGet(doc,XRT_STR_LITERAL("programs"));const char* prompt=MdoPackageText(doc,"prompt",32768);
                valid=valid&&prompt&&xrtValueType(programs)==XVALUE_ARRAY&&xrtValueCount(programs)>0&&xrtValueCount(programs)<=32;
                for(size_t j=0;valid&&j<xrtValueCount(programs);j++){
                    const xvalue* program=xrtValueArrayGet(programs,j);const char* name=MdoPackageText(program,"name",64),*path=MdoPackageText(program,"path",100);
                    valid=MdoPackageId(name)&&MdoPackagePath(path)&&MdoPackageText(program,"description",600);
                    for(size_t k=0;valid&&k<j;k++)if(!strcmp(name,MdoPackageText(xrtValueArrayGet(programs,k),"name",64)))valid=false;
                    bool found=false;for(size_t k=0;valid&&k<xrtValueCount(files);k++)if(!strcmp(path,MdoPackageText(xrtValueArrayGet(files,k),"path",220)))found=true;
                    if(!found)valid=false;
                }
                const xvalue* archive=xrtValueObjectGet(doc,XRT_STR_LITERAL("archive"));
                if(archive)valid=false; /* Every reviewed byte belongs to this immutable portable package. */
            }
            xrtValueRelease(doc);if(!valid){reason=plugin?"Invalid mdo plugin manifest or frontend file":"Invalid command-line toolpack manifest";goto bad;}
        }
        /* Portable MCP exports have credential placeholders, never vault or
         * local file references, inline tokens, or machine-specific paths. */
        if(!strcmp(kind,"mcp")){
            xvalue* doc=xrtJsonParse(xrtStrView(content));const xvalue* transport=xrtValueObjectGet(doc,XRT_STR_LITERAL("transport"));
            bool ok=doc&&xrtValueType(transport)==XVALUE_OBJECT;const char* keys[]={"environment","headers"};
            for(size_t k=0;ok&&k<2;k++){const xvalue* list=xrtValueObjectGet(transport,xrtStrView(keys[k]));
                if(list&&xrtValueType(list)!=XVALUE_ARRAY){ok=false;break;}
                for(size_t j=0;ok&&j<xrtValueCount(list);j++){const xvalue* e=xrtValueArrayGet(list,j);const char* ref=MdoPackageText(e,"secret_ref",256);
                    ok=ref&&!strncmp(ref,"input:",6)&&strlen(ref)>6;}}
            const char* cwd=MdoPackageText(transport,"working_directory",1024);if(cwd&&cwd[0])ok=false;
            xrtValueRelease(doc);if(!ok){reason="MCP export must use input: credential placeholders and no local working directory";goto bad;}
        }
    }
    {int64 largeBuild=0;size_t size=0;char* json=xrtJsonStringify(package,false,&size);bool bounded=json&&size<=(v2?MDO_PACKAGE_LIMIT:MDO_PACKAGE_V1_LIMIT);xrtFree(json);
        if(v2&&size>6u*1024u*1024u&&(!MdoPackageNumber(m,"min_build_id",&largeBuild)||largeBuild<MDO_PACKAGE_LARGE_MIN_BUILD)){reason="Large packages require a newer client build";goto bad;}
        if(!bounded){reason="Package JSON exceeds its format limit";goto bad;}}
    return true;
bad:if(error&&capacity)snprintf(error,capacity,"%s",reason);return false;
}
#endif
