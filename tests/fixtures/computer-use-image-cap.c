#include <stdio.h>
#include <stdlib.h>
#include <xsbase.h>
#include <xwork.h>
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;xwork_mcp_stdio_config Config;xwork_error Error;xwork_tool_output Output;
    const char* Args[]={"tests/fixtures/computer-use-image-peer.py"};
    xworkMcpStdioConfigInit(&Config);Config.sServerName="large-image";
    Config.sProgram="python";Config.psArguments=Args;Config.iArgumentCount=1;
    Config.sProtocolVersion="2025-11-25";Config.iMaxMessageBytes=16u*1024u*1024u;
    xwork_mcp_client* Client=xworkMcpClientCreate(&Config,&Error);
    xworkToolOutputInit(&Output);
    if(!Client||!xworkMcpClientConnect(Client,&Error)||
       xworkMcpClientCallTool(Client,"image","{}",NULL,UINT64_MAX,&Output,&Error)!=XWORK_RESULT_OK) {
        printf("large_image_error=%s\n",Error.sMessage);exit(2);
    }
    printf("large_image_bytes=%zu\n",Output.iImageSize);
    if(!Output.bSuccess||Output.iImageSize<=1024u*1024u)exit(17);
    FILE* File=fopen(".build/computer-use/large-image-result.png","wb");
    if(!File||fwrite(Output.pImageBytes,1,Output.iImageSize,File)!=Output.iImageSize)exit(3);
    fclose(File);xworkToolOutputUnit(&Output);xworkMcpClientDestroy(Client);
    puts("large_mcp_image=PASS");fflush(stdout);exit(0);
}
void ServiceUnit(XS_HostInfo* Host){(void)Host;}
