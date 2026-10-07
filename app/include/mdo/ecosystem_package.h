#ifndef MDO_ECOSYSTEM_PACKAGE_H
#define MDO_ECOSYSTEM_PACKAGE_H

/* Portable source package. Shared by the native client and website; neither
 * validation nor preview compiles C, connects MCP, or executes resource files. */
#include <stdio.h>
#include <string.h>
#define MDO_PACKAGE_LIMIT (1024u * 1024u)
#define MDO_PACKAGE_FORMAT "mdo.extension.v1"
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
        !strcmp(kind,"c-agents")||!strcmp(kind,"c-subagents"));
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
        if(!slash)break;p=slash+1;
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
    if(!format||strcmp(format,MDO_PACKAGE_FORMAT)||!MdoPackageId(slug)||!name||!name[0]||!version||!version[0]||
       !description||!description[0]||!readme||!readme[0]||!license||!license[0]||
       xrtValueType(resources)!=XVALUE_ARRAY||!xrtValueCount(resources)||xrtValueCount(resources)>16)goto bad;
    for(const char* p=version;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||strchr(".-_",*p)))goto bad;
    const xvalue* platforms=xrtValueObjectGet(m,XRT_STR_LITERAL("platforms"));
    if(xrtValueType(platforms)!=XVALUE_ARRAY||!xrtValueCount(platforms)||xrtValueCount(platforms)>4)goto bad;
    for(size_t i=0;i<xrtValueCount(platforms);i++){xstrview p;if(!xrtValueGetString(xrtValueArrayGet(platforms,i),&p)||
        !(xrtStrEqual(p,XRT_STR_LITERAL("windows-x86_64"))||xrtStrEqual(p,XRT_STR_LITERAL("android-arm64-v8a"))||
          xrtStrEqual(p,XRT_STR_LITERAL("linux-x86_64"))||xrtStrEqual(p,XRT_STR_LITERAL("macos-arm64"))))goto bad;}
    size_t total=0;
    for(size_t i=0;i<xrtValueCount(resources);i++){
        const xvalue* r=xrtValueArrayGet(resources,i);const char* kind=MdoPackageText(r,"kind",16),*id=MdoPackageText(r,"id",64),*content=MdoPackageText(r,"content",131072);
        reason="Invalid resource kind, ID or content";
        if(!MdoPackageKind(kind)||!MdoPackageId(id)||!content||!content[0]||(!strcmp(kind,"agents")&&!strcmp(id,"default")))goto bad;
        for(size_t j=0;j<i;j++){const xvalue* q=xrtValueArrayGet(resources,j);if(!strcmp(kind,MdoPackageText(q,"kind",16))&&!strcmp(id,MdoPackageText(q,"id",64))){reason="Duplicate resource ID";goto bad;}}
        total+=strlen(content);if(total>524288){reason="Package source exceeds 512 KiB";goto bad;}
        const xvalue* files=xrtValueObjectGet(r,XRT_STR_LITERAL("files"));
        if(files){reason="Only Skills may contain extra files";if(strcmp(kind,"skills")||xrtValueType(files)!=XVALUE_ARRAY||xrtValueCount(files)>128)goto bad;
            for(size_t j=0;j<xrtValueCount(files);j++){const xvalue* f=xrtValueArrayGet(files,j);const char* path=MdoPackageText(f,"path",220),*base64=MdoPackageText(f,"base64",180000);
                reason="Invalid Skill file path or size";if(!MdoPackagePath(path)||MdoPackageSamePath(path,"SKILL.md")||!base64)goto bad;
                for(size_t k=0;k<j;k++)if(MdoPackageSamePath(path,MdoPackageText(xrtValueArrayGet(files,k),"path",220))){reason="Duplicate file path";goto bad;}
                size_t size=0;void* bytes=xrtBase64DecodeNew(base64,strlen(base64),&size,NULL);bool ok=bytes&&size<=131072;xrtFree(bytes);if(!ok)goto bad;
                total+=size;if(total>524288){reason="Package source exceeds 512 KiB";goto bad;}
            }
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
    return true;
bad:if(error&&capacity)snprintf(error,capacity,"%s",reason);return false;
}
#endif
