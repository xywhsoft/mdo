/* 代理路径隔离测试：vendored xllm（带 proxy 字段）→ 本机 CONNECT 代理 → ling 端点
 * 用法：proxytest.exe [proxy_host] [proxy_port]（缺省 127.0.0.1:18888）
 * 前置：python tests/mock 代理或任一 CONNECT 代理在监听；不依赖 API key 正确性——
 *       只要能拿到 HTTP 层回应（哪怕 401）就证明 TLS-over-CONNECT 打通。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xllm.h"

int main(int argc, char** argv)
{
    const char* sProxyHost = argc > 1 ? argv[1] : "127.0.0.1";
    int iProxyPort = argc > 2 ? atoi(argv[2]) : 18888;

    xllm_client_config tCfg;
    xllmClientConfigInit(&tCfg);
    tCfg.sBaseUrl = "https://ai.xywhsoft.com:8444/v1";
    tCfg.sApiKey = "sk-invalid-on-purpose";
    tCfg.sModel = "ling-3.0-tiny";
    tCfg.eProxyKind = 2;
    tCfg.sProxyHost = sProxyHost;
    tCfg.uProxyPort = (uint16_t)iProxyPort;

    xllm_error tErr;
    xllmErrorInit(&tErr);
    xllm_client* pC = xllmClientCreate(&tCfg, &tErr);
    if (!pC) { printf("client create failed: %s\n", tErr.sMessage); return 1; }

    xllm_request tReq;
    xllmRequestInit(&tReq);
    xllm_message tMsg;
    xllmMessageInit(&tMsg, XLLM_ROLE_USER);
    xllmMessageSetContent(&tMsg, "hi");
    xllmRequestAddMessage(&tReq, &tMsg);
    xllmMessageUnit(&tMsg);

    xllm_response* pResp = NULL;
    xllm_error tRErr;
    xllmErrorInit(&tRErr);
    xllm_result r = xllmClientComplete(pC, &tReq, NULL, &pResp, &tRErr);
    printf("result=%d\n  err=%s\n  provider=%s\n  http=%d\n",
        (int)r,
        tRErr.sMessage[0] ? tRErr.sMessage : "-",
        tRErr.sProviderMessage[0] ? tRErr.sProviderMessage : "-",
        (pResp != NULL) ? (int)pResp->uHttpStatus : -1);

    if (pResp) xllmResponseDestroy(pResp);
    xllmRequestUnit(&tReq);
    xllmClientDestroy(pC);
    /* 401/4xx = 代理链路通（拿到了真实 HTTP 回应）；0/超时/断连 = 链路没通 */
    return (pResp != NULL && pResp->uHttpStatus != 0) ? 0 : 2;
}
