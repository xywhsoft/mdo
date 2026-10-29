/* Native platform selection for a single HTTP(S) destination. Secrets, if a
 * process proxy URI contains them, are transient; clear the complete result
 * after creating the transport. This API never changes platform settings. */
#define XS_SYSTEM_PROXY_VERSION 1u
#define XS_SYSTEM_PROXY_DIRECT 0u
#define XS_SYSTEM_PROXY_HTTP_CONNECT 1u
#define XS_SYSTEM_PROXY_SOCKS5 2u
typedef struct XS_SystemProxyInfo {
    uint32 Size;
    uint32 Version;
    uint32 Kind;
    uint16 Port;
    char Host[256];
    char User[256];
    char Password[4097];
    char Reason[192];
} XS_SystemProxyInfo;
/* Windows uses current-user WinINet settings/PAC (then WinHTTP service
 * defaults); Android uses ProxySelector; POSIX uses HTTPS_PROXY/ALL_PROXY and
 * NO_PROXY. No proxy means DIRECT. Discovery/unsupported values fail instead
 * of silently bypassing an explicitly configured proxy. TimeoutMs is passed
 * to the platform resolver (1..10000). Browser extension state is not read. */
XS_API bool xsSystemProxyResolve(cstr Url,uint32 TimeoutMs,XS_SystemProxyInfo* Result);
