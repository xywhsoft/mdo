#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
/* Small real stdio MCP peer. No network, keys, or test logging. */
static int field(const char* json,const char* key,char* out,size_t limit,int raw)
{
    char pattern[80];snprintf(pattern,sizeof(pattern),"\"%s\"",key);const char* p=strstr(json,pattern);if(!p)return 0;
    p=strchr(p+strlen(pattern),':');if(!p)return 0;p++;while(isspace((unsigned char)*p))p++;
    const char* begin=p;size_t n=0;
    if(*p=='"'){p++;while(*p&&*p!='"')p++;if(!*p)return 0;p++;if(!raw){begin++;n=(size_t)(p-begin-1);}else n=(size_t)(p-begin);}
    else {while(*p&&*p!=','&&*p!='}'&&!isspace((unsigned char)*p))p++;n=(size_t)(p-begin);}
    if(!n||n>=limit)return 0;memcpy(out,begin,n);out[n]=0;return 1;
}
int main(int argc,char** argv)
{
    const char* token=getenv("QA_PRIVATE_TOKEN"),*root=getenv("MDO_EXTENSION_ROOT"),*digest=getenv("MDO_EXTENSION_DIGEST");
    FILE* support=fopen("support/banner.txt","rb");if(argc!=2||!token||!*token||!root||!digest||!support)return 4;fclose(support);
    char line[65536],method[256],id[256];int initialized=0;
    while(fgets(line,sizeof(line),stdin)){
        if(!field(line,"method",method,sizeof(method),0))continue;
        if(!strcmp(method,"notifications/initialized")){initialized=1;continue;}
        if(!field(line,"id",id,sizeof(id),1))continue;
        if(!strcmp(method,"initialize"))printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{\"tools\":{},\"resources\":{}},\"serverInfo\":{\"name\":\"directory-fixture\",\"version\":\"1\"}}}\n",id);
        else if(initialized&&!strcmp(method,"tools/list"))printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"tools\":[{\"name\":\"echo\",\"description\":\"Directory fixture\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}},\"annotations\":{\"readOnlyHint\":true}}]}}\n",id);
        else if(initialized&&!strcmp(method,"resources/read"))printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"contents\":[{\"uri\":\"fixture://marker\",\"mimeType\":\"text/plain\",\"text\":\"%s\"}]}}\n",id,argv[1]);
        else printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{}}\n",id);
        fflush(stdout);
    }return 0;
}
