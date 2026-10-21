/* Browser navigation is state/PKCE protected; all other mutations use the
 * normal loopback/write-token admission path. Never echo callback data. */
static bool McpAuthCallback(MdoApiContext* C)
{
    char State[128]={0},Code[4097]={0},Issuer[4097]={0},Error[257]={0};unsigned Seen=0;
    bool Ok=C->Target.Query.Size>0&&C->Target.Query.Size<=12000;
    for(size_t At=0;Ok&&At<C->Target.Query.Size;) {
        size_t End=At,Equal;
        while(End<C->Target.Query.Size&&C->Target.Query.Data[End]!='&')End++;
        Equal=At;while(Equal<End&&C->Target.Query.Data[Equal]!='=')Equal++;
        size_t Kn=0,Vn=0;char* Key=Equal<End?(char*)xrtPercentDecodeNew(xrtStrViewN(C->Target.Query.Data+At,Equal-At),&Kn):NULL;
        char* Value=Equal<End?(char*)xrtPercentDecodeNew(xrtStrViewN(C->Target.Query.Data+Equal+1,End-Equal-1),&Vn):NULL;
        Ok=Key&&Value&&!memchr(Key,0,Kn)&&!memchr(Value,0,Vn)&&xrtUtf8Valid(xrtStrViewN(Value,Vn),NULL);
        unsigned Bit=0;char* Out=NULL;size_t Limit=0;
        if(Ok) {
            if(!strcmp(Key,"state")){Bit=1;Out=State;Limit=sizeof(State);}
            else if(!strcmp(Key,"code")){Bit=2;Out=Code;Limit=sizeof(Code);}
            else if(!strcmp(Key,"iss")){Bit=4;Out=Issuer;Limit=sizeof(Issuer);}
            else if(!strcmp(Key,"error")){Bit=8;Out=Error;Limit=sizeof(Error);}
            if(Bit){Ok=!(Seen&Bit)&&Vn&&Vn<Limit;if(Ok){memcpy(Out,Value,Vn);Out[Vn]=0;Seen|=Bit;}}
        }
        xrtFree(Key);if(Value){xrtSecureZero(Value,Vn);xrtFree(Value);}At=End+1;
    }
    Ok=Ok&&(Seen&1)&&(((Seen&2)!=0)^((Seen&8)!=0))&&MdoMcpAuthorizationCallback(State,
        Seen&2?Code:NULL,Seen&4?Issuer:NULL,Seen&8?Error:NULL);
    xrtSecureZero(Code,sizeof(Code));
    return MdoApiReplyAccountHtml(C,Ok?200:400,Ok?
        "<!doctype html><meta charset=utf-8><title>MCP</title><p>已收到授权结果，请返回墨斗查看状态。</p>":
        "<!doctype html><meta charset=utf-8><title>MCP</title><p>授权请求已过期或不匹配，请返回墨斗重新授权。</p>");
}

bool MdoApiMcpAuthRoute(MdoApiContext* C)
{
    if(!C->ParamCount)return McpAuthCallback(C);
    char Id[129];xstrview Param=C->Params[0];
    if(!Param.Size||Param.Size>=sizeof(Id))return MdoApiReplyError(C,400,"invalid_mcp_server","Invalid MCP ID",NULL);
    for(size_t i=0;i<Param.Size;i++)if(!((Param.Data[i]>='a'&&Param.Data[i]<='z')||(Param.Data[i]>='A'&&Param.Data[i]<='Z')||
        (Param.Data[i]>='0'&&Param.Data[i]<='9')||Param.Data[i]=='-'||Param.Data[i]=='_'||Param.Data[i]=='.'))
        return MdoApiReplyError(C,400,"invalid_mcp_server","Invalid MCP ID",NULL);
    memcpy(Id,Param.Data,Param.Size);Id[Param.Size]=0;
    if(C->Request->head->MethodCode==XHTTP_METHOD_GET) {
        xvalue* Status=MdoMcpAuthorizationStatus(Id);
        return Status?MdoApiReplySuccessTake(C,200,Status,NULL):MdoApiReplyError(C,404,"mcp_oauth_unavailable","OAuth is not configured for this server",NULL);
    }
    bool Ok=false;
    xstrview Path=C->Target.Path;
    if(C->Request->head->MethodCode==XHTTP_METHOD_DELETE)Ok=MdoMcpAuthorizationForget(Id);
    else if(Path.Size>=5&&!memcmp(Path.Data+Path.Size-5,"/open",5))Ok=MdoMcpAuthorizationOpen(Id);
    else {
        if(!xsCredentialProtectionAvailable())return MdoApiReplyError(C,409,"credential_protection_unavailable",
            "This device cannot securely store OAuth credentials; system credential protection is unavailable",NULL);
        const xhttpfield* Host=NULL;char Origin[128];
        if(xrtHttpFieldGetUnique(C->Request->head->Fields,C->Request->head->FieldCount,XRT_STR_LITERAL("Host"),&Host)==XHTTP_NEXT_ITEM&&Host&&Host->Value.Size<90) {
            snprintf(Origin,sizeof(Origin),"%s://%.*s",C->Request->tls?"https":"http",(int)Host->Value.Size,Host->Value.Data);
            Ok=MdoMcpAuthorizationStart(Id,Origin);
        }
    }
    return Ok?MdoApiReplySuccessTake(C,202,MdoMcpAuthorizationStatus(Id),NULL):
        MdoApiReplyError(C,409,"mcp_oauth_busy","Authorization is unavailable, busy or has not reached the browser step",NULL);
}
