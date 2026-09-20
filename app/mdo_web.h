/*
 * mdo_web.h — 搜索三件（web_search / fetch_content / get_search_content）
 *
 * 设计文档《mdo.md》工具表 v0.2 的"搜索三件"。
 * 抓取机械：xrtProcessSpawn 起系统 curl（Windows 10+/Linux 自带），
 *   -L 跟重定向、--compressed 自动解压、--max-time 硬超时；
 * 编码：UTF-8 校验优先，非法序列按 GB18030 转 UTF-8（仿 xwork exec 的转码路径）；
 * HTML → 可读文本：剥 script/style、块级标签转行、剥标签、实体解码、折叠空行；
 * 两段式：web_search 存结果集（responseId）→ get_search_content 按序号取全文
 *   /findText 定位切片（防上下文炸弹）；页面 LRU 缓存避免重复抓取。
 * 三件均为只读效应（XWORK_TOOL_EFFECT_READ_ONLY）。
 * 仅用公共 API：xrtProcess* / xrtJsonWriter / xrtValue* / xworkAgentRegisterTool。
 */

#ifndef MDO_WEB_H
#define MDO_WEB_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#define MDO_WEB_MAX_BYTES    (1024u * 1024u)   /* 单页抓取上限 1MB */
#define MDO_WEB_TIMEOUT_S    15                /* 单次抓取硬超时（秒） */
#define MDO_WEB_SETS_MAX     12                /* 结果集环形容量 */
#define MDO_WEB_RESULTS_MAX  10                /* 每结果集条目上限 */
#define MDO_WEB_PAGES_MAX    24                /* 结果集内页面缓存上限 */

/* ---------------- 小工具 ---------------- */

static char* MdoWebStrDupN(const char* s, size_t n)
{
	char* p = (char*)malloc(n + 1u);
	if ( p == NULL ) return NULL;
	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

static void MdoWebUrlDecode(const char* sIn, char* pOut, size_t iCap)
{
	size_t i = 0, o = 0;
	while ( sIn[i] != 0 && o + 1 < iCap ) {
		if ( sIn[i] == '%' && sIn[i + 1] != 0 && sIn[i + 2] != 0 ) {
			char aHex[3] = { sIn[i + 1], sIn[i + 2], 0 };
			pOut[o++] = (char)strtoul(aHex, NULL, 16);
			i += 3;
		} else if ( sIn[i] == '+' ) {
			pOut[o++] = ' ';
			i++;
		} else {
			pOut[o++] = sIn[i++];
		}
	}
	pOut[o] = 0;
}

static void MdoWebEntityDecode(char* s)
{
	size_t i = 0, o = 0;
	while ( s[i] != 0 ) {
		if ( s[i] == '&' ) {
			if ( strncmp(s + i, "&amp;", 5) == 0 ) { s[o++] = '&'; i += 5; continue; }
			if ( strncmp(s + i, "&lt;", 4) == 0 ) { s[o++] = '<'; i += 4; continue; }
			if ( strncmp(s + i, "&gt;", 4) == 0 ) { s[o++] = '>'; i += 4; continue; }
			if ( strncmp(s + i, "&quot;", 6) == 0 ) { s[o++] = '"'; i += 6; continue; }
			if ( strncmp(s + i, "&#39;", 5) == 0 ) { s[o++] = '\''; i += 5; continue; }
			if ( strncmp(s + i, "&apos;", 6) == 0 ) { s[o++] = '\''; i += 6; continue; }
			if ( strncmp(s + i, "&nbsp;", 6) == 0 ) { s[o++] = ' '; i += 6; continue; }
		}
		s[o++] = s[i++];
	}
	s[o] = 0;
}

/* ---------------- 抓取基座（curl 同步执行） ---------------- */

static char* MdoWebFetch(const char* sUrl, char* sErr, size_t iErrCap)
{
	xprocessconfig tCfg;
	xprocess* pProc = NULL;
	char* sBody = NULL;
	size_t iCap = 256u * 1024u, iLen = 0;
	char aTimeout[16];
	const char* aArgs[8];
	xwaitresult eWait;

	if ( strncmp(sUrl, "http://", 7) != 0 && strncmp(sUrl, "https://", 8) != 0 ) {
		snprintf(sErr, iErrCap, "only http/https URLs are supported");
		return NULL;
	}
	snprintf(aTimeout, sizeof(aTimeout), "%d", MDO_WEB_TIMEOUT_S);

	xrtProcessConfigInit(&tCfg);
	tCfg.Target = XPROCESS_EXEC;
	tCfg.Program = "curl";
	tCfg.Arg0 = "curl";
	tCfg.HideWindow = true;
	aArgs[0] = "-sL";
	aArgs[1] = "--compressed";
	aArgs[2] = "--max-time";
	aArgs[3] = aTimeout;
	aArgs[4] = "-A";
	aArgs[5] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 mdo-agent/0.1";
	aArgs[6] = "--";
	aArgs[7] = sUrl;
	tCfg.Args = aArgs;
	tCfg.ArgCount = 8;
	tCfg.Stdout.Mode = XPROCESS_IO_PIPE;
	tCfg.Stderr.Mode = XPROCESS_IO_NULL;
	tCfg.Stdin.Mode = XPROCESS_IO_NULL;

	pProc = xrtProcessSpawn(&tCfg);
	if ( pProc == NULL ) {
		snprintf(sErr, iErrCap, "failed to spawn curl (is curl installed and in PATH?)");
		return NULL;
	}
	sBody = (char*)malloc(iCap);
	if ( sBody == NULL ) { xrtProcessDestroy(pProc); snprintf(sErr, iErrCap, "out of memory"); return NULL; }
	for ( ; ; ) {
		int64 iN = xrtProcessRead(pProc, XPROCESS_STDOUT, sBody + iLen, iCap - iLen - 1u);
		if ( iN <= 0 ) break;
		iLen += (size_t)iN;
		if ( iLen + 1u >= iCap ) {
			char* pNew;
			if ( iCap >= MDO_WEB_MAX_BYTES * 4u ) break;
			iCap *= 2u;
			pNew = (char*)realloc(sBody, iCap);
			if ( pNew == NULL ) break;
			sBody = pNew;
		}
	}
	eWait = xrtProcessWaitFor(pProc, (uint64)MDO_WEB_TIMEOUT_S * 1000u * 1000u);
	xrtProcessDestroy(pProc);
	if ( eWait != XWAIT_OK ) {
		free(sBody);
		snprintf(sErr, iErrCap, "fetch timed out after %ds", MDO_WEB_TIMEOUT_S);
		return NULL;
	}
	if ( iLen == 0 ) {
		free(sBody);
		snprintf(sErr, iErrCap, "empty response (bad URL, blocked, or no network?)");
		return NULL;
	}
	sBody[iLen] = 0;

	/* 编码：UTF-8 合法即用；非法序列按 GB18030 转（中文网页常见），再不行替换法兜底 */
	if ( memchr(sBody, 0, iLen) == NULL && !xrtUtf8Valid(xrtStrViewN(sBody, iLen), NULL) ) {
#if defined(_WIN32)
		int iWide = MultiByteToWideChar(936 /*GB18030*/, 0, sBody, (int)iLen, NULL, 0);
		if ( iWide > 0 ) {
			wchar_t* pWide = (wchar_t*)malloc(((size_t)iWide + 1u) * sizeof(wchar_t));
			if ( pWide != NULL && MultiByteToWideChar(936, 0, sBody, (int)iLen, pWide, iWide) == iWide ) {
				int iUtf8 = WideCharToMultiByte(CP_UTF8, 0, pWide, iWide, NULL, 0, NULL, NULL);
				if ( iUtf8 > 0 ) {
					char* pUtf8 = (char*)malloc((size_t)iUtf8 + 1u);
					if ( pUtf8 != NULL && WideCharToMultiByte(CP_UTF8, 0, pWide, iWide, pUtf8, iUtf8, NULL, NULL) == iUtf8 ) {
						pUtf8[iUtf8] = 0;
						free(sBody);
						return pUtf8;
					}
					free(pUtf8);
				}
			}
			free(pWide);
		}
#endif
		{
			xbytesview tSrc;
			size_t iOut = 0;
			char* pFix;
			tSrc.Data = (const unsigned char*)sBody;
			tSrc.Size = iLen;
			pFix = (char*)xrtTranscode(tSrc, XENCODING_UTF8, XENCODING_UTF8, XUTF_REPLACE, false, &iOut);
			if ( pFix != NULL ) { free(sBody); sBody = pFix; }
		}
	} else {
		sBody[iLen] = 0;
	}
	return sBody;
}

/* ---------------- HTML → 可读文本 ---------------- */

static bool MdoWebIsBlockTag(const char* s)
{
	static const char* const c_aTags[] = { "p", "div", "br", "li", "tr", "td", "h1", "h2",
		"h3", "h4", "h5", "h6", "ul", "ol", "table", "section", "article", "header",
		"footer", "blockquote", "pre", "form", NULL };
	int i;
	for ( i = 0; c_aTags[i] != NULL; i++ )
		if ( strncmp(s, c_aTags[i], strlen(c_aTags[i])) == 0 ) return true;
	return false;
}

static char* MdoWebHtmlToText(const char* sHtml)
{
	size_t iCap = strlen(sHtml) + 256u;
	size_t o = 0, i = 0, r = 0, w = 0;
	int iSkip = 0, iBlank = 0;
	char* pOut = (char*)malloc(iCap);
	char* pFold;

	if ( pOut == NULL ) return NULL;
	while ( sHtml[i] != 0 ) {
		if ( sHtml[i] == '<' ) {
			const char* pTag = sHtml + i + 1;
			if ( pTag[0] == '/' ) pTag++;
			if ( strncasecmp(pTag, "script", 6) == 0 || strncasecmp(pTag, "style", 5) == 0 ||
			     strncasecmp(pTag, "noscript", 8) == 0 ) {
				iSkip = (sHtml[i + 1] == '/') ? 0 : 1;
			}
			if ( MdoWebIsBlockTag(pTag) && o > 0 && pOut[o - 1] != '\n' ) {
				if ( o + 1u >= iCap ) { iCap *= 2u; pOut = (char*)realloc(pOut, iCap); if ( !pOut ) return NULL; }
				pOut[o++] = '\n';
			}
			while ( sHtml[i] != 0 && sHtml[i] != '>' ) i++;
			if ( sHtml[i] == '>' ) i++;
			continue;
		}
		if ( iSkip ) { i++; continue; }
		if ( sHtml[i] == '\r' ) { i++; continue; }
		pOut[o++] = sHtml[i++];
		if ( o + 1u >= iCap ) { iCap *= 2u; pOut = (char*)realloc(pOut, iCap); if ( !pOut ) return NULL; }
	}
	pOut[o] = 0;
	MdoWebEntityDecode(pOut);

	pFold = (char*)malloc(o + 2u);
	if ( pFold == NULL ) return pOut;
	while ( pOut[r] != 0 ) {
		char c = pOut[r++];
		if ( c == ' ' || c == '\t' ) c = ' ';
		if ( c == '\n' ) {
			while ( pOut[r] == ' ' || pOut[r] == '\t' ) r++;
			if ( pOut[r] == '\n' ) { c = '\n'; while ( pOut[r] == '\n' ) r++; }
		}
		if ( c == '\n' ) {
			if ( iBlank >= 2 ) continue;
			iBlank++;
		} else iBlank = 0;
		pFold[w++] = c;
	}
	pFold[w] = 0;
	free(pOut);
	return pFold;
}

/* ---------------- 结果集与页面缓存（进程内，锁保护） ---------------- */

typedef struct {
	char* sUrl;
	char* sTitle;
	char* sSnippet;
} MdoWebResult;

typedef struct {
	uint64 uId;
	char* sQuery;
	MdoWebResult aResults[MDO_WEB_RESULTS_MAX];
	size_t nResults;
	char* aPageUrl[MDO_WEB_PAGES_MAX];
	char* aPageText[MDO_WEB_PAGES_MAX];
	size_t nPages;
} MdoWebSet;

typedef struct {
	xmutex* pLock;
	MdoWebSet aSets[MDO_WEB_SETS_MAX];
	size_t iNext;
	uint64 uNextId;
} MdoWebState;

static MdoWebState g_web = { NULL, { { 0, NULL, { { NULL, NULL, NULL } }, 0, { NULL }, { NULL }, 0 } }, 0, 0 };
static bool g_webInit = false;

static void MdoWebInit(void)
{
	if ( g_webInit ) return;
	g_web.pLock = xrtMutexCreate();
	g_web.uNextId = 100;
	g_webInit = true;
}

static void MdoWebResultUnit(MdoWebResult* pR)
{
	free(pR->sUrl); free(pR->sTitle); free(pR->sSnippet);
	pR->sUrl = pR->sTitle = pR->sSnippet = NULL;
}

static void MdoWebSetUnit(MdoWebSet* pSet)
{
	size_t i;
	free(pSet->sQuery);
	pSet->sQuery = NULL;
	for ( i = 0; i < pSet->nResults; i++ ) MdoWebResultUnit(&pSet->aResults[i]);
	pSet->nResults = 0;
	for ( i = 0; i < pSet->nPages; i++ ) { free(pSet->aPageUrl[i]); free(pSet->aPageText[i]); }
	pSet->nPages = 0;
}

static xwork_result MdoWebFail(xwork_tool_output* pOutput, const char* sMsg)
{
	/* xwork 约定：工具失败 = OK + bSuccess=false（错误喂回模型自行纠正）；
	 * RESULT_ERROR 属基础设施级，会中断整个回合 */
	xworkToolOutputSet(pOutput, false, sMsg);
	return XWORK_RESULT_OK;
}

/* ---------------- Bing 结果解析 ---------------- */

/* 就地剥除内嵌标签（<strong> 等） */
static void MdoWebStripTags(char* s)
{
	size_t i = 0, o = 0;
	while ( s[i] != 0 ) {
		if ( s[i] == '<' ) {
			while ( s[i] != 0 && s[i] != '>' ) i++;
			if ( s[i] == '>' ) i++;
			continue;
		}
		s[o++] = s[i++];
	}
	s[o] = 0;
}

/* Bing 解析：按 class="b_algo" 分块；块内 h2>a 取 URL/标题，第一个 <p> 取摘要 */
static size_t MdoWebParseBing(const char* sHtml, MdoWebResult* aOut, size_t iMax)
{
	const char* p = sHtml;
	static const char sMark[] = "class=\"b_algo\"";
	size_t n = 0;

	while ( n < iMax && (p = strstr(p, sMark)) != NULL ) {
		const char* pBlock = p;
		const char* pNext = strstr(pBlock + 1, sMark);
		size_t iBlockLen = pNext ? (size_t)(pNext - pBlock) : (size_t)4096;
		const char* pH2;
		char aUrl[1024] = "";
		char aTitle[512] = "";
		char aSnippet[1024] = "";

		if ( iBlockLen > 8192 ) iBlockLen = 8192;
		p = pBlock + 1;

		/* h2 内的 a 才是结果链接（块内第一个 a 可能是面包屑） */
		pH2 = strstr(pBlock, "<h2");
		if ( pH2 == NULL || (size_t)(pH2 - pBlock) > iBlockLen ) continue;
		{
			const char* pA = strchr(pH2, '<');
			while ( pA != NULL && pA < pBlock + iBlockLen ) {
				const char* pHref = strstr(pA, "href=\"");
				if ( pHref != NULL && pHref < pBlock + iBlockLen ) {
					const char* pU = pHref + 6;
					const char* pE = strchr(pU, '"');
					const char* pGt = strchr(pU, '>');
					if ( pE != NULL && (pGt == NULL || pE < pGt) ) {
						size_t iLen = (size_t)(pE - pU);
						if ( iLen < sizeof(aUrl) ) { memcpy(aUrl, pU, iLen); aUrl[iLen] = 0; }
						break;
					}
				}
				pA = strchr(pA + 1, '<');
			}
		}
		if ( aUrl[0] == 0 || strncmp(aUrl, "http", 4) != 0 ) continue;
		{
			const char* pTitle = strchr(pH2, '>');
			if ( pTitle != NULL ) {
				const char* pE2 = strstr(pTitle, "</a>");
				if ( pE2 != NULL && (size_t)(pE2 - pBlock) <= iBlockLen + 64 ) {
					size_t iT = (size_t)(pE2 - (pTitle + 1));
					if ( iT < sizeof(aTitle) ) {
						memcpy(aTitle, pTitle + 1, iT);
						aTitle[iT] = 0;
						MdoWebStripTags(aTitle);
						MdoWebEntityDecode(aTitle);
					}
				}
			}
		}
		{
			const char* pP = strstr(pBlock, "<p");
			if ( pP != NULL && (size_t)(pP - pBlock) < iBlockLen ) {
				const char* pGt = strchr(pP, '>');
				const char* pE3 = pGt ? strstr(pGt, "</p>") : NULL;
				if ( pGt != NULL && pE3 != NULL ) {
					size_t iS = (size_t)(pE3 - (pGt + 1));
					if ( iS >= sizeof(aSnippet) ) iS = sizeof(aSnippet) - 1u;
					memcpy(aSnippet, pGt + 1, iS);
					aSnippet[iS] = 0;
					MdoWebStripTags(aSnippet);
					MdoWebEntityDecode(aSnippet);
				}
			}
		}
		if ( aTitle[0] == 0 ) snprintf(aTitle, sizeof(aTitle), "%s", aUrl);
		aOut[n].sUrl = MdoWebStrDupN(aUrl, strlen(aUrl));
		aOut[n].sTitle = MdoWebStrDupN(aTitle, strlen(aTitle));
		aOut[n].sSnippet = aSnippet[0] ? MdoWebStrDupN(aSnippet, strlen(aSnippet)) : NULL;
		if ( aOut[n].sUrl && aOut[n].sTitle ) n++;
		else MdoWebResultUnit(&aOut[n]);
	}
	return n;
}

/* ---------------- 参数读取（公共 xrtValue API） ---------------- */

static char* MdoWebArgText(xvalue* pArgs, const char* sKey)
{
	xvalue* pV = xrtValueObjectGet(pArgs, xrtStrView(sKey));
	xstrview tV;
	if ( pV == NULL || !xrtValueGetString(pV, &tV) ) return NULL;
	return MdoWebStrDupN(tV.Data, tV.Size);
}

static int64 MdoWebArgInt(xvalue* pArgs, const char* sKey, int64 iDefault)
{
	xvalue* pV = xrtValueObjectGet(pArgs, xrtStrView(sKey));
	int64 iV = 0;
	if ( pV != NULL && xrtValueGetInt(pV, &iV) && iV > 0 ) return iV;
	return iDefault;
}

/* ---------------- web_search ---------------- */

static xwork_result MdoWebToolSearch(void* pUserData, const xwork_tool_context* pContext,
	const char* sArgumentsJson, xwork_tool_output* pOutput, xwork_error* pError)
{	fprintf(stderr, "[web] search enter\n");

	xvalue* tArgs = NULL;
	char* sQuery = NULL;
	char* sHtml = NULL;
	char* sQEnc = NULL;
	char sUrl[1024];
	char sErr[160] = "";
	MdoWebResult aHits[MDO_WEB_RESULTS_MAX];
	size_t nHits = 0, i, o;
	xjsonwriter* pW = NULL;
	xwork_result eResult = XWORK_RESULT_ERROR;
	MdoWebSet* pSlot;
	char* sJson = NULL;

	(void)pUserData; (void)pContext;
	if ( !sArgumentsJson ) return MdoWebFail(pOutput, "invalid arguments");
	tArgs = xrtJsonParse(xrtStrView(sArgumentsJson));
	if ( tArgs == NULL || xrtValueType(tArgs) != XVALUE_OBJECT ) {
		if ( tArgs != NULL ) xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "invalid arguments: expected a JSON object");
	}
	sQuery = MdoWebArgText(tArgs, "query");
	if ( sQuery == NULL || sQuery[0] == 0 ) {
		free(sQuery); xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "query is required");
	}

	sQEnc = (char*)malloc(strlen(sQuery) * 3u + 1u);
	if ( sQEnc == NULL ) { free(sQuery); xrtValueRelease(tArgs); return MdoWebFail(pOutput, "out of memory"); }
	o = 0;
	for ( i = 0; sQuery[i] != 0; i++ ) {
		unsigned char c = (unsigned char)sQuery[i];
		if ( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') )
			sQEnc[o++] = (char)c;
		else if ( c == ' ' ) sQEnc[o++] = '+';
		else o += (size_t)snprintf(sQEnc + o, 4, "%%%02X", c);
	}
	sQEnc[o] = 0;
	snprintf(sUrl, sizeof(sUrl), "https://cn.bing.com/search?q=%s&count=10", sQEnc);

	sHtml = MdoWebFetch(sUrl, sErr, sizeof(sErr));
	if ( sHtml == NULL ) { eResult = MdoWebFail(pOutput, sErr); goto cleanup; }

	fprintf(stderr, "[web] fetching bing...\n");
	memset(aHits, 0, sizeof(aHits));
	fprintf(stderr, "[web] fetched %zu bytes, parsing...\n", strlen(sHtml));
	nHits = MdoWebParseBing(sHtml, aHits, MDO_WEB_RESULTS_MAX);
	free(sHtml); sHtml = NULL;
	if ( nHits == 0 ) { eResult = MdoWebFail(pOutput, "no results returned"); goto cleanup; }

	MdoWebInit();
	xrtMutexLock(g_web.pLock);
	fprintf(stderr, "[web] parsed %zu hits, storing...\n", nHits);
	pSlot = &g_web.aSets[g_web.iNext];
	fprintf(stderr, "[web] A\n");
	MdoWebSetUnit(pSlot);
	fprintf(stderr, "[web] B\n");
	pSlot->uId = ++g_web.uNextId;
	fprintf(stderr, "[web] C\n");
	pSlot->sQuery = MdoWebStrDupN(sQuery, strlen(sQuery));
	fprintf(stderr, "[web] D\n");
	for ( i = 0; i < nHits; i++ ) pSlot->aResults[i] = aHits[i];
	pSlot->nResults = nHits;
	fprintf(stderr, "[web] E\n");
	g_web.iNext = (g_web.iNext + 1u) % MDO_WEB_SETS_MAX;
	xrtMutexUnlock(g_web.pLock);
	fprintf(stderr, "[web] F unlock\n");

	pW = JWOpen();
	if ( pW == NULL ) { eResult = MdoWebFail(pOutput, "encode failed"); goto cleanup; }
	xrtJsonWriterObject(pW);
	JWName(pW, "responseId");
	{ char aId[24]; snprintf(aId, sizeof(aId), "r%llu", (unsigned long long)pSlot->uId); JWStr(pW, aId); }
	JWName(pW, "results");
	xrtJsonWriterArray(pW);
	for ( i = 0; i < nHits; i++ ) {
		xrtJsonWriterObject(pW);
		JWName(pW, "i"); JWUInt(pW, i);
		JWName(pW, "title"); JWStr(pW, aHits[i].sTitle ? aHits[i].sTitle : "");
		JWName(pW, "url");   JWStr(pW, aHits[i].sUrl);
		JWName(pW, "snippet"); JWStr(pW, aHits[i].sSnippet ? aHits[i].sSnippet : "");
		xrtJsonWriterEnd(pW);
	}
	xrtJsonWriterEnd(pW);
	JWName(pW, "hint");
	JWStr(pW, "use get_search_content with responseId + index to read a result");
	xrtJsonWriterEnd(pW);
	sJson = JWTake(pW);
	xworkToolOutputSet(pOutput, true, sJson ? sJson : "{}");
	if ( sJson != NULL ) xrtFree(sJson);
	eResult = XWORK_RESULT_OK;

cleanup:
	/* 注意：aHits 的所有权已移交结果集槽位（浅拷贝入表），此处不得释放，
	 * 否则下次 MdoWebSetUnit 释放槽位时双重释放崩溃 */
	free(sHtml); free(sQEnc); free(sQuery);
	xrtValueRelease(tArgs);
	return eResult;
}

/* ---------------- get_search_content ---------------- */

static xwork_result MdoWebToolGetContent(void* pUserData, const xwork_tool_context* pContext,
	const char* sArgumentsJson, xwork_tool_output* pOutput, xwork_error* pError)
{
	xvalue* tArgs = NULL;
	char* sRid = NULL;
	char* sUrl = NULL;
	char* sText = NULL;
	char* sOwn = NULL;
	char* sFind = NULL;
	int64 iIndex = -1;
	uint64 uRid = 0;
	MdoWebSet* pSet = NULL;
	size_t i, k;
	const char* pAt;
	xjsonwriter* pW = NULL;
	char* sJson = NULL;
	xwork_result eResult = XWORK_RESULT_ERROR;
	xvalue* pV;

	(void)pUserData; (void)pContext;
	if ( !sArgumentsJson ) return MdoWebFail(pOutput, "invalid arguments");
	tArgs = xrtJsonParse(xrtStrView(sArgumentsJson));
	if ( tArgs == NULL || xrtValueType(tArgs) != XVALUE_OBJECT ) {
		if ( tArgs != NULL ) xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "invalid arguments: expected a JSON object");
	}
	sRid = MdoWebArgText(tArgs, "responseId");
	if ( sRid == NULL || sRid[0] != 'r' ) {
		xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "responseId is required (from web_search)");
	}
	uRid = (uint64)strtoul(sRid + 1, NULL, 10);
	iIndex = MdoWebArgInt(tArgs, "index", -1);
	{
		int64 iMaxV = MdoWebArgInt(tArgs, "max_chars", 4000);
		if ( iMaxV > 0 ) { /* iMax 用局部 */ }
	}
	sFind = MdoWebArgText(tArgs, "findText");

	MdoWebInit();
	xrtMutexLock(g_web.pLock);
	for ( i = 0; i < MDO_WEB_SETS_MAX; i++ )
		if ( g_web.aSets[i].uId == uRid ) { pSet = &g_web.aSets[i]; break; }
	if ( pSet == NULL ) {
		xrtMutexUnlock(g_web.pLock);
		free(sRid); free(sFind); xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "unknown responseId (expired or wrong)");
	}
	if ( iIndex < 0 || (size_t)iIndex >= pSet->nResults ) {
		xrtMutexUnlock(g_web.pLock);
		free(sRid); free(sFind); xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "index out of range");
	}
	sUrl = MdoWebStrDupN(pSet->aResults[iIndex].sUrl, strlen(pSet->aResults[iIndex].sUrl));
	xrtMutexUnlock(g_web.pLock);

	/* 缓存命中 → 直接用；否则抓取+转文本（锁外，可长至 15s） */
	xrtMutexLock(g_web.pLock);
	for ( k = 0; k < pSet->nPages; k++ )
		if ( pSet->aPageUrl[k] != NULL && strcmp(pSet->aPageUrl[k], sUrl) == 0 ) {
			sText = MdoWebStrDupN(pSet->aPageText[k], strlen(pSet->aPageText[k]));
			break;
		}
	xrtMutexUnlock(g_web.pLock);
	if ( sText == NULL ) {
		char sErr[160] = "";
		sText = MdoWebFetch(sUrl, sErr, sizeof(sErr));
		if ( sText == NULL ) { eResult = MdoWebFail(pOutput, sErr); goto cleanup; }
		sOwn = MdoWebHtmlToText(sText);
		free(sText);
		sText = sOwn;
		if ( sText == NULL ) { eResult = MdoWebFail(pOutput, "out of memory"); goto cleanup; }
		xrtMutexLock(g_web.pLock);
		if ( pSet->nPages < MDO_WEB_PAGES_MAX ) {
			pSet->aPageUrl[pSet->nPages] = MdoWebStrDupN(sUrl, strlen(sUrl));
			pSet->aPageText[pSet->nPages] = sText;   /* 所有权移交缓存 */
			if ( pSet->aPageUrl[pSet->nPages] != NULL ) { pSet->nPages++; sOwn = NULL; }
		}
		xrtMutexUnlock(g_web.pLock);
	}

	/* findText 定位 / 头部切片 */
	{
		const char* pHit = NULL;
		int64 iMax = MdoWebArgInt(tArgs, "max_chars", 4000);
		size_t iLen;
		bool bTrunc;
		pAt = sText;
		if ( sFind != NULL && sFind[0] != 0 ) {
			pHit = strstr(sText, sFind);
			if ( pHit != NULL ) pAt = pHit;
		}
		iLen = strlen(pAt);
		bTrunc = iLen > (size_t)iMax;
		if ( bTrunc ) iLen = (size_t)iMax;
		pW = JWOpen();
		if ( pW == NULL ) { eResult = MdoWebFail(pOutput, "encode failed"); goto cleanup; }
		xrtJsonWriterObject(pW);
		JWName(pW, "url"); JWStr(pW, sUrl);
		JWName(pW, "found"); JWBool(pW, (sFind == NULL || sFind[0] == 0) ? true : (pHit != NULL));
		JWName(pW, "text");
		xrtJsonWriterString(pW, xrtStrViewN(pAt, iLen));
		JWName(pW, "truncated"); JWBool(pW, bTrunc);
		JWName(pW, "total_chars"); JWUInt(pW, (uint64)strlen(pAt));
		xrtJsonWriterEnd(pW);
		sJson = JWTake(pW);
		xworkToolOutputSet(pOutput, true, sJson ? sJson : "{}");
		if ( sJson != NULL ) xrtFree(sJson);
		eResult = XWORK_RESULT_OK;
	}

cleanup:
	free(sText); free(sOwn); free(sUrl); free(sRid); free(sFind);
	xrtValueRelease(tArgs);
	return eResult;
}

/* ---------------- fetch_content ---------------- */

static xwork_result MdoWebToolFetch(void* pUserData, const xwork_tool_context* pContext,
	const char* sArgumentsJson, xwork_tool_output* pOutput, xwork_error* pError)
{
	xvalue* tArgs = NULL;
	char* sUrl = NULL;
	char* sHtml = NULL;
	char* sText = NULL;
	char sErr[160] = "";
	xjsonwriter* pW = NULL;
	char* sJson = NULL;
	xwork_result eResult = XWORK_RESULT_ERROR;

	(void)pUserData; (void)pContext;
	if ( !sArgumentsJson ) return MdoWebFail(pOutput, "invalid arguments");
	tArgs = xrtJsonParse(xrtStrView(sArgumentsJson));
	if ( tArgs == NULL || xrtValueType(tArgs) != XVALUE_OBJECT ) {
		if ( tArgs != NULL ) xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "invalid arguments: expected a JSON object");
	}
	sUrl = MdoWebArgText(tArgs, "url");
	if ( sUrl == NULL || sUrl[0] == 0 ) {
		xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "url is required");
	}

	sHtml = MdoWebFetch(sUrl, sErr, sizeof(sErr));
	if ( sHtml == NULL ) { eResult = MdoWebFail(pOutput, sErr); goto cleanup; }
	sText = MdoWebHtmlToText(sHtml);
	free(sHtml);
	if ( sText == NULL ) { eResult = MdoWebFail(pOutput, "out of memory"); goto cleanup; }
	{
		int64 iMax = MdoWebArgInt(tArgs, "max_chars", 6000);
		size_t iLen = strlen(sText);
		bool bTrunc = iLen > (size_t)iMax;
		if ( bTrunc ) iLen = (size_t)iMax;
		pW = JWOpen();
		if ( pW == NULL ) { eResult = MdoWebFail(pOutput, "encode failed"); goto cleanup; }
		xrtJsonWriterObject(pW);
		JWName(pW, "url"); JWStr(pW, sUrl);
		JWName(pW, "text");
		xrtJsonWriterString(pW, xrtStrViewN(sText, iLen));
		JWName(pW, "truncated"); JWBool(pW, bTrunc);
		JWName(pW, "total_chars"); JWUInt(pW, (uint64)strlen(sText));
		xrtJsonWriterEnd(pW);
		sJson = JWTake(pW);
		xworkToolOutputSet(pOutput, true, sJson ? sJson : "{}");
		if ( sJson != NULL ) xrtFree(sJson);
		eResult = XWORK_RESULT_OK;
	}

cleanup:
	free(sText); free(sHtml); free(sUrl);
	xrtValueRelease(tArgs);
	return eResult;
}

/* ---------------- 注册（mdo per-run agent） ---------------- */

static bool MdoWebRegisterTools(xwork_agent* pAgent, xwork_error* pError)
{
	static const xwork_tool_definition c_aTools[] = {
		{ "web_search",
		  "Search the public web for information beyond your knowledge cutoff or about current events. Returns titles, URLs, and snippets plus a responseId; use get_search_content to read a result.",
		  "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"minLength\":1},\"max_results\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10}},\"required\":[\"query\"],\"additionalProperties\":false}",
		  true, XWORK_TOOL_EFFECT_READ_ONLY, MdoWebToolSearch, NULL, NULL },
		{ "get_search_content",
		  "Read the full text of one web_search result by responseId and index; findText jumps straight to the relevant section. Prevents flooding context with whole pages.",
		  "{\"type\":\"object\",\"properties\":{\"responseId\":{\"type\":\"string\",\"pattern\":\"^r\\\\d+$\"},\"index\":{\"type\":\"integer\",\"minimum\":0},\"findText\":{\"type\":\"string\"},\"max_chars\":{\"type\":\"integer\",\"minimum\":200,\"maximum\":20000}},\"required\":[\"responseId\",\"index\"],\"additionalProperties\":false}",
		  true, XWORK_TOOL_EFFECT_READ_ONLY, MdoWebToolGetContent, NULL, NULL },
		{ "fetch_content",
		  "Fetch a known URL and return its readable text (HTML converted; JS-heavy or paywalled pages may come back empty). Use when you already know the address.",
		  "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\",\"minLength\":1},\"max_chars\":{\"type\":\"integer\",\"minimum\":200,\"maximum\":20000}},\"required\":[\"url\"],\"additionalProperties\":false}",
		  true, XWORK_TOOL_EFFECT_READ_ONLY, MdoWebToolFetch, NULL, NULL },
	};
	size_t i;
	if ( pAgent == NULL ) {
		xworkErrorInit(pError);
		snprintf(pError->sMessage, sizeof(pError->sMessage), "agent is null");
		return false;
	}
	for ( i = 0; i < sizeof(c_aTools) / sizeof(c_aTools[0]); i++ ) {
		xwork_tool_definition tTool = c_aTools[i];
		tTool.sSource = "mdo-web";
		if ( !xworkAgentRegisterTool(pAgent, &tTool, pError) ) return false;
	}
	return true;
}

#endif /* MDO_WEB_H */
