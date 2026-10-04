#include <string.h>
#include "service_client.h"

xvalue* MdoRemoteServiceCall(cstr Path, cstr Method, const xvalue* Body,
    const MdoAccountLease* Lease, uint16* Status)
{
    if (Status) *Status = 0u;
    if (!Path || !Method) return NULL;
    bool allowed = !strcmp(Path,"/api/v1/devices") && !strcmp(Method,"GET") && !Body;
    if (!strcmp(Method,"POST") && xrtValueType(Body) == XVALUE_OBJECT) allowed =
        !strcmp(Path,"/api/v1/devices/register") || !strcmp(Path,"/api/v1/devices/revoke") ||
        !strcmp(Path,"/api/v1/devices/remove") || !strcmp(Path,"/api/v1/devices/ticket");
    if (!allowed) return NULL;
    xvalue* value = MdoAccountServiceJson(Path,Method,Body,Lease,Status);
    if (Lease && xrtCancelRequested(Lease->Cancel)) {
        MdoAccountSecretValueRelease(value); if (Status) *Status = 0u; return NULL;
    }
    return value;
}
MdoRemoteSocket* MdoRemoteServiceConnect(MdoRemoteNet* Net,
    const xvalue* Ticket, const MdoAccountLease* Lease, uint16* Status)
{
    if (Status) *Status = 0u;
    if (!Net || !Lease || !Lease->Managed || !Lease->Cancel || xrtCancelRequested(Lease->Cancel)) return NULL;
    cstr token = MdoAccountText(Ticket,"ticket",64u), path = MdoAccountText(Ticket,"path",128u);
    cstr protocol = MdoAccountText(Ticket,"protocol",64u); uint64 limit = 0u;
    if (!MdoAccountHex(token,64u) || !path || strcmp(path,MDO_REMOTE_RELAY_PATH) || !protocol ||
        strcmp(protocol,MDO_REMOTE_RELAY_PROTOCOL) ||
        !MdoAccountGetUInt(xrtValueObjectGet(Ticket,XRT_STR_LITERAL("payload_limit")),&limit) ||
        limit != MDO_REMOTE_RELAY_PAYLOAD) return NULL;
    char origin[MDO_ACCOUNT_ORIGIN_LIMIT], host[256], offers[192]; uint32 port;
    if (!MdoAccountOrigin(MDO_ACCOUNT_SERVICE_ORIGIN,origin) || strcmp(origin,MDO_ACCOUNT_SERVICE_ORIGIN)) return NULL;
    bool secure = !strncmp(origin,"https://",8u); const char* authority = origin + (secure ? 8u : 7u);
    const char* end = authority + strlen(authority); const char* suffix = NULL;
    if (*authority == '[') {
        authority++; end = strchr(authority,']'); if (!end) return NULL;
        if (end[1] == ':') suffix = end+2;
    } else {
        const char* colon = strchr(authority,':'); if (colon) { end = colon; suffix = colon+1; }
    }
    size_t size = (size_t)(end-authority);
    if (!size || size >= sizeof(host)) return NULL;
    memcpy(host,authority,size); host[size] = 0;
    port = secure ? 443u : 80u;
    if (suffix) {
        port = 0u;
        for (size_t i = 0u; suffix[i]; i++) {
            if (suffix[i] < '0' || suffix[i] > '9') return NULL;
            port = port*10u + (uint32)(suffix[i]-'0'); if (port > 65535u) return NULL;
        }
        if (!port) return NULL;
    }
    snprintf(offers,sizeof(offers),MDO_REMOTE_RELAY_PROTOCOL ", xadmin.ticket.%s",token);
    MdoRemoteSocketConfig config = {host,(uint16)port,secure,MDO_REMOTE_RELAY_PATH,origin,
        offers,MDO_REMOTE_RELAY_PROTOCOL,MDO_REMOTE_RELAY_PAYLOAD+MDO_REMOTE_RELAY_HEADER};
    MdoRemoteSocket* socket = MdoRemoteSocketOpen(Net,&config,Lease->Cancel,Status);
    xrtSecureZero(offers,sizeof(offers)); return socket;
}
