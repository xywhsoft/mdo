/*
 * mdo_base.h — HTTP / JSON / 缓冲基础件（被 main.c 单 TU 包含）
 */

#ifndef MDO_BASE_H
#define MDO_BASE_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <xsbase.h>
#include <md4c.h>
#include <md4c-html.h>

/* ================================================================== */
/* HTTP 基础                                                            */
/* ================================================================== */

static bool ConnSend(XS_HttpReq* pReq, const void* pData, size_t iSize)
{
	size_t iWritten = 0;

	if ( pReq == NULL || pData == NULL ) return false;
	if ( pReq->tls != NULL ) {
		return xrtTlsStreamSend(pReq->tls, pData, iSize, &iWritten) == XTLS_OK &&
		       iWritten == iSize;
	}
	return pReq->tcp != NULL &&
		xrtNetStreamSend(pReq->tcp, pData, iSize) == XNET_RESULT_OK;
}

static bool ReplyJSON(XS_HttpReq* pReq, uint16 iStatus, const char* sBody)
{
	char aHead[512];
	char aLen[24];
	xhttpfield aF[3];
	size_t nF = 0, iHeadSize = 0;
	xstrview tReason;

	snprintf(aLen, sizeof(aLen), "%llu", (unsigned long long)strlen(sBody));
	aF[nF].Name = XRT_STR_LITERAL("Content-Length");
	aF[nF].Value = xrtStrView(aLen); nF++;
	aF[nF].Name = XRT_STR_LITERAL("Content-Type");
	aF[nF].Value = xrtStrView("application/json; charset=utf-8"); nF++;
	aF[nF].Name = XRT_STR_LITERAL("Access-Control-Allow-Origin");
	aF[nF].Value = xrtStrView("*"); nF++;

	tReason = xrtHttpStatusText(iStatus);
	if ( !xrtHttp1ResponseWrite(XHTTP_VERSION_1_1, iStatus, tReason,
	     aF, nF, aHead, sizeof(aHead), &iHeadSize) )
		return false;
	if ( !ConnSend(pReq, aHead, iHeadSize) ) return false;
	if ( pReq->head->MethodCode != XHTTP_METHOD_HEAD )
		return ConnSend(pReq, sBody, strlen(sBody));
	return true;
}

/* 兜底 JSON 应答（经静态缓冲返回，避免每次分支都堆分配失败路径） */
static bool ReplyErr(XS_HttpReq* pReq, uint16 iStatus, const char* sMsg)
{
	static char aOut[512];
	snprintf(aOut, sizeof(aOut), "{\"ok\":false,\"error\":\"%s\"}", sMsg);
	return ReplyJSON(pReq, iStatus, aOut);
}

static char* ReqBody(XS_HttpReq* pReq, size_t* pSize)
{
	const xnetbuf* pBuf;
	unsigned char aChunk[4096];
	char* sOut = NULL;
	size_t iOut = 0;
	size_t iBufOff = 0;
	size_t iBufSize;

	*pSize = 0;
	if ( pReq == NULL || pReq->body == NULL ) return NULL;
	pBuf = pReq->tcp != NULL ? xrtNetStreamBuffer(pReq->tcp) :
	       xrtTlsStreamBuffer(pReq->tls);
	if ( pBuf == NULL ) return NULL;
	iBufSize = xrtNetBufSize(pBuf);
	while ( iBufOff < iBufSize ) {
		size_t iGot = xrtNetBufPeek(pBuf, iBufOff, aChunk, sizeof(aChunk));
		xhttp1errorinfo tErr;
		xhttp1bodystatus eBody;
		xbytesview tData;
		xbytesview tIn;
		size_t iConsumed = 0;

		if ( iGot == 0 ) break;
		tIn.Data = aChunk;
		tIn.Size = iGot;
		eBody = xrtHttp1BodyRead(pReq->body, tIn, false, &iConsumed, &tData, &tErr);
		iBufOff += iConsumed;
		if ( tData.Size > 0 ) {
			char* pNew = (char*)xrtRealloc(sOut, iOut + tData.Size + 1);
			if ( pNew == NULL ) { xrtFree(sOut); return NULL; }
			sOut = pNew;
			memcpy(sOut + iOut, tData.Data, tData.Size);
			iOut += tData.Size;
			sOut[iOut] = 0;
		}
		if ( eBody == XHTTP1_BODY_DONE || eBody == XHTTP1_BODY_ERROR ) break;
	}
	*pSize = iOut;
	return sOut;
}

/* ================================================================== */
/* JSON 解析便捷层（xrtJsonParse → xvalue 取值）                        */
/* ================================================================== */

typedef struct {
	xvalue* pRoot;          /* 拥有；NULL = 解析失败/空 */
} MdoJson;

static bool MdoJsonParse(MdoJson* pJ, const char* sText)
{
	pJ->pRoot = NULL;
	if ( sText == NULL ) return false;
	pJ->pRoot = xrtJsonParse(xrtStrView(sText));
	return pJ->pRoot != NULL;
}

static void MdoJsonFree(MdoJson* pJ)
{
	if ( pJ->pRoot != NULL ) xrtValueRelease(pJ->pRoot);
	pJ->pRoot = NULL;
}

static xvalue* JObj(const MdoJson* pJ, const char* sKey)
{
	if ( pJ == NULL || pJ->pRoot == NULL ) return NULL;
	if ( xrtValueType(pJ->pRoot) != XVALUE_OBJECT ) return NULL;
	return xrtValueObjectGet(pJ->pRoot, xrtStrView(sKey));
}

/* 取字符串字段到调用方缓冲；缺失返回 false（缓冲置空） */
static bool JStr(const MdoJson* pJ, const char* sKey, char* pOut, size_t iCap)
{
	xvalue* pV = JObj(pJ, sKey);
	xstrview tS;
	pOut[0] = 0;
	if ( pV == NULL || !xrtValueGetString(pV, &tS) ) return false;
	{
		size_t n = tS.Size < iCap - 1 ? tS.Size : iCap - 1;
		memcpy(pOut, tS.Data, n);
		pOut[n] = 0;
	}
	return pOut[0] != 0;
}

static bool JStrIn(xvalue* pObj, const char* sKey, char* pOut, size_t iCap)
{
	xstrview tS;
	xvalue* pV;
	pOut[0] = 0;
	if ( pObj == NULL || xrtValueType(pObj) != XVALUE_OBJECT ) return false;
	pV = xrtValueObjectGet(pObj, xrtStrView(sKey));
	if ( pV == NULL || !xrtValueGetString(pV, &tS) ) return false;
	{
		size_t n = tS.Size < iCap - 1 ? tS.Size : iCap - 1;
		memcpy(pOut, tS.Data, n);
		pOut[n] = 0;
	}
	return pOut[0] != 0;
}

static int64 JInt(const MdoJson* pJ, const char* sKey, int64 iDefault)
{
	xvalue* pV = JObj(pJ, sKey);
	int64 iOut;
	if ( pV == NULL || !xrtValueGetInt(pV, &iOut) ) return iDefault;
	return iOut;
}

static xvalue* JArr(const MdoJson* pJ, const char* sKey)
{
	xvalue* pV = JObj(pJ, sKey);
	if ( pV == NULL || xrtValueType(pV) != XVALUE_ARRAY ) return NULL;
	return pV;
}

/* ================================================================== */
/* JSON 写出便捷层（内存 writer → str）                                 */
/* ================================================================== */

static xjsonwriter* JWOpen(void)
{
	xjsonwriteconfig tCfg;
	xrtJsonWriteConfigInit(&tCfg);
	return xrtJsonWriterCreate(&tCfg);
}

/* 完成并取走文本；失败返回 NULL。结果 xrtFree 释放。 */
static char* JWTake(xjsonwriter* pW)
{
	size_t iSize = 0;
	str sText;
	if ( pW == NULL ) return NULL;
	if ( !xrtJsonWriterFinish(pW) ) return NULL;
	sText = xrtJsonWriterTake(pW, &iSize);
	return sText;
}

static void JWName(xjsonwriter* pW, const char* sKey)
{
	xrtJsonWriterName(pW, xrtStrView(sKey));
}

static void JWStr(xjsonwriter* pW, const char* sText)
{
	/* NULL 或空串统一写空串 */
	if ( sText == NULL ) sText = "";
	xrtJsonWriterString(pW, xrtStrView(sText));
}

static void JWStrN(xjsonwriter* pW, const char* sText, size_t iSize)
{
	if ( sText == NULL ) { xrtJsonWriterString(pW, xrtStrView("")); return; }
	xrtJsonWriterString(pW, xrtStrViewN(sText, iSize));
}

static void JWInt(xjsonwriter* pW, int64 iValue)
{
	xrtJsonWriterInt(pW, iValue);
}

static void JWUInt(xjsonwriter* pW, uint64 uValue)
{
	xrtJsonWriterUInt(pW, uValue);
}

static void JWBool(xjsonwriter* pW, bool bValue)
{
	xrtJsonWriterBool(pW, bValue);
}

/* ================================================================== */
/* 时间 / 随机 id                                                       */
/* ================================================================== */

static uint64 MdoNowMs(void)
{
	return (uint64)(xrtTimeUnixMs(xrtNow()) );
}

static uint64 MdoRandHex(void)
{
	static uint64 uState = 0;
	uint64 uNow = (uint64)xrtClock();
	if ( uState == 0 ) uState = uNow ^ 0x9E3779B97F4A7C15ull;
	uState = uState * 6364136223846793005ull + 1442695040888963407ull;
	return (uNow ^ (uState >> 11)) & 0xFFFFFFFFull;
}

/* 十进制无符号解析（TCC 运行时无 strtoull 链接） */
static uint64 MdoParseU64(const char* sText)
{
	uint64 uValue = 0;
	if ( sText == NULL ) return 0;
	while ( *sText == ' ' || *sText == '\t' ) sText++;
	while ( *sText >= '0' && *sText <= '9' ) {
		uValue = uValue * 10u + (uint64)(*sText - '0');
		sText++;
	}
	return uValue;
}

/* 在 [pBegin, pEnd) 内找 sNeedle（短字面量）；视图内存非 NUL 结尾，
 * 一律用这个，不可对 xstrview.Data 用 strstr */
static const char* MdoFindBounded(const char* pBegin, const char* pEnd,
	const char* sNeedle)
{
	size_t n = strlen(sNeedle);

	if ( pBegin == NULL || pEnd == NULL || pBegin >= pEnd ) return NULL;
	for ( ; pBegin + n <= pEnd; pBegin++ )
		if ( memcmp(pBegin, sNeedle, n) == 0 ) return pBegin;
	return NULL;
}

/* UTF-8 安全截断到 iMaxBytes 内的最大前缀 */
static size_t Utf8Clip(const char* sText, size_t iLen, size_t iMaxBytes)
{
	size_t n = 0;
	if ( iLen <= iMaxBytes ) return iLen;
	n = iMaxBytes;
	while ( n > 0 && ((unsigned char)sText[n] & 0xC0) == 0x80 ) n--;
	return n;
}

/* ================================================================== */
/* md4c Markdown → HTML                                                 */
/* ================================================================== */

typedef struct {
	char* pData;
	size_t iLen;
	size_t iCap;
} MdoBuf;

static void MdoBufAppend(MdoBuf* pBuf, const char* sText, unsigned iSize)
{
	while ( pBuf->iLen + iSize + 1 > pBuf->iCap ) {
		pBuf->iCap = pBuf->iCap ? pBuf->iCap * 2 : 1024;
		pBuf->pData = (char*)xrtRealloc(pBuf->pData, pBuf->iCap);
	}
	memcpy(pBuf->pData + pBuf->iLen, sText, iSize);
	pBuf->iLen += iSize;
	pBuf->pData[pBuf->iLen] = 0;
}

static void MdoHtmlSink(const MD_CHAR* sData, MD_SIZE iSize, void* pUserData)
{
	MdoBufAppend((MdoBuf*)pUserData, sData, iSize);
}

static char* MdoRenderHTML(const char* sMarkdown, size_t iLen)
{
	MdoBuf tBuf = {0};
	/* GITHUB 方言 + NOHTML：表格/任务列表/删除线/脚注可用，
	 * 原始 HTML 块与行内 HTML 一律转义（模型输出不可信，webview 内防 XSS） */
	unsigned uParserFlags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
	unsigned uRendererFlags = MD_HTML_FLAG_SKIP_UTF8_BOM;

	if ( sMarkdown == NULL || iLen == 0 ) return NULL;
	if ( md_html(sMarkdown, (MD_SIZE)iLen, MdoHtmlSink, &tBuf,
	     uParserFlags, uRendererFlags) != 0 ) {
		xrtFree(tBuf.pData);
		return NULL;
	}
	return tBuf.pData;
}

/* 把 C 字符串作为 JSON 字符串（带引号+转义）追加进 MdoBuf */
static void BufJsonStr(MdoBuf* pB, const char* sText)
{
	const unsigned char* p = (const unsigned char*)(sText ? sText : "");
	MdoBufAppend(pB, "\"", 1);
	while ( *p ) {
		switch ( *p ) {
		case '"':  MdoBufAppend(pB, "\\\"", 2); break;
		case '\\': MdoBufAppend(pB, "\\\\", 2); break;
		case '\b': MdoBufAppend(pB, "\\b", 2); break;
		case '\f': MdoBufAppend(pB, "\\f", 2); break;
		case '\n': MdoBufAppend(pB, "\\n", 2); break;
		case '\r': MdoBufAppend(pB, "\\r", 2); break;
		case '\t': MdoBufAppend(pB, "\\t", 2); break;
		default:
			if ( *p < 0x20 ) {
				char aEsc[8];
				snprintf(aEsc, sizeof(aEsc), "\\u%04x", *p);
				MdoBufAppend(pB, aEsc, 6);
			} else {
				MdoBufAppend(pB, (const char*)p, 1);
			}
			break;
		}
		p++;
	}
	MdoBufAppend(pB, "\"", 1);
}

#endif /* MDO_BASE_H */
