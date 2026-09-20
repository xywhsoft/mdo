/*
 * mdo (墨斗) — agent 工作台的 C 脚本后端
 *
 * 架构：前端静态文件由 xs 静态层服务（wwwroot/）；本脚本处理 /api/* 路由。
 * 进程内直调 xllm/xllm-session/xwork 三件套，无 HTTP 中间层。
 *
 * 路由总表：
 *   GET  /api/health
 *   POST /api/render                     {markdown} → {html}（md4c）
 *   GET  /api/models                     模型表（zcode 式配置）
 *   POST /api/models                     {model:{...}, default?} upsert
 *   POST /api/models/delete              {id}
 *   GET  /api/projects                   项目表 + 活动项目
 *   POST /api/projects                   {path} 注册并激活
 *   POST /api/projects/activate          {slug}
 *   GET  /api/sessions                   活动项目会话列表
 *   POST /api/sessions                   {title?} 新建会话
 *   POST /api/sessions/rename            {id, title}
 *   POST /api/sessions/delete            {id}
 *   GET  /api/sessions/<id>/events       UI 事件日志全量重放
 *   POST /api/prompt                     {sessionId, text, modelId?} → {turnId}
 *   GET  /api/turn/<id>/events?since=N   回合事件轮询
 *   POST /api/turn/<id>/approval         {id, decision}
 *   POST /api/turn/<id>/cancel
 */

#include "mdo_base.h"

/* 扩展头（xs 按启用扩展注入 XS_USE_* 宏） */
#ifdef XS_USE_XLLM
#include <xllm.h>
#endif
#ifdef XS_USE_XLLM_SESSION
#include <xllm-session.h>
#endif
#ifdef XS_USE_XWORK
#include <xwork.h>
#endif

#include "mdo_web.h"
#include "mdo_store.h"
#include "mdo_engine.h"
#include "mdo_sched.h"

/* ================================================================== */
/* 工作区文件枚举（@ 补全数据源；Codex 式相对路径列表）                    */
/* ================================================================== */

#define MDO_WS_MAX_DEPTH 5
#define MDO_WS_MAX_FILES 400

static const char* g_aWsIgnores[] = {
	".git", "node_modules", "dist", "build", "target", "__pycache__",
	".venv", "venv", "out", ".idea", ".vscode", ".mdo", "data", NULL
};

typedef struct {
	MdoBuf tBuf;
	char aQuery[128];
	size_t nCount;
	bool bFirst;
} MdoWsWalk;

static bool MdoWsIgnoreDir(const char* sName)
{
	size_t i;
	for ( i = 0; g_aWsIgnores[i] != NULL; i++ )
		if ( strcmp(sName, g_aWsIgnores[i]) == 0 ) return true;
	return false;
}

/* 大小写不敏感子串匹配（q 已小写化；目标逐字符小写比较） */
static bool MdoWsMatch(const char* sRel, const char* sQ)
{
	size_t i, j;

	if ( sQ[0] == 0 ) return true;
	for ( i = 0; sRel[i] != 0; i++ ) {
		for ( j = 0; sQ[j] != 0; j++ ) {
			char a = sRel[i + j], b = sQ[j];
			if ( a == 0 ) return false;
			if ( a >= 'A' && a <= 'Z' ) a += 32;
			if ( a != b ) break;
		}
		if ( sQ[j] == 0 ) return true;
	}
	return false;
}

static void MdoWsWalkDir(MdoWsWalk* pW, const char* sAbsDir, char* sRel,
	size_t iRelLen, int iDepth)
{
	xdir hDir = xrtDirOpen(sAbsDir, XDIR_STAT);
	xdirentry tEntry;
	/* 两遍式：先收本层文件（浅层命中在前），再递归子目录（深树不挤掉根层结果） */
	int iPass;

	if ( hDir == NULL ) return;
	for ( iPass = 0; iPass < 2 && pW->nCount < MDO_WS_MAX_FILES; iPass++ ) {
		while ( xrtDirNext(hDir, &tEntry) == XDIR_NEXT_ITEM ) {
			bool bDir = tEntry.Info.Type == XFILE_TYPE_DIRECTORY;
			const char* sName = tEntry.Name.Data;

			if ( pW->nCount >= MDO_WS_MAX_FILES ) break;
			if ( sName[0] == '.' && strcmp(sName, ".gitignore") != 0 ) continue;
			if ( bDir != (iPass == 1) ) continue;
			if ( bDir ) {
				if ( MdoWsIgnoreDir(sName) || iDepth + 1 >= MDO_WS_MAX_DEPTH ) continue;
				{
					char aChild[512];
					size_t n = snprintf(sRel + iRelLen, 260 - iRelLen, "%s/", sName);
					snprintf(aChild, sizeof(aChild), "%s/%s", sAbsDir, sName);
					MdoWsWalkDir(pW, aChild, sRel, iRelLen + n, iDepth + 1);
					sRel[iRelLen] = 0;    /* 递归改写后截断还原 */
				}
			} else {
				snprintf(sRel + iRelLen, 260 - iRelLen, "%s", sName);
				if ( MdoWsMatch(sRel, pW->aQuery) ) {
					if ( !pW->bFirst ) MdoBufAppend(&pW->tBuf, ",", 1);
					BufJsonStr(&pW->tBuf, sRel);
					pW->bFirst = false;
					pW->nCount++;
				}
				sRel[iRelLen] = 0;
			}
		}
		/* 重开枚举器走第二遍（单向游标不可回退） */
		xrtDirClose(hDir);
		if ( iPass == 0 ) {
			hDir = xrtDirOpen(sAbsDir, XDIR_STAT);
			if ( hDir == NULL ) return;
		}
	}
	xrtDirClose(hDir);
}

/* ================================================================== */
/* 会话的 xllm 载入（惰性 Recover；含模型客户端绑定）                      */
/* ================================================================== */

static bool MdoSessionEnsure(MdoSession* pSess, MdoProject* pProj, MdoModel* pModel, char* pErr, size_t iCap)
{
	xllm_error tErr;
	char aJournal[380], aSnap[380];

	if ( pSess->bLoaded && pSess->pSession != NULL ) {
		xllmSessionBindClient(pSess->pSession, pModel->pClient);
		return true;
	}

	xllmErrorInit(&tErr);
	/* 属主桶优先（aProject 空=历史会话，按 ID 全桶定位一次并补记） */
	if ( pProj == NULL ) {
		size_t k;
		for ( k = 0; k < g_nProjects; k++ )
			if ( strcmp(g_projects[k].aSlug, pSess->aProject) == 0 ) { pProj = &g_projects[k]; break; }
		if ( pProj == NULL && pSess->aProject[0] == 0 ) {
			pProj = MdoSessionLocate(pSess->aId, NULL, 0);
			snprintf(pSess->aProject, sizeof(pSess->aProject), "%s",
				pProj ? pProj->aSlug : MDO_TASKS_SLUG);
		}
	}
	MdoSessionPaths(pProj, pSess->aId, NULL, 0, aJournal, sizeof(aJournal),
		aSnap, sizeof(aSnap), NULL, 0);
	{
		xllm_session_config tCfg;
		xllmSessionConfigInit(&tCfg);
		tCfg.uContextWindowTokens = pSess->uContextWindow;
		/* 拒绝线 = 动态预算 +1KB：恒大于 xwork 截断产物（预算+约150B 标记），超限不再炸回合 */
		tCfg.uToolResultCapBytes = (uint32_t)(MdoToolBudgetBytes(pSess->uContextWindow) + 1024u);
		tCfg.uMaxOutputTokens = pModel->uMaxOutput
			? pModel->uMaxOutput
			: (uint32_t)(pSess->uContextWindow / 4u);
		pSess->pSession = xllmSessionRecover(aSnap, aJournal, &tCfg, &tErr);
	}
	if ( pSess->pSession == NULL ) {
		snprintf(pErr, iCap, "session recover failed: %s",
			tErr.sMessage[0] ? tErr.sMessage : "?");
		return false;
	}
	xllmSessionBindClient(pSess->pSession, pModel->pClient);
	/* 思考力度注入钩子（pUserData=会话，指针数组稳定） */
	memset(&pSess->tHooks, 0, sizeof(pSess->tHooks));
	pSess->tHooks.pRenderComplete = MdoEffortRenderComplete;
	pSess->tHooks.pUserData = pSess;
	xllmSessionSetHooks(pSess->pSession, &pSess->tHooks);
	pSess->bLoaded = true;
	return true;
}

/* ================================================================== */
/* 路由辅助                                                             */
/* ================================================================== */

/* 解析 /api/<seg1>/<seg2>/... 形态的段；返回段数 */
static int SplitPath(char* pPath, char** pSegs, int iMax)
{
	int n = 0;
	char* p = pPath;
	pSegs[n++] = p;
	while ( *p && n < iMax ) {
		if ( *p == '/' ) {
			*p = 0;
			pSegs[n++] = p + 1;
		}
		p++;
	}
	return n;
}

/* ================================================================== */
/* RequestProc                                                          */
/* ================================================================== */

/* ---------------- /i18n-data.js：语言包（xvalue 装载 + xrtTemplate 渲染） ----------------
 * 语言包 JSON 解析为 xvalue（校验 + 渲染数据根），经模板渲染为 ES 模块；
 * 前端切换语言即重新 import 本路由产物，免重启。 */
static const char* const c_sI18nTpl =
	"// mdo i18n-data: rendered by xrtTemplate (GET /i18n-data.js?lang=xx)\n"
	"export const lang = '{$lang}';\n"
	"export const pack = {$pack};\n";

static char g_wwwRoot[300] = "";   /* 资源根（语言包定位） */
static char* g_aI18nPack[3] = { NULL, NULL, NULL };
static xtemplate* g_pI18nTpl = NULL;

static bool MdoI18nDataReply(XS_HttpReq* pReq)
{
	static const char* const aLangs[3] = { "zh", "en", "ru" };
	char aLang[8] = "zh";
	char aFile[380];
	char* sPack;
	str sRendered = NULL;
	MdoJson tJ, tRoot;
	xjsonwriter* pW;
	char* sRootText;
	size_t iSize = 0, iOut = 0;
	int iLang = 0, k;
	bool bOk = false;
	{
		xstrview tQ = pReq->head->Target;
		const char* pQM = memchr(tQ.Data, '?', tQ.Size);
		if ( pQM != NULL ) {
			const char* pP = MdoFindBounded(pQM, tQ.Data + tQ.Size, "lang=");
			if ( pP != NULL ) {
				size_t n = 0;
				pP += 5;
				while ( pP + n < tQ.Data + tQ.Size && pP[n] != '&' &&
				        n < sizeof(aLang) - 1 ) { aLang[n] = pP[n]; n++; }
				aLang[n] = 0;
			}
		}
	}
	for ( k = 0; k < 3; k++ ) if ( strcmp(aLang, aLangs[k]) == 0 ) { iLang = k; break; }
	if ( k == 3 ) iLang = 0;

	if ( g_pI18nTpl == NULL )
		g_pI18nTpl = xrtTemplateCompile(xrtStrView(c_sI18nTpl));
	if ( g_pI18nTpl == NULL ) return ReplyErr(pReq, 500, "template compile failed");

	if ( g_aI18nPack[iLang] == NULL ) {
		char* pDir = (g_wwwRoot[0] ? MdoPathJoin(g_wwwRoot, "lang") : xrtStrDup("lang"));
		char* pFile = pDir ? MdoPathJoin(pDir, aLangs[iLang]) : NULL;
		if ( pFile == NULL ) { xrtFree(pDir); return ReplyErr(pReq, 500, "lang path failed"); }
		snprintf(aFile, sizeof(aFile), "%s.json", pFile);
		xrtFree(pFile); xrtFree(pDir);
		sPack = xrtFileReadText(aFile, XENCODING_UTF8, XUTF_REPLACE, &iSize);
		if ( sPack == NULL || !MdoJsonParse(&tJ, sPack ? sPack : "") ) {
			xrtFree(sPack); MdoJsonFree(&tJ);
			return ReplyErr(pReq, 500, "lang pack missing/invalid");
		}
		MdoJsonFree(&tJ);   /* 校验通过：语言包即 xvalue 数据源 */
		g_aI18nPack[iLang] = sPack;
	}

	pW = JWOpen();
	if ( pW == NULL ) return ReplyErr(pReq, 500, "encode failed");
	xrtJsonWriterObject(pW);
	JWName(pW, "lang"); JWStr(pW, aLangs[iLang]);
	JWName(pW, "pack"); JWStr(pW, g_aI18nPack[iLang]);
	xrtJsonWriterEnd(pW);
	sRootText = JWTake(pW);
	bOk = sRootText != NULL && MdoJsonParse(&tRoot, sRootText);
	xrtFree(sRootText);
	if ( !bOk ) { MdoJsonFree(&tRoot); return ReplyErr(pReq, 500, "root build failed"); }
	sRendered = xrtTemplateRender(g_pI18nTpl, tRoot.pRoot, &iOut);
	MdoJsonFree(&tRoot);
	if ( sRendered == NULL ) return ReplyErr(pReq, 500, "render failed");
	{
		char aHead[384]; char aLen[24];
		xhttpfield aF[3]; size_t nF = 0, iHead = 0;
		snprintf(aLen, sizeof(aLen), "%llu", (unsigned long long)strlen(sRendered));
		aF[nF].Name = XRT_STR_LITERAL("Content-Length"); aF[nF].Value = xrtStrView(aLen); nF++;
		aF[nF].Name = XRT_STR_LITERAL("Content-Type");
		aF[nF].Value = xrtStrView("text/javascript; charset=utf-8"); nF++;
		aF[nF].Name = XRT_STR_LITERAL("Cache-Control");
		aF[nF].Value = xrtStrView("no-store"); nF++;
		bOk = xrtHttp1ResponseWrite(XHTTP_VERSION_1_1, 200,
			xrtHttpStatusText(200), aF, nF, aHead, sizeof(aHead), &iHead) &&
			ConnSend(pReq, aHead, iHead) &&
			(pReq->head->MethodCode == XHTTP_METHOD_HEAD ||
			 ConnSend(pReq, sRendered, strlen(sRendered)));
	}
	xrtFree(sRendered);
	return bOk;
}

/* ---------------- GET /api/feedback —— 反馈记录聚合（内存表全部会话，跨桶） ---------------- */
static char* MdoFeedbackCollect(void)
{
	xjsonwriter* pW = JWOpen();
	size_t i;
	if ( pW == NULL ) return NULL;
	xrtJsonWriterObject(pW);
	JWName(pW, "ok"); JWBool(pW, true);
	JWName(pW, "items");
	xrtJsonWriterArray(pW);
	xrtMutexLock(g_lock);
	/* 去激活化：聚合内存表所有会话（各桶会话随属主加载进内存表），不再按活动桶过滤 */
	for ( i = 0; i < g_nSessions; i++ ) {
		MdoSession* pS = g_sessions[i];
		size_t nLines = 0;
		char* sLog = MdoUiReadAll(pS, &nLines);
		char* pLine;
		if ( sLog == NULL ) continue;
		for ( pLine = sLog; *pLine; ) {
			char* pNL = strchr(pLine, '\n');
			MdoJson tJ;
			if ( pNL != NULL ) *pNL = 0;
			if ( strstr(pLine, "\"feedback/set\"") != NULL &&
			     MdoJsonParse(&tJ, pLine) ) {
				xvalue* pData = JObj(&tJ, "data");
				char aNode[80] = "";
				char aValue[12] = "";
				int64 iTime = 0, iSeq = 0;
				if ( pData != NULL ) {
					JStrIn(pData, "nodeId", aNode, sizeof(aNode));
					JStrIn(pData, "value", aValue, sizeof(aValue));
				}
				iTime = JInt(&tJ, "time", 0);
				iSeq = JInt(&tJ, "seq", 0);
				if ( aValue[0] ) {
					xrtJsonWriterObject(pW);
					JWName(pW, "sessionId"); JWStr(pW, pS->aId);
					JWName(pW, "title");     JWStr(pW, pS->aTitle);
					JWName(pW, "nodeId");    JWStr(pW, aNode);
					JWName(pW, "value");     JWStr(pW, aValue);
					JWName(pW, "time");      xrtJsonWriterUInt(pW, (uint64)(iTime > 0 ? iTime : iSeq));
					xrtJsonWriterEnd(pW);
				}
				MdoJsonFree(&tJ);
			}
			if ( pNL == NULL ) break;
			pLine = pNL + 1;
		}
		xrtFree(sLog);
	}
	xrtMutexUnlock(g_lock);
	xrtJsonWriterEnd(pW);
	xrtJsonWriterEnd(pW);
	return JWTake(pW);
}

int RequestProc(XS_HttpReq* pReq)
{
	char aPath[256];
	char* aSeg[6] = {0};   /* 未初始化=栈垃圾：单段路径读 aSeg[1] 曾致 404/崩溃 */
	int nSeg;
	size_t iBodyLen = 0;
	char* sBody = NULL;
	MdoJson tJ;

	if ( pReq == NULL || pReq->head == NULL ) return XS_FALLBACK;

	{
		xstrview tUri = pReq->head->Target;
		size_t nUri = tUri.Size;
		const char* pQ = tUri.Size > 0 ? memchr(tUri.Data, '?', tUri.Size) : NULL;
		if ( pQ != NULL ) nUri = (size_t)(pQ - tUri.Data);
		if ( nUri >= sizeof(aPath) ) return XS_FALLBACK;
		if ( nUri > 0 ) memcpy(aPath, tUri.Data, nUri);
		aPath[nUri] = 0;
	}
	/* 语言包模块（xrtTemplate 渲染；静态目录之外的动态产物） */
	/* ask_user 作答（全局 askId 查找；D2 式跨会话一致） */
	if ( strcmp(aPath, "/api/ask") == 0 && pReq->head->MethodCode == XHTTP_METHOD_POST ) {
		char aId[80], aAnswer[1024];
		MdoRun* pAskRun = NULL;
		size_t r;

		{
			size_t iBodyLen = 0;
			char* sBody = ReqBody(pReq, &iBodyLen);
			MdoJson tJ;
			if ( sBody == NULL || !MdoJsonParse(&tJ, sBody) ) {
				xrtFree(sBody);
				return ReplyErr(pReq, 400, "invalid json body") ? XS_OK : XS_OK;
			}
			JStr(&tJ, "id", aId, sizeof(aId));
			JStr(&tJ, "answer", aAnswer, sizeof(aAnswer));
			MdoJsonFree(&tJ);
			xrtFree(sBody);
		}
		fprintf(stderr, "[ask] route enter id=%s\n", aId);
		xrtMutexLock(g_lock);
		for ( r = 0; r < g_nRuns; r++ ) {
			if ( g_runs[r] != NULL &&
			     strcmp(g_runs[r]->aAskId, aId) == 0 ) { pAskRun = g_runs[r]; break; }
		}
		xrtMutexUnlock(g_lock);
		if ( pAskRun == NULL )
			return ReplyErr(pReq, 404, "ask not found") ? XS_OK : XS_OK;
		xrtMutexLock(pAskRun->pALock);
		if ( pAskRun->iAskState < 0 ) {
			snprintf(pAskRun->aAskAnswer, sizeof(pAskRun->aAskAnswer), "%s", aAnswer);
			pAskRun->iAskState = 1;
			xrtCondBroadcast(pAskRun->pACond);
			fprintf(stderr, "[ask] answered + broadcast\n");
		}
		xrtMutexUnlock(pAskRun->pALock);
		return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
	}
	/* 反馈聚合（D3b：数据管理旁的数据视图） */
	if ( strcmp(aPath, "/api/feedback") == 0 && pReq->head->MethodCode == XHTTP_METHOD_GET ) {
		char* sJson = MdoFeedbackCollect();
		if ( sJson == NULL )
			return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
		{
			bool bOk = ReplyJSON(pReq, 200, sJson);
			xrtFree(sJson);
			return bOk ? XS_OK : XS_OK;
		}
	}
	if ( strcmp(aPath, "/i18n-data.js") == 0 && pReq->head->MethodCode == XHTTP_METHOD_GET )
		return MdoI18nDataReply(pReq) ? XS_OK : XS_OK;
	if ( strncmp(aPath, "/api/", 5) != 0 ) return XS_FALLBACK;
	nSeg = SplitPath(aPath + 5, aSeg, 6);    /* 跳过 "api/" */
	(void)nSeg;

#define MDO_METHOD(m)  (pReq->head->MethodCode == XHTTP_METHOD_##m)
#define NEED_BODY() \
	do { \
		sBody = ReqBody(pReq, &iBodyLen); \
		if ( sBody == NULL || !MdoJsonParse(&tJ, sBody) ) { \
			xrtFree(sBody); \
			return ReplyErr(pReq, 400, "invalid json body") ? XS_OK : XS_OK; \
		} \
	} while ( 0 )
#define BODY_DONE()  do { MdoJsonFree(&tJ); xrtFree(sBody); } while ( 0 )

	/* ---------------- 设置（服务端权威持久化） ---------------- */
	if ( strcmp(aSeg[0], "settings") == 0 && aSeg[1] == NULL ) {
		if ( MDO_METHOD(GET) ) {
			xjsonwriter* pW = JWOpen();
			char* sJson = NULL;
			if ( pW != NULL ) {
				xrtJsonWriterObject(pW);
				JWName(pW, "ok"); JWBool(pW, true);
				JWName(pW, "settings");
				xrtJsonWriterObject(pW);
				xrtMutexLock(g_lock);
				JWName(pW, "theme"); JWStr(pW, g_settings.aTheme);
				JWName(pW, "lang"); JWStr(pW, g_settings.aLang);
				JWName(pW, "interactMode"); JWStr(pW, g_settings.aInteractMode);
				JWName(pW, "fontSize"); JWStr(pW, g_settings.aFontSize);
				JWName(pW, "sound"); JWBool(pW, g_settings.bSound);
				JWName(pW, "autoApprove"); JWBool(pW, g_settings.bAutoApprove);
				JWName(pW, "systemPrompt"); JWStr(pW, g_settings.aSystemPrompt);
				JWName(pW, "proxyEnabled"); JWBool(pW, g_settings.bProxyEnabled);
				JWName(pW, "proxyHost"); JWStr(pW, g_settings.aProxyHost);
				JWName(pW, "proxyPort"); JWUInt(pW, g_settings.uProxyPort);
				JWName(pW, "proxyUser"); JWStr(pW, g_settings.aProxyUser);
				JWName(pW, "proxyPass"); JWStr(pW, g_settings.aProxyPass);
				JWName(pW, "proxyBypass"); JWStr(pW, g_settings.aProxyBypass);
				JWName(pW, "caCertPath"); JWStr(pW, g_settings.aCaCertPath);
				JWName(pW, "preventSleep"); JWBool(pW, g_settings.bPreventSleep);
				JWName(pW, "winX"); JWInt(pW, g_settings.iWinX);
				JWName(pW, "winY"); JWInt(pW, g_settings.iWinY);
				JWName(pW, "winW"); JWInt(pW, g_settings.iWinW);
				JWName(pW, "winH"); JWInt(pW, g_settings.iWinH);
				JWName(pW, "winMax"); JWBool(pW, g_settings.bWinMax);
				xrtMutexUnlock(g_lock);
				xrtJsonWriterEnd(pW);
				xrtJsonWriterEnd(pW);
				sJson = JWTake(pW);
			}
			if ( sJson == NULL )
				return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
			{
				bool bOk = ReplyJSON(pReq, 200, sJson);
				xrtFree(sJson);
				return bOk ? XS_OK : XS_OK;
			}
		}
		if ( MDO_METHOD(POST) ) {
			char aTheme[12] = {0}, aFont[8] = {0}, aSys[2048] = {0}, aLangOut[8] = {0}, aInteractOut[8] = {0};
			char aProxyHost[200] = {0}, aProxyUser[120] = {0}, aProxyPass[120] = {0};
			char aBypass[600] = {0}, aCa[280] = {0};
			bool bSound = false, bApprove = false, bProxy = false, bSleep = false, bWinMax = false;
			bool bHasSound = false, bHasApprove = false, bHasProxy = false, bHasSleep = false;
			int64 iPort = 0, iX = 0, iY = 0, iW = 0, iH = 0;
			bool bCaChanged = false, bSleepChanged = false;

			NEED_BODY();
			{
				xvalue* pS = JObj(&tJ, "settings");
				bool b = false;
				int64 iV = 0;
				if ( pS == NULL )
					{ BODY_DONE(); return ReplyErr(pReq, 400, "missing settings") ? XS_OK : XS_OK; }
				JStrIn(pS, "theme", aTheme, sizeof(aTheme));
			JStrIn(pS, "lang", aLangOut, sizeof(aLangOut));
			JStrIn(pS, "interactMode", aInteractOut, sizeof(aInteractOut));
				JStrIn(pS, "fontSize", aFont, sizeof(aFont));
				JStrIn(pS, "systemPrompt", aSys, sizeof(aSys));
				JStrIn(pS, "proxyHost", aProxyHost, sizeof(aProxyHost));
				JStrIn(pS, "proxyUser", aProxyUser, sizeof(aProxyUser));
				JStrIn(pS, "proxyPass", aProxyPass, sizeof(aProxyPass));
				JStrIn(pS, "proxyBypass", aBypass, sizeof(aBypass));
				JStrIn(pS, "caCertPath", aCa, sizeof(aCa));
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("sound")), &b) )
					{ bSound = b; bHasSound = true; }
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("autoApprove")), &b) )
					{ bApprove = b; bHasApprove = true; }
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("proxyEnabled")), &b) )
					{ bProxy = b; bHasProxy = true; }
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("preventSleep")), &b) )
					{ bSleep = b; bHasSleep = true; }
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("winMax")), &b) )
					bWinMax = b;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("proxyPort")), &iV) )
					iPort = iV;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("winX")), &iV) ) iX = iV;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("winY")), &iV) ) iY = iV;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("winW")), &iV) ) iW = iV;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("winH")), &iV) ) iH = iV;
			}
			BODY_DONE();
			xrtMutexLock(g_lock);
			if ( aTheme[0] ) snprintf(g_settings.aTheme, sizeof(g_settings.aTheme), "%s", aTheme);
			if ( aFont[0] ) snprintf(g_settings.aFontSize, sizeof(g_settings.aFontSize), "%s", aFont);
			if ( aLangOut[0] ) snprintf(g_settings.aLang, sizeof(g_settings.aLang), "%s", aLangOut);
			if ( aInteractOut[0] ) snprintf(g_settings.aInteractMode, sizeof(g_settings.aInteractMode), "%s", aInteractOut);
			snprintf(g_settings.aSystemPrompt, sizeof(g_settings.aSystemPrompt), "%s", aSys);
			snprintf(g_settings.aProxyHost, sizeof(g_settings.aProxyHost), "%s", aProxyHost);
			snprintf(g_settings.aProxyUser, sizeof(g_settings.aProxyUser), "%s", aProxyUser);
			snprintf(g_settings.aProxyPass, sizeof(g_settings.aProxyPass), "%s", aProxyPass);
			snprintf(g_settings.aProxyBypass, sizeof(g_settings.aProxyBypass), "%s", aBypass);
			if ( strcmp(g_settings.aCaCertPath, aCa) != 0 ) {
				snprintf(g_settings.aCaCertPath, sizeof(g_settings.aCaCertPath), "%s", aCa);
				bCaChanged = true;
			}
			if ( bHasSound ) g_settings.bSound = bSound;
			if ( bHasApprove ) g_settings.bAutoApprove = bApprove;
			if ( bHasProxy ) g_settings.bProxyEnabled = bProxy;
			if ( bHasSleep ) {
				if ( g_settings.bPreventSleep != bSleep ) bSleepChanged = true;
				g_settings.bPreventSleep = bSleep;
			}
			if ( iPort > 0 && iPort <= 65535 ) g_settings.uProxyPort = (uint32)iPort;
			g_settings.iWinX = (int)iX; g_settings.iWinY = (int)iY;
			g_settings.iWinW = (int)iW; g_settings.iWinH = (int)iH;
			g_settings.bWinMax = bWinMax;
			MdoConfigSaveLocked();
			xrtMutexUnlock(g_lock);
			if ( bCaChanged ) MdoCaInvalidate();
			if ( bSleepChanged ) MdoApplyPreventSleep(g_settings.bPreventSleep);
			return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
		}
	}

	/* ---------------- 健康检查 ---------------- */
	if ( strcmp(aSeg[0], "health") == 0 && MDO_METHOD(GET) ) {
		return ReplyJSON(pReq, 200, "{\"ok\":true,\"name\":\"mdo\",\"version\":\"0.2\"}")
			? XS_OK : XS_OK;
	}

	/* ---------------- Markdown 渲染 ---------------- */
	if ( strcmp(aSeg[0], "render") == 0 && MDO_METHOD(POST) ) {
		static char aOut[192 * 1024];
		char aText[16384] = {0};
		char* sHTML;
		size_t iPos;

		NEED_BODY();
		JStr(&tJ, "markdown", aText, sizeof(aText));
		BODY_DONE();
		if ( aText[0] == 0 )
			return ReplyErr(pReq, 400, "missing markdown") ? XS_OK : XS_OK;
		sHTML = MdoRenderHTML(aText, strlen(aText));
		if ( sHTML == NULL )
			return ReplyErr(pReq, 500, "render failed") ? XS_OK : XS_OK;
		/* 手工拼 JSON：HTML 内含引号需转义——走 writer */
		{
			xjsonwriter* pW = JWOpen();
			char* sJson = NULL;
			if ( pW != NULL ) {
				xrtJsonWriterObject(pW);
				JWName(pW, "ok"); JWBool(pW, true);
				JWName(pW, "html"); JWStr(pW, sHTML);
				xrtJsonWriterEnd(pW);
				sJson = JWTake(pW);
			}
			xrtFree(sHTML);
			if ( sJson == NULL )
				return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
			iPos = strlen(sJson);
			if ( iPos < sizeof(aOut) ) {
				memcpy(aOut, sJson, iPos + 1);
				xrtFree(sJson);
				return ReplyJSON(pReq, 200, aOut) ? XS_OK : XS_OK;
			}
			{
				bool bOk = ReplyJSON(pReq, 200, sJson);
				xrtFree(sJson);
				return bOk ? XS_OK : XS_OK;
			}
		}
	}

	/* ---------------- 模型管理 ---------------- */
	if ( strcmp(aSeg[0], "models") == 0 && aSeg[1] == NULL ) {
		if ( MDO_METHOD(GET) ) {
			xjsonwriter* pW = JWOpen();
			char* sJson = NULL;
			size_t i;
			if ( pW != NULL ) {
				xrtJsonWriterObject(pW);
				JWName(pW, "ok"); JWBool(pW, true);
				JWName(pW, "defaultModel"); JWStr(pW, g_defaultModel);
				JWName(pW, "models");
				xrtJsonWriterArray(pW);
				xrtMutexLock(g_lock);
				for ( i = 0; i < g_nModels; i++ ) {
					const MdoModel* pM = &g_models[i];
					xrtJsonWriterObject(pW);
					JWName(pW, "id"); JWStr(pW, pM->aId);
					JWName(pW, "name"); JWStr(pW, pM->aName);
					JWName(pW, "baseUrl"); JWStr(pW, pM->aBaseUrl);
					if ( strcmp(pM->aId, MDO_BUILTIN_MODEL_ID) == 0 )
						{ JWName(pW, "apiKey"); JWStr(pW, ""); }   /* 内置 key 不下发前端 */
					else
						{ JWName(pW, "apiKey"); JWStr(pW, pM->aApiKey); }
					JWName(pW, "model"); JWStr(pW, pM->aModel);
					JWName(pW, "dialect"); JWStr(pW, pM->aDialect);
					JWName(pW, "reasoning"); JWStr(pW, pM->aReasoning);
					JWName(pW, "caPem"); JWStr(pW, pM->aCaPem);
					JWName(pW, "contextWindow"); xrtJsonWriterUInt(pW, pM->uContextWindow);
					JWName(pW, "maxOutput"); xrtJsonWriterUInt(pW, pM->uMaxOutput);
					if ( strcmp(pM->aId, MDO_BUILTIN_MODEL_ID) == 0 ) {
						JWName(pW, "builtin"); JWBool(pW, true);
					}
					xrtJsonWriterEnd(pW);
				}
				xrtMutexUnlock(g_lock);
				xrtJsonWriterEnd(pW);
				xrtJsonWriterEnd(pW);
				sJson = JWTake(pW);
			}
			if ( sJson == NULL )
				return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
			{
				bool bOk = ReplyJSON(pReq, 200, sJson);
				xrtFree(sJson);
				return bOk ? XS_OK : XS_OK;
			}
		}
		if ( MDO_METHOD(POST) ) {
			xvalue* pModelObj;
			char aId[64];
			MdoModel* pExisting;
			char aErr[128];

			NEED_BODY();
			pModelObj = JObj(&tJ, "model");
			if ( pModelObj == NULL ||
			     !JStrIn(pModelObj, "id", aId, sizeof(aId)) ) {
				BODY_DONE();
				return ReplyErr(pReq, 400, "missing model.id") ? XS_OK : XS_OK;
			}
			if ( aSeg[1] != NULL ) { BODY_DONE();
				return ReplyErr(pReq, 404, "not found") ? XS_OK : XS_OK; }
			xrtMutexLock(g_lock);
			pExisting = MdoModelFind(aId);
			if ( pExisting != NULL && strcmp(aId, MDO_BUILTIN_MODEL_ID) == 0 ) {
				/* 内置模型只读：key 在程序内；default 语义仍走下方通用段 */
				;
			}
			else if ( pExisting != NULL ) {
				/* 更新：先弃客户端（配置可能变了） */
				MdoModelDropClient(pExisting);
				xrtFree(pExisting->sCaPemText);
				pExisting->sCaPemText = NULL;
				MdoModelFromJson(pExisting, pModelObj);
			} else if ( g_nModels < MDO_MAX_MODELS ) {
				MdoModel* pM = &g_models[g_nModels++];
				memset(pM, 0, sizeof(*pM));
				MdoModelFromJson(pM, pModelObj);
			} else {
				xrtMutexUnlock(g_lock);
				BODY_DONE();
				snprintf(aErr, sizeof(aErr), "model table full");
				return ReplyErr(pReq, 400, aErr) ? XS_OK : XS_OK;
			}
			{
				char aDefault[64];
				if ( JStr(&tJ, "default", aDefault, sizeof(aDefault)) )
					snprintf(g_defaultModel, sizeof(g_defaultModel), "%s", aDefault);
				else if ( g_defaultModel[0] == 0 )
					snprintf(g_defaultModel, sizeof(g_defaultModel), "%s", aId);
			}
			MdoConfigSaveLocked();
			xrtMutexUnlock(g_lock);
			BODY_DONE();
			return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
		}
	}

	if ( strcmp(aSeg[0], "models") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "delete") == 0 && MDO_METHOD(POST) ) {
		char aId[64];
		bool bFound = false;
		size_t i;

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		if ( strcmp(aId, MDO_BUILTIN_MODEL_ID) == 0 ) {
			BODY_DONE();
			return ReplyErr(pReq, 400, "builtin model cannot be deleted") ? XS_OK : XS_OK;
		}
		xrtMutexLock(g_lock);
		for ( i = 0; i < g_nModels; i++ )
			if ( strcmp(g_models[i].aId, aId) == 0 ) {
				MdoModelDropClient(&g_models[i]);
				xrtFree(g_models[i].sCaPemText);
				memmove(&g_models[i], &g_models[i + 1],
					(g_nModels - i - 1) * sizeof(MdoModel));
				g_nModels--;
				bFound = true;
				break;
			}
		if ( bFound && strcmp(g_defaultModel, aId) == 0 )
			snprintf(g_defaultModel, sizeof(g_defaultModel), "%s",
				g_nModels > 0 ? g_models[0].aId : "");
		if ( bFound ) MdoConfigSaveLocked();
		xrtMutexUnlock(g_lock);
		BODY_DONE();
		return bFound
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 404, "model not found") ? XS_OK : XS_OK);
	}

	/* ---------------- 计划任务（闹钟） ---------------- */
	if ( strcmp(aSeg[0], "schedules") == 0 && aSeg[1] == NULL ) {
		if ( MDO_METHOD(GET) ) {
			xjsonwriter* pW = JWOpen();
			char* sJson = NULL;
			if ( pW != NULL ) {
				size_t i, k;
				xrtJsonWriterObject(pW);
				JWName(pW, "ok"); JWBool(pW, true);
				JWName(pW, "schedules");
				xrtJsonWriterArray(pW);
				xrtMutexLock(g_schedLock);
				for ( i = 0u; i < g_nScheds; i++ ) {
					const MdoSchedule* pT = g_scheds[i];
					xrtJsonWriterObject(pW);
					JWName(pW, "id"); JWStr(pW, pT->aId);
					JWName(pW, "title"); JWStr(pW, pT->aTitle);
					JWName(pW, "prompt"); JWStr(pW, pT->aPrompt);
					JWName(pW, "project"); JWStr(pW, pT->aProject);
					JWName(pW, "model"); JWStr(pW, pT->aModel);
					JWName(pW, "kind"); JWStr(pW, pT->aKind);
					JWName(pW, "cron"); JWStr(pW, pT->aCron);
					JWName(pW, "intervalMin"); xrtJsonWriterUInt(pW, pT->uIntervalMin);
					JWName(pW, "delayMin"); xrtJsonWriterUInt(pW, pT->uDelayMin);
					JWName(pW, "miss"); JWStr(pW, pT->aMiss);
					JWName(pW, "enabled"); JWBool(pW, pT->bEnabled);
					JWName(pW, "maxRuns"); xrtJsonWriterUInt(pW, pT->uMaxRuns);
					JWName(pW, "created"); xrtJsonWriterUInt(pW, pT->uCreated);
					JWName(pW, "lastRun"); xrtJsonWriterUInt(pW, pT->uLastRun);
					JWName(pW, "nextDue"); xrtJsonWriterUInt(pW, pT->uNextDue);
					JWName(pW, "runCount"); xrtJsonWriterUInt(pW, pT->uRunCount);
					JWName(pW, "lastResult"); JWStr(pW, pT->aLastResult);
					JWName(pW, "running"); JWBool(pW, pT->bRunning);
					JWName(pW, "runs");
					xrtJsonWriterArray(pW);
					for ( k = 0u; k < pT->nRuns; k++ ) {
						xrtJsonWriterObject(pW);
						JWName(pW, "sessionId"); JWStr(pW, pT->aRuns[k].aSessionId);
						JWName(pW, "time"); xrtJsonWriterUInt(pW, pT->aRuns[k].uTime);
						JWName(pW, "status"); JWStr(pW, pT->aRuns[k].aStatus);
						xrtJsonWriterEnd(pW);
					}
					xrtJsonWriterEnd(pW);
					xrtJsonWriterEnd(pW);
				}
				xrtMutexUnlock(g_schedLock);
				xrtJsonWriterEnd(pW);
				xrtJsonWriterEnd(pW);
				sJson = JWTake(pW);
			}
			if ( sJson == NULL )
				return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
			{
				bool bOk = ReplyJSON(pReq, 200, sJson);
				xrtFree(sJson);
				return bOk ? XS_OK : XS_OK;
			}
		}
		if ( MDO_METHOD(POST) ) {
			MdoSchedule* pT;
			char aKind[12] = "", aCron[64] = "", aMiss[12] = "";

			NEED_BODY();
			if ( g_nScheds >= MDO_SCHED_MAX ) { BODY_DONE();
				return ReplyErr(pReq, 400, "schedule limit reached") ? XS_OK : XS_OK; }
			JStr(&tJ, "kind", aKind, sizeof(aKind));
			JStr(&tJ, "cron", aCron, sizeof(aCron));
			JStr(&tJ, "miss", aMiss, sizeof(aMiss));
			if ( aKind[0] == 0 ) snprintf(aKind, sizeof(aKind), "once");
			if ( strcmp(aKind, "cron") != 0 && strcmp(aKind, "interval") != 0 &&
			     strcmp(aKind, "once") != 0 ) { BODY_DONE();
				return ReplyErr(pReq, 400, "kind must be cron|interval|once") ? XS_OK : XS_OK; }
			if ( strcmp(aKind, "cron") == 0 && aCron[0] == 0 ) { BODY_DONE();
				return ReplyErr(pReq, 400, "cron schedule requires a cron expression") ? XS_OK : XS_OK; }
			pT = (MdoSchedule*)xrtRealloc(NULL, sizeof(MdoSchedule));
			if ( pT == NULL ) { BODY_DONE();
				return ReplyErr(pReq, 500, "oom") ? XS_OK : XS_OK; }
			memset(pT, 0, sizeof(*pT));
			snprintf(pT->aId, sizeof(pT->aId), "sc%08x%04x",
				(unsigned)(MdoNowMs() & 0xFFFFFFFFu), (unsigned)(MdoRandHex() & 0xFFFFu));
			JStr(&tJ, "title", pT->aTitle, sizeof(pT->aTitle));
			JStr(&tJ, "prompt", pT->aPrompt, sizeof(pT->aPrompt));
			JStr(&tJ, "project", pT->aProject, sizeof(pT->aProject));
			JStr(&tJ, "model", pT->aModel, sizeof(pT->aModel));
			snprintf(pT->aKind, sizeof(pT->aKind), "%s", aKind);
			snprintf(pT->aCron, sizeof(pT->aCron), "%s", aCron);
			pT->uIntervalMin = (uint32)JInt(&tJ, "intervalMin", 0);
			pT->uDelayMin = (uint32)JInt(&tJ, "delayMin", 0);
			snprintf(pT->aMiss, sizeof(pT->aMiss), "%s",
				(aMiss[0] != 0 && strcmp(aMiss, "catchup") == 0) ? "catchup" : "skip");
			{
				xvalue* pV = xrtValueObjectGet(tJ.pRoot, xrtStrView("enabled"));
				bool b = true;
				pT->bEnabled = (pV == NULL || !xrtValueGetBool(pV, &b)) ? true : b;
			}
			pT->uMaxRuns = (uint32)JInt(&tJ, "maxRuns", 0);
			pT->uCreated = MdoNowMs();
			if ( pT->aTitle[0] == 0 || pT->aPrompt[0] == 0 ) {
				xrtFree(pT); BODY_DONE();
				return ReplyErr(pReq, 400, "missing title/prompt") ? XS_OK : XS_OK;
			}
			if ( strcmp(aKind, "interval") == 0 && pT->uIntervalMin == 0 ) {
				xrtFree(pT); BODY_DONE();
				return ReplyErr(pReq, 400, "interval schedule requires intervalMin >= 1") ? XS_OK : XS_OK;
			}
			{
				uint64 uNow = MdoNowMs();
				if ( strcmp(aKind, "cron") == 0 ) {
					if ( !MdoSchedCronNext(pT->aCron, uNow, &pT->uNextDue) ) {
						xrtFree(pT); BODY_DONE();
						return ReplyErr(pReq, 400, "cron expression has no future match") ? XS_OK : XS_OK;
					}
				} else if ( strcmp(aKind, "interval") == 0 ) {
					pT->uNextDue = uNow + (uint64)pT->uIntervalMin * 60000u;
				} else {
					pT->uNextDue = uNow + (uint64)pT->uDelayMin * 60000u;
				}
			}
			BODY_DONE();
			xrtMutexLock(g_schedLock);
			g_scheds[g_nScheds++] = pT;
			MdoSchedSaveLocked(pT);
			xrtMutexUnlock(g_schedLock);
			MdoSchedWake();
			MdoAuditLog("schedule-created", pT->aId);
			{
				char aOut[96];
				snprintf(aOut, sizeof(aOut), "{\"ok\":true,\"id\":\"%s\"}", pT->aId);
				return ReplyJSON(pReq, 200, aOut) ? XS_OK : XS_OK;
			}
		}
	}

	/* ---- POST /api/schedules/<update|delete|run> {id, ...} ---- */
	if ( strcmp(aSeg[0], "schedules") == 0 && aSeg[1] != NULL &&
	     (strcmp(aSeg[1], "update") == 0 || strcmp(aSeg[1], "delete") == 0 ||
	      strcmp(aSeg[1], "run") == 0) ) {
		char aId[24];
		MdoSchedule* pT;

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		if ( aId[0] == 0 ) { BODY_DONE();
			return ReplyErr(pReq, 400, "missing id") ? XS_OK : XS_OK; }
		xrtMutexLock(g_schedLock);
		pT = MdoSchedFindLocked(aId);
		if ( pT == NULL ) { xrtMutexUnlock(g_schedLock); BODY_DONE();
			return ReplyErr(pReq, 404, "schedule not found") ? XS_OK : XS_OK; }
		if ( strcmp(aSeg[1], "delete") == 0 ) {
			size_t i;
			char aPath[420], aDetail[64];
			if ( pT->bRunning ) { xrtMutexUnlock(g_schedLock); BODY_DONE();
				return ReplyErr(pReq, 409, "schedule is running") ? XS_OK : XS_OK; }
			for ( i = 0u; i < g_nScheds; i++ )
				if ( g_scheds[i] == pT ) {
					memmove(&g_scheds[i], &g_scheds[i + 1],
						(g_nScheds - i - 1u) * sizeof(MdoSchedule*));
					g_nScheds--;
					break;
				}
			xrtMutexUnlock(g_schedLock);
			BODY_DONE();
			MdoSchedPath(pT, aPath, sizeof(aPath));
			MdoTrashFile(aPath, "schedule-delete");
			snprintf(aDetail, sizeof(aDetail), "%s", pT->aId);
			xrtFree(pT);
			MdoAuditLog("schedule-deleted", aDetail);
			MdoSchedWake();
			return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
		}
		if ( strcmp(aSeg[1], "run") == 0 ) {
			pT->uNextDue = MdoNowMs();
			MdoSchedSaveLocked(pT);
			xrtMutexUnlock(g_schedLock);
			BODY_DONE();
			MdoAuditLog("schedule-run-now", pT->aId);
			MdoSchedWake();
			return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
		}
		/* update */
		{
			char aTitle[120], aPrompt[4000], aProject[80], aModel[64];
			char aKind[12], aCron[64], aMiss[12];
			bool bChanged = false, bScheduleChanged = false;
			if ( JStr(&tJ, "title", aTitle, sizeof(aTitle)) && strcmp(aTitle, pT->aTitle) != 0 )
				{ snprintf(pT->aTitle, sizeof(pT->aTitle), "%s", aTitle); bChanged = true; }
			if ( JStr(&tJ, "prompt", aPrompt, sizeof(aPrompt)) && strcmp(aPrompt, pT->aPrompt) != 0 )
				{ snprintf(pT->aPrompt, sizeof(pT->aPrompt), "%s", aPrompt); bChanged = true; }
			if ( JStr(&tJ, "project", aProject, sizeof(aProject)) && strcmp(aProject, pT->aProject) != 0 )
				{ snprintf(pT->aProject, sizeof(pT->aProject), "%s", aProject); bChanged = true; }
			if ( JStr(&tJ, "model", aModel, sizeof(aModel)) && strcmp(aModel, pT->aModel) != 0 )
				{ snprintf(pT->aModel, sizeof(pT->aModel), "%s", aModel); bChanged = true; }
			if ( JStr(&tJ, "kind", aKind, sizeof(aKind)) && strcmp(aKind, pT->aKind) != 0 )
				{ snprintf(pT->aKind, sizeof(pT->aKind), "%s", aKind); bChanged = bScheduleChanged = true; }
			if ( JStr(&tJ, "cron", aCron, sizeof(aCron)) && strcmp(aCron, pT->aCron) != 0 )
				{ snprintf(pT->aCron, sizeof(pT->aCron), "%s", aCron); bChanged = bScheduleChanged = true; }
			if ( JStr(&tJ, "miss", aMiss, sizeof(aMiss)) && strcmp(aMiss, pT->aMiss) != 0 )
				{ snprintf(pT->aMiss, sizeof(pT->aMiss), "%s", aMiss); bChanged = true; }
			{
				uint32 uInterval = (uint32)JInt(&tJ, "intervalMin", (int64)pT->uIntervalMin);
				uint32 uDelay = (uint32)JInt(&tJ, "delayMin", (int64)pT->uDelayMin);
				uint32 uMax = (uint32)JInt(&tJ, "maxRuns", (int64)pT->uMaxRuns);
				if ( uInterval != pT->uIntervalMin ) { pT->uIntervalMin = uInterval; bChanged = bScheduleChanged = true; }
				if ( uDelay != pT->uDelayMin ) { pT->uDelayMin = uDelay; bChanged = bScheduleChanged = true; }
				if ( uMax != pT->uMaxRuns ) { pT->uMaxRuns = uMax; bChanged = true; }
			}
			{
				xvalue* pV = xrtValueObjectGet(tJ.pRoot, xrtStrView("enabled"));
				bool b = false;
				if ( pV != NULL && xrtValueGetBool(pV, &b) && b != pT->bEnabled ) {
					pT->bEnabled = b;
					bChanged = bScheduleChanged = true;   /* 重新启用需重算 nextDue */
				}
			}
			if ( bScheduleChanged && pT->bEnabled ) {
				uint64 uNow = MdoNowMs();
				if ( strcmp(pT->aKind, "cron") == 0 ) {
					if ( !MdoSchedCronNext(pT->aCron, uNow, &pT->uNextDue) ) {
						pT->bEnabled = false; pT->uNextDue = 0;
					}
				} else if ( strcmp(pT->aKind, "interval") == 0 ) {
					pT->uNextDue = pT->uIntervalMin ? uNow + (uint64)pT->uIntervalMin * 60000u : 0;
					if ( pT->uNextDue == 0 ) pT->bEnabled = false;
				} else {
					pT->uNextDue = uNow + (uint64)pT->uDelayMin * 60000u;
				}
				bChanged = true;
			}
			if ( bChanged ) MdoSchedSaveLocked(pT);
			xrtMutexUnlock(g_schedLock);
			BODY_DONE();
			if ( bChanged ) MdoSchedWake();
			return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
		}
	}

	/* ---------------- 项目管理 ---------------- */
	if ( strcmp(aSeg[0], "projects") == 0 && aSeg[1] == NULL ) {
		if ( MDO_METHOD(GET) ) {
			xjsonwriter* pW = JWOpen();
			char* sJson = NULL;
			size_t i;
			if ( pW != NULL ) {
				xrtJsonWriterObject(pW);
				JWName(pW, "ok"); JWBool(pW, true);
				JWName(pW, "active");
				JWStr(pW, g_activeProject[0] ? g_activeProject : MDO_TASKS_SLUG);
				JWName(pW, "projects");
				xrtJsonWriterArray(pW);
				xrtMutexLock(g_lock);
				for ( i = 0; i < g_nProjects; i++ ) {
					xrtJsonWriterObject(pW);
					JWName(pW, "slug"); JWStr(pW, g_projects[i].aSlug);
					JWName(pW, "name"); JWStr(pW, g_projects[i].aName);
					JWName(pW, "path"); JWStr(pW, g_projects[i].aPath);
					JWName(pW, "defaultModel"); JWStr(pW, g_projects[i].aDefaultModel);
					JWName(pW, "valid");
					JWBool(pW, g_projects[i].aPath[0] &&
						xrtDirExists(g_projects[i].aPath));
					xrtJsonWriterEnd(pW);
				}
				/* 「任务」默认分类：不属于任何项目的会话归宿，永在末尾 */
				xrtJsonWriterObject(pW);
				JWName(pW, "slug"); JWStr(pW, MDO_TASKS_SLUG);
				JWName(pW, "name"); JWStr(pW, MDO_TASKS_NAME);
				JWName(pW, "path"); JWStr(pW, "");
				JWName(pW, "defaultModel"); JWStr(pW, "");
				JWName(pW, "valid"); JWBool(pW, true);
				JWName(pW, "tasks"); JWBool(pW, true);
				xrtJsonWriterEnd(pW);
				xrtMutexUnlock(g_lock);
				xrtJsonWriterEnd(pW);
				xrtJsonWriterEnd(pW);
				sJson = JWTake(pW);
			}
			if ( sJson == NULL )
				return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
			{
				bool bOk = ReplyJSON(pReq, 200, sJson);
				xrtFree(sJson);
				return bOk ? XS_OK : XS_OK;
			}
		}
		if ( MDO_METHOD(POST) ) {
			char aPath[300], aName[128], aSlug[80];
			size_t i;
			bool bExists = false;

			NEED_BODY();
			JStr(&tJ, "path", aPath, sizeof(aPath));
			if ( aPath[0] == 0 || !xrtDirExists(aPath) ) {
				BODY_DONE();
				return ReplyErr(pReq, 400, "workspace directory not found") ? XS_OK : XS_OK;
			}
			/* 规整尾部斜杠 */
			{
				size_t n = strlen(aPath);
				while ( n > 3 && (aPath[n - 1] == '/' || aPath[n - 1] == '\\') )
					aPath[--n] = 0;
			}
			MdoSlugFromPath(aPath, aSlug, sizeof(aSlug));
			{
				const char* pBase = strrchr(aPath, '/');
				const char* pBase2 = strrchr(aPath, '\\');
				const char* pLeaf = (pBase2 != NULL && (pBase == NULL || pBase2 > pBase))
					? pBase2 + 1 : (pBase ? pBase + 1 : aPath);
				snprintf(aName, sizeof(aName), "%s", pLeaf);
			}
			xrtMutexLock(g_lock);
			for ( i = 0; i < g_nProjects; i++ )
				if ( strcmp(g_projects[i].aSlug, aSlug) == 0 ) {
					bExists = true;
					break;
				}
			if ( !bExists && g_nProjects < MDO_MAX_PROJECTS ) {
				MdoProject* pP = &g_projects[g_nProjects++];
				memset(pP, 0, sizeof(*pP));
				snprintf(pP->aSlug, sizeof(pP->aSlug), "%s", aSlug);
				snprintf(pP->aName, sizeof(pP->aName), "%s", aName);
				snprintf(pP->aPath, sizeof(pP->aPath), "%s", aPath);
				MdoProjectSaveLocked(pP);
			}
			snprintf(g_activeProject, sizeof(g_activeProject), "%s", aSlug);
			MdoConfigSaveLocked();
			MdoSessionsScanLocked();
			xrtMutexUnlock(g_lock);
			BODY_DONE();
			return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
		}
	}

	if ( strcmp(aSeg[0], "projects") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "activate") == 0 && MDO_METHOD(POST) ) {
		char aSlug[80];
		size_t i;
		bool bFound = false;

		NEED_BODY();
		JStr(&tJ, "slug", aSlug, sizeof(aSlug));
		xrtMutexLock(g_lock);
		if ( strcmp(aSlug, MDO_TASKS_SLUG) == 0 ) {
			/* 「任务」= 无项目态：不属于任何项目 */
			g_activeProject[0] = 0;
			MdoConfigSaveLocked();
			MdoSessionsScanLocked();
			bFound = true;
		} else {
			for ( i = 0; i < g_nProjects; i++ )
				if ( strcmp(g_projects[i].aSlug, aSlug) == 0 ) { bFound = true; break; }
			if ( bFound ) {
				snprintf(g_activeProject, sizeof(g_activeProject), "%s", aSlug);
				MdoConfigSaveLocked();
				MdoSessionsScanLocked();
			}
		}
		xrtMutexUnlock(g_lock);
		BODY_DONE();
		return bFound
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 404, "project not found") ? XS_OK : XS_OK);
	}

	/* ---- POST /api/projects/delete {slug, purge?} ---- */
	if ( strcmp(aSeg[0], "projects") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "delete") == 0 && MDO_METHOD(POST) ) {
		char aSlug[80];
		bool bPurge = false, bOk;

		NEED_BODY();
		JStr(&tJ, "slug", aSlug, sizeof(aSlug));
		{
			xvalue* pV = JObj(&tJ, "purge");
			bool b = false;
			if ( pV != NULL && xrtValueGetBool(pV, &b) ) bPurge = b;
		}
		BODY_DONE();
		if ( aSlug[0] == 0 )
			return ReplyErr(pReq, 400, "missing slug") ? XS_OK : XS_OK;
		if ( strcmp(aSlug, MDO_TASKS_SLUG) == 0 ) {
			BODY_DONE();
			return ReplyErr(pReq, 400, "tasks category cannot be deleted") ? XS_OK : XS_OK;
		}
		xrtMutexLock(g_lock);
		bOk = MdoProjectDeleteLocked(aSlug, bPurge);
		if ( bOk ) MdoSessionsScanLocked();
		xrtMutexUnlock(g_lock);
		return bOk
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 400, "project not found or sessions running") ? XS_OK : XS_OK);
	}

	/* ---- POST /api/projects/config {slug, defaultModel} ---- */
	if ( strcmp(aSeg[0], "projects") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "config") == 0 && MDO_METHOD(POST) ) {
		char aSlug[80], aModel[64];
		size_t i;
		bool bFound = false;

		NEED_BODY();
		JStr(&tJ, "slug", aSlug, sizeof(aSlug));
		JStr(&tJ, "defaultModel", aModel, sizeof(aModel));
		BODY_DONE();
		xrtMutexLock(g_lock);
		for ( i = 0; i < g_nProjects; i++ )
			if ( strcmp(g_projects[i].aSlug, aSlug) == 0 ) {
				snprintf(g_projects[i].aDefaultModel,
					sizeof(g_projects[i].aDefaultModel), "%s", aModel);
				MdoProjectSaveLocked(&g_projects[i]);
				bFound = true;
				break;
			}
		xrtMutexUnlock(g_lock);
		return bFound
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 404, "project not found") ? XS_OK : XS_OK);
	}

	/* ---- GET /api/workspace/files?q= —— @ 补全数据源 ---- */
	if ( strcmp(aSeg[0], "workspace") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "files") == 0 && MDO_METHOD(GET) ) {
		MdoProject* pProj;
		MdoWsWalk tW;
		char aRel[260];
		char aQuery[128] = {0};
		xstrview tQ = pReq->head->Target;
		const char* pQ = memchr(tQ.Data, '?', tQ.Size);

		xrtMutexLock(g_lock);
		pProj = MdoActiveProject();
		xrtMutexUnlock(g_lock);
		if ( pProj == NULL )
			return ReplyErr(pReq, 400, "no active project") ? XS_OK : XS_OK;
		if ( !xrtDirExists(pProj->aPath) )
			return ReplyErr(pReq, 400, "workspace path missing") ? XS_OK : XS_OK;
		if ( pQ != NULL ) {
			const char* pEnd = tQ.Data + tQ.Size;
			const char* pS = MdoFindBounded(pQ, pEnd, "q=");
			if ( pS != NULL ) {
				size_t n = 0;
				pS += 2;
				while ( pS + n < pEnd && pS[n] != '&' && n < sizeof(aQuery) - 1 ) {
					char c = pS[n];
					if ( c == '%' && pS + n + 2 < pEnd ) {
						char aHex[3] = { pS[n + 1], pS[n + 2], 0 };
						aQuery[n] = (char)strtoul(aHex, NULL, 16);
						n += 3;
					} else if ( c == '+' ) {
						aQuery[n++] = ' ';
					} else {
						aQuery[n++] = c;
					}
				}
				aQuery[n] = 0;
				/* 查询串小写化（匹配器约定） */
				for ( n = 0; aQuery[n]; n++ )
					if ( aQuery[n] >= 'A' && aQuery[n] <= 'Z' ) aQuery[n] += 32;
			}
		}
		memset(&tW, 0, sizeof(tW));
		snprintf(tW.aQuery, sizeof(tW.aQuery), "%s", aQuery);
		tW.bFirst = true;
		aRel[0] = 0;
		MdoWsWalkDir(&tW, pProj->aPath, aRel, 0, 0);
		{
			char aOut[1024];
			MdoBuf tHead = {0};
			snprintf(aOut, sizeof(aOut), "{\"ok\":true,\"root\":");
			MdoBufAppend(&tHead, aOut, (unsigned)strlen(aOut));
			BufJsonStr(&tHead, pProj->aPath);
			MdoBufAppend(&tHead, ",\"files\":[", 10);
			if ( tW.tBuf.pData != NULL )
				MdoBufAppend(&tHead, tW.tBuf.pData, (unsigned)tW.tBuf.iLen);
			MdoBufAppend(&tHead, "]}", 2);
			xrtFree(tW.tBuf.pData);
			if ( tHead.pData == NULL )
				return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
			{
				bool bOk = ReplyJSON(pReq, 200, tHead.pData);
				xrtFree(tHead.pData);
				return bOk ? XS_OK : XS_OK;
			}
		}
	}

	/* ---------------- 会话管理 ---------------- */
	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] == NULL ) {
		if ( MDO_METHOD(GET) ) {
			xjsonwriter* pW = JWOpen();
			char* sJson = NULL;
			size_t i;
			char aScanSlug[80] = {0};
			bool bAll = false;
			{
				xstrview tQS = pReq->head->Target;
				const char* pQM = memchr(tQS.Data, '?', tQS.Size);
				if ( pQM != NULL ) {
					const char* pP = MdoFindBounded(pQM, tQS.Data + tQS.Size, "project=");
					if ( pP != NULL ) {
						size_t nS = 0;
						pP += 8;
						while ( pP + nS < tQS.Data + tQS.Size && pP[nS] != '&' &&
						        nS < sizeof(aScanSlug) - 1 ) { aScanSlug[nS] = pP[nS]; nS++; }
						aScanSlug[nS] = 0;
					}
					bAll = (MdoFindBounded(pQM, tQS.Data + tQS.Size, "all=1") != NULL);
				}
			}
			if ( pW != NULL ) {
				xrtJsonWriterObject(pW);
				JWName(pW, "ok"); JWBool(pW, true);
				JWName(pW, "sessions");
				xrtJsonWriterArray(pW);
				xrtMutexLock(g_lock);
				if ( bAll ) MdoSessionsScanAllLocked(pW);
				else MdoSessionsScanProjectLocked(aScanSlug[0] ? aScanSlug : NULL);
				if ( !bAll ) for ( i = 0; i < g_nSessions; i++ ) {
					const MdoSession* pS = g_sessions[i];
					xrtJsonWriterObject(pW);
					JWName(pW, "id"); JWStr(pW, pS->aId);
					JWName(pW, "title"); JWStr(pW, pS->aTitle);
					JWName(pW, "model"); JWStr(pW, pS->aModelId);
					JWName(pW, "updatedAt"); xrtJsonWriterUInt(pW, pS->uUpdatedAt);
					JWName(pW, "turns"); xrtJsonWriterUInt(pW, pS->uTurns);
					JWName(pW, "pinned"); JWBool(pW, pS->bPinned);
					JWName(pW, "running"); JWBool(pW, pS->pRun != NULL);
					if ( pS->pRun != NULL )
						{ JWName(pW, "turnId"); xrtJsonWriterUInt(pW, pS->pRun->uTurnId); }
					xrtJsonWriterEnd(pW);
				}
				xrtMutexUnlock(g_lock);
				xrtJsonWriterEnd(pW);
				xrtJsonWriterEnd(pW);
				sJson = JWTake(pW);
			}
			if ( sJson == NULL )
				return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
			{
				bool bOk = ReplyJSON(pReq, 200, sJson);
				xrtFree(sJson);
				return bOk ? XS_OK : XS_OK;
			}
		}
		if ( MDO_METHOD(POST) ) {
			char aTitle[128];
			MdoSession* pNew;
			char aModelId[64];
			char aUserPrompt[2048];
			char aProject[80];
			MdoProject* pProj = NULL;
			uint64 uWindow = 0;

			NEED_BODY();
			JStr(&tJ, "title", aTitle, sizeof(aTitle));
			JStr(&tJ, "model", aModelId, sizeof(aModelId));
			JStr(&tJ, "systemPrompt", aUserPrompt, sizeof(aUserPrompt));
			JStr(&tJ, "project", aProject, sizeof(aProject));
			xrtMutexLock(g_lock);
			{
				size_t k;
				/* 去激活化：显式 project 参数优先，否则回退激活态（旧客户端兼容） */
				const char* sUse = aProject[0] ? aProject :
					(g_activeProject[0] ? g_activeProject : MDO_TASKS_SLUG);
				for ( k = 0; k < g_nProjects; k++ )
					if ( strcmp(g_projects[k].aSlug, sUse) == 0 ) { pProj = &g_projects[k]; break; }
				MdoModel* pM = MdoModelFind(aModelId[0] ? aModelId : g_defaultModel);
				if ( pM != NULL ) uWindow = pM->uContextWindow;
				if ( !aModelId[0] && pM != NULL )
					snprintf(aModelId, sizeof(aModelId), "%s", pM->aId);
			}
			pNew = MdoSessionCreateLocked(pProj, aTitle, aModelId, uWindow, aUserPrompt);
			xrtMutexUnlock(g_lock);
			BODY_DONE();
			if ( pNew == NULL )
				return ReplyErr(pReq, 400, "no active project or table full") ? XS_OK : XS_OK;
			{
				static char aOut[256];
				snprintf(aOut, sizeof(aOut),
					"{\"ok\":true,\"id\":\"%s\",\"title\":\"%s\",\"model\":\"%s\"}",
					pNew->aId, pNew->aTitle, pNew->aModelId);
				return ReplyJSON(pReq, 200, aOut) ? XS_OK : XS_OK;
			}
		}
	}

	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "rename") == 0 && MDO_METHOD(POST) ) {
		char aId[40], aTitle[128];
		bool bFound = false;

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		JStr(&tJ, "title", aTitle, sizeof(aTitle));
		xrtMutexLock(g_lock);
		{
			MdoSession* pS = MdoSessionFindLocked(aId);
			if ( pS != NULL && aTitle[0] ) {
				snprintf(pS->aTitle, sizeof(pS->aTitle), "%s", aTitle);
				pS->uUpdatedAt = MdoNowMs();
				MdoSessionSaveMetaLocked(pS);
				/* UI 日志补 session/title（重放一致） */
				{
					xjsonwriter* pW = JWOpen();
					if ( pW != NULL ) {
						xrtJsonWriterObject(pW);
						JWName(pW, "seq"); xrtJsonWriterUInt(pW, 0);
						JWName(pW, "time"); xrtJsonWriterUInt(pW, MdoNowMs());
						JWName(pW, "type"); JWStr(pW, "session/title");
						JWName(pW, "data");
						xrtJsonWriterObject(pW);
						JWName(pW, "title"); JWStr(pW, aTitle);
						xrtJsonWriterEnd(pW);
						xrtJsonWriterEnd(pW);
						{
							char* sLine = JWTake(pW);
							if ( sLine != NULL ) {
								MdoUiAppend(pS, sLine);
								xrtFree(sLine);
							}
						}
					}
				}
				bFound = true;
			}
		}
		xrtMutexUnlock(g_lock);
		BODY_DONE();
		return bFound
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 404, "session not found") ? XS_OK : XS_OK);
	}

	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "delete") == 0 && MDO_METHOD(POST) ) {
		char aId[40];
		bool bFound;

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		xrtMutexLock(g_lock);
		bFound = MdoSessionDeleteLocked(aId);
		xrtMutexUnlock(g_lock);
		BODY_DONE();
		return bFound
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 400, "session not found or running") ? XS_OK : XS_OK);
	}

	/* ---- POST /api/sessions/clear {id} —— 重置上下文（保留标题/模型/置顶/用户指令） ---- */
	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "clear") == 0 && MDO_METHOD(POST) ) {
		char aId[40];
		MdoSession* pS;
		bool bOk = false;

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		BODY_DONE();
		xrtMutexLock(g_lock);
		pS = MdoSessionFindLocked(aId);
		if ( pS != NULL && pS->pRun == NULL ) {
			MdoProject* pProj = NULL;                 /* 按会话属主桶定位，不依赖激活态 */
			size_t k;
			for ( k = 0; k < g_nProjects; k++ )
				if ( strcmp(g_projects[k].aSlug, pS->aProject) == 0 ) { pProj = &g_projects[k]; break; }
			char aMeta[380], aJournal[380], aSnap[380], aUi[380];
			MdoSessionCloseHandles(pS);
			{
				MdoSessionPaths(pProj, aId, aMeta, sizeof(aMeta), aJournal,
					sizeof(aJournal), aSnap, sizeof(aSnap), aUi, sizeof(aUi));
				MdoTrashFile(aJournal, "session-clear");
				MdoTrashFile(aSnap, "session-clear");
				MdoTrashFile(aUi, "session-clear");
			}
			pS->uTurns = 0;
			pS->uUpdatedAt = MdoNowMs();
			MdoSessionSaveMetaLocked(pS);
			bOk = true;
		}
		xrtMutexUnlock(g_lock);
		return bOk
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 400, "session not found or running") ? XS_OK : XS_OK);
	}

	/* ---- POST /api/sessions/truncate {id, keepCount} —— UI 日志按行截断（重试/编辑的
	 *      视图对齐；客户端与服务端事件 1:1，行数即锚点。LLM 账本不可变：模型上下文
	 *      保留完整历史，此为重放一致性操作） ---- */
	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "truncate") == 0 && MDO_METHOD(POST) ) {
		char aId[40];
		int64 iKeep = 0;
		MdoSession* pS;
		bool bOk = false;

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		iKeep = JInt(&tJ, "keepCount", 0);
		BODY_DONE();
		xrtMutexLock(g_lock);
		pS = MdoSessionFindLocked(aId);
		if ( pS != NULL && pS->pRun == NULL ) {
			size_t nLines = 0;
			char* sLog = MdoUiReadAll(pS, &nLines);
			MdoProject* pProj = NULL;                 /* 按会话属主桶定位 */
			size_t k;
			for ( k = 0; k < g_nProjects; k++ )
				if ( strcmp(g_projects[k].aSlug, pS->aProject) == 0 ) { pProj = &g_projects[k]; break; }
			if ( sLog != NULL ) {
				/* 保留 seq <= uptoSeq 的行重写 */
				MdoBuf tKeep = {0};
				char* pLine = sLog;
				char* pEnd = sLog + strlen(sLog);
				if ( pProj != NULL ) {
					char aUi[380];
					int64 iLine = 0;
					MdoSessionPaths(pProj, aId, NULL, 0, NULL, 0, NULL, 0, aUi, sizeof(aUi));
					while ( pLine < pEnd ) {
						char* pNl = strchr(pLine, '\n');
						size_t iLen = pNl ? (size_t)(pNl - pLine) : (size_t)(pEnd - pLine);
						if ( iLine < iKeep ) {     /* 只保留前 keepCount 行 */
							MdoBufAppend(&tKeep, pLine, (unsigned)iLen);
							MdoBufAppend(&tKeep, "\n", 1);
						}
						iLine++;
						pLine = pNl ? pNl + 1 : pEnd;
					}
					if ( pS->fUi != NULL ) { fclose(pS->fUi); pS->fUi = NULL; }
					xrtFileWriteTextAtomic(aUi,
						xrtStrView(tKeep.pData ? tKeep.pData : ""),
						XENCODING_UTF8, XUTF_REPLACE, false);
					xrtFree(tKeep.pData);
				}
				xrtFree(sLog);
			}
			pS->uUpdatedAt = MdoNowMs();
			MdoSessionSaveMetaLocked(pS);
			bOk = true;
		}
		xrtMutexUnlock(g_lock);
		return bOk
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 400, "session not found or running") ? XS_OK : XS_OK);
	}

	/* ---- POST /api/sessions/feedback {id, nodeId, value} —— 点赞/点踩写穿 UI 日志 ---- */
	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "feedback") == 0 && MDO_METHOD(POST) ) {
		char aId[40], aNodeId[40], aValue[12];
		MdoSession* pS;
		bool bOk = false;

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		JStr(&tJ, "nodeId", aNodeId, sizeof(aNodeId));
		JStr(&tJ, "value", aValue, sizeof(aValue));
		BODY_DONE();
		if ( aId[0] == 0 || aNodeId[0] == 0 ||
		     (strcmp(aValue, "good") != 0 && strcmp(aValue, "bad") != 0 &&
		      strcmp(aValue, "none") != 0) )
			return ReplyErr(pReq, 400, "bad feedback payload") ? XS_OK : XS_OK;
		xrtMutexLock(g_lock);
		pS = MdoSessionFindLocked(aId);
		if ( pS == NULL ) {
			MdoSessionsScanLocked();
			pS = MdoSessionFindLocked(aId);
		}
		/* 运行中拒绝：fUi 句柄与回合线程共用（pQ 锁内写），避免无锁并发 fputs */
		if ( pS != NULL && pS->pRun == NULL ) {
			xjsonwriter* pW = JWOpen();
			if ( pW != NULL ) {
				char* sLine;
				xrtJsonWriterObject(pW);
				JWName(pW, "seq"); xrtJsonWriterUInt(pW, 0);
				JWName(pW, "time"); xrtJsonWriterUInt(pW, MdoNowMs());
				JWName(pW, "type"); JWStr(pW, "feedback/set");
				JWName(pW, "data");
				xrtJsonWriterObject(pW);
				JWName(pW, "nodeId"); JWStr(pW, aNodeId);
				JWName(pW, "value"); JWStr(pW, aValue);
				xrtJsonWriterEnd(pW);
				xrtJsonWriterEnd(pW);
				sLine = JWTake(pW);
				if ( sLine != NULL ) {
					MdoUiAppend(pS, sLine);
					xrtFree(sLine);
					pS->uUpdatedAt = MdoNowMs();
					MdoSessionSaveMetaLocked(pS);
					bOk = true;
				}
			}
		}
		xrtMutexUnlock(g_lock);
		return bOk
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 400, "session not found or running") ? XS_OK : XS_OK);
	}

	/* ---- POST /api/sessions/fork {id} —— 会话分叉（完整复制：LLM 状态+UI 日志+meta） ---- */
	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "fork") == 0 && MDO_METHOD(POST) ) {
		char aId[40], aErr[256];
		MdoSession* pSrc;
		MdoSession* pNew = NULL;
		MdoProject* pProj;
		char aMeta[380], aJournal[380], aSnap[380], aUi[380];
		char aNewMeta[380], aNewJournal[380], aNewSnap[380], aNewUi[380];

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		BODY_DONE();
		if ( aId[0] == 0 )
			return ReplyErr(pReq, 400, "missing id") ? XS_OK : XS_OK;

		xrtMutexLock(g_lock);
		pSrc = MdoSessionFindLocked(aId);
		if ( pSrc == NULL ) {
			MdoSessionsScanLocked();
			pSrc = MdoSessionFindLocked(aId);
		}
		if ( pSrc != NULL && pSrc->pRun != NULL ) pSrc = NULL;   /* 运行中禁分叉 */
		pProj = MdoActiveProject();   /* 任务态为 NULL：路径落到 _tasks 桶 */
		xrtMutexUnlock(g_lock);
		if ( pSrc == NULL )
			return ReplyErr(pReq, 400, "session not found or running") ? XS_OK : XS_OK;

		/* 源 LLM 会话惰性载入（锁外：磁盘 IO） */
		{
			MdoModel* pModel = MdoModelFind(pSrc->aModelId[0] ?
				pSrc->aModelId : g_defaultModel);
			xllm_error tCErr;
			xllmErrorInit(&tCErr);
			if ( pModel == NULL || MdoClientFor(pModel, &tCErr) == NULL ) {
				snprintf(aErr, sizeof(aErr), "model unavailable: %s", pSrc->aModelId);
				return ReplyErr(pReq, 400, aErr) ? XS_OK : XS_OK;
			}
			if ( !MdoSessionEnsure(pSrc, NULL, pModel, aErr, sizeof(aErr)) )
				return ReplyErr(pReq, 500, aErr) ? XS_OK : XS_OK;
		}

		/* 新会话注册（锁内） */
		xrtMutexLock(g_lock);
		pNew = MdoSessionCreateLocked(MdoActiveProject(), NULL, pSrc->aModelId,
			pSrc->uContextWindow, pSrc->aUserPrompt);
		xrtMutexUnlock(g_lock);
		if ( pNew == NULL )
			return ReplyErr(pReq, 500, "fork create failed") ? XS_OK : XS_OK;
		snprintf(pNew->aTitle, sizeof(pNew->aTitle), "%s (分叉)", pSrc->aTitle);
		pNew->uTurns = pSrc->uTurns;
		pNew->uCreatedAt = pSrc->uCreatedAt;
		MdoSessionPaths(pProj, pSrc->aId, aMeta, sizeof(aMeta), aJournal,
			sizeof(aJournal), aSnap, sizeof(aSnap), aUi, sizeof(aUi));
		MdoSessionPaths(pProj, pNew->aId, aNewMeta, sizeof(aNewMeta), aNewJournal,
			sizeof(aNewJournal), aNewSnap, sizeof(aNewSnap), aNewUi, sizeof(aNewUi));

		/* LLM 状态：Fork → 快照落盘（新会话惰性 Recover 自 snap） */
		{
			xllm_error tFErr;
			xllm_session* pFork;
			xllmErrorInit(&tFErr);
			pFork = xllmSessionFork(pSrc->pSession, &tFErr);
			if ( pFork == NULL ) {
				snprintf(aErr, sizeof(aErr), "fork failed: %s",
					tFErr.sMessage[0] ? tFErr.sMessage : "?");
				xrtMutexLock(g_lock);
				MdoSessionDeleteLocked(pNew->aId);
				xrtMutexUnlock(g_lock);
				return ReplyErr(pReq, 500, aErr) ? XS_OK : XS_OK;
			}
			xllmSessionSave(pFork, aNewSnap, &tFErr);
			xllmSessionDestroy(pFork);
		}
		/* UI 日志：整文件复制 */
		{
			size_t iSize = 0;
			bytes pLog = xrtFileReadAll(aUi, &iSize);
			if ( pLog != NULL && iSize > 0 ) {
				xbytesview tView;
				tView.Data = pLog;
				tView.Size = iSize;
				xrtFileWriteAll(aNewUi, tView);
			}
			xrtFree(pLog);
		}
		xrtMutexLock(g_lock);
		MdoSessionSaveMetaLocked(pNew);
		xrtMutexUnlock(g_lock);

		{
			static char aOut[256];
			snprintf(aOut, sizeof(aOut),
				"{\"ok\":true,\"id\":\"%s\",\"title\":\"%s\",\"model\":\"%s\"}",
				pNew->aId, pNew->aTitle, pNew->aModelId);
			return ReplyJSON(pReq, 200, aOut) ? XS_OK : XS_OK;
		}
	}

	/* ---- POST /api/sessions/pin {id, pinned} —— 置顶持久化 ---- */
	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] != NULL &&
	     strcmp(aSeg[1], "pin") == 0 && MDO_METHOD(POST) ) {
		char aId[40];
		MdoSession* pS;
		bool bPin = false, bOk = false;

		NEED_BODY();
		JStr(&tJ, "id", aId, sizeof(aId));
		{
			xvalue* pV = JObj(&tJ, "pinned");
			bool b = false;
			if ( pV != NULL && xrtValueGetBool(pV, &b) ) bPin = b;
		}
		BODY_DONE();
		xrtMutexLock(g_lock);
		pS = MdoSessionFindLocked(aId);
		if ( pS != NULL ) {
			pS->bPinned = bPin;
			MdoSessionSaveMetaLocked(pS);
			bOk = true;
		}
		xrtMutexUnlock(g_lock);
		return bOk
			? (ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK)
			: (ReplyErr(pReq, 404, "session not found") ? XS_OK : XS_OK);
	}

	/* GET /api/sessions/<id>/events —— UI 日志全量重放（行=合法 JSON，直接拼接） */
	if ( strcmp(aSeg[0], "sessions") == 0 && aSeg[1] != NULL &&
	     aSeg[2] != NULL && strcmp(aSeg[2], "events") == 0 && MDO_METHOD(GET) ) {
		MdoSession* pS;
		size_t nLines = 0;
		char* sLog;
		MdoBuf tOut = {0};

		xrtMutexLock(g_lock);
		MdoSessionsScanLocked();
		pS = MdoSessionFindLocked(aSeg[1]);
		xrtMutexUnlock(g_lock);
		if ( pS == NULL )
			return ReplyErr(pReq, 404, "session not found") ? XS_OK : XS_OK;
		sLog = MdoUiReadAll(pS, &nLines);
		MdoBufAppend(&tOut, "{\"ok\":true,\"id\":", 16);
		BufJsonStr(&tOut, pS->aId);
		MdoBufAppend(&tOut, ",\"title\":", 9);
		BufJsonStr(&tOut, pS->aTitle);
		MdoBufAppend(&tOut, ",\"model\":", 9);
		BufJsonStr(&tOut, pS->aModelId);
		MdoBufAppend(&tOut, ",\"events\":[", 11);
		if ( sLog != NULL ) {
			char* pLine = sLog;
			char* pEnd = sLog + strlen(sLog);
			bool bFirst = true;
			while ( pLine < pEnd ) {
				char* pNl = strchr(pLine, '\n');
				size_t iLen = pNl ? (size_t)(pNl - pLine) : (size_t)(pEnd - pLine);
				while ( iLen > 0 && (*pLine == ' ' || *pLine == '\r') ) {
					pLine++; iLen--;
				}
				if ( iLen > 0 && *pLine == '{' ) {
					if ( !bFirst ) MdoBufAppend(&tOut, ",", 1);
					MdoBufAppend(&tOut, pLine, (unsigned)iLen);
					bFirst = false;
				}
				pLine = pNl ? pNl + 1 : pEnd;
			}
		}
		xrtFree(sLog);
		MdoBufAppend(&tOut, "]}", 2);
		if ( tOut.pData == NULL )
			return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
		{
			bool bOk = ReplyJSON(pReq, 200, tOut.pData);
			xrtFree(tOut.pData);
			return bOk ? XS_OK : XS_OK;
		}
	}

	/* ---------------- 回合：prompt / events / approval / cancel ---------------- */
	if ( strcmp(aSeg[0], "prompt") == 0 && MDO_METHOD(POST) ) {
		char aSessionId[40], aText[16384], aModelId[64], aErr[256];
		MdoSession* pS;
		MdoModel* pModel;
		xllm_error tErr;
		uint64 uTurnId;
		MdoImage aImages[MDO_MAX_IMAGES];
		size_t nImages = 0;
		size_t iImg;

		for ( iImg = 0; iImg < MDO_MAX_IMAGES; iImg++ ) {
			aImages[iImg].sDataUrl = NULL;
			aImages[iImg].pBytes = NULL;
			aImages[iImg].iSize = 0;
			aImages[iImg].aMime[0] = 0;
		}
#define PROMPT_FREE_IMAGES() \
	do { \
		size_t iF_; \
		for ( iF_ = 0; iF_ < nImages; iF_++ ) { \
			xrtFree(aImages[iF_].sDataUrl); \
			xrtFree(aImages[iF_].pBytes); \
		} \
		nImages = 0; \
	} while ( 0 )

		char aEffort[12] = {0};

		NEED_BODY();
		JStr(&tJ, "sessionId", aSessionId, sizeof(aSessionId));
		JStr(&tJ, "text", aText, sizeof(aText));
		JStr(&tJ, "modelId", aModelId, sizeof(aModelId));
		JStr(&tJ, "effort", aEffort, sizeof(aEffort));
		/* images[]：dataUrl 形态（data:image/png;base64,...）；解析后即解码 */
		{
			xvalue* pArr = JArr(&tJ, "images");
			size_t nImg = pArr ? xrtValueCount(pArr) : 0;
			size_t i;

			if ( nImg > MDO_MAX_IMAGES ) nImg = MDO_MAX_IMAGES;
			for ( i = 0; i < nImg; i++ ) {
				xvalue* pItem = xrtValueArrayAt(pArr, (int64)i);
				xstrview tUrl;
				if ( pItem == NULL || !xrtValueGetString(pItem, &tUrl) ) continue;
				/* data:<mime>;base64,<payload> */
				if ( tUrl.Size > 5 && memcmp(tUrl.Data, "data:", 5) == 0 ) {
					const char* pBase = (const char*)tUrl.Data + 5;
					size_t nLeft = tUrl.Size - 5;
					const char* pSemi = (const char*)memchr(pBase, ';', nLeft);
					const char* pComma = (const char*)memchr(pBase, ',', nLeft);
					if ( pSemi != NULL && pComma != NULL && pComma > pSemi + 6 &&
					     memcmp(pSemi, ";base64", 7) == 0 ) {
						size_t nMime = (size_t)(pSemi - pBase);
						size_t nPayload = (size_t)((const char*)tUrl.Data + tUrl.Size - (pComma + 1));
						size_t nDecoded = 0;
						bytes pDecoded = xrtBase64DecodeNew(pComma + 1, nPayload,
							&nDecoded, NULL);
						if ( pDecoded != NULL && nDecoded > 0 &&
						     nDecoded <= MDO_IMAGE_MAX_BYTES && nMime < 40 ) {
							MdoImage* pImg = &aImages[nImages];
							pImg->sDataUrl = (char*)xrtRealloc(NULL, tUrl.Size + 1);
							if ( pImg->sDataUrl != NULL ) {
								memcpy(pImg->sDataUrl, tUrl.Data, tUrl.Size);
								pImg->sDataUrl[tUrl.Size] = 0;
								pImg->pBytes = pDecoded;
								pImg->iSize = nDecoded;
								memcpy(pImg->aMime, pBase, nMime);
								pImg->aMime[nMime] = 0;
								nImages++;
								continue;
							}
							xrtFree(pImg->sDataUrl);
						}
						xrtFree(pDecoded);
					}
				}
			}
		}
		BODY_DONE();
		if ( aSessionId[0] == 0 || aText[0] == 0 ) {
			PROMPT_FREE_IMAGES();
			return ReplyErr(pReq, 400, "missing sessionId/text") ? XS_OK : XS_OK;
		}

		xrtMutexLock(g_lock);
		pS = MdoSessionFindLocked(aSessionId);
		if ( pS == NULL ) {
			MdoSessionsScanLocked();    /* 冷表：扫盘重查 */
			pS = MdoSessionFindLocked(aSessionId);
		}
		if ( pS == NULL ) {
			xrtMutexUnlock(g_lock);
			PROMPT_FREE_IMAGES();
			return ReplyErr(pReq, 404, "session not found") ? XS_OK : XS_OK;
		}
		if ( pS->pRun != NULL ) {
			xrtMutexUnlock(g_lock);
			PROMPT_FREE_IMAGES();
			return ReplyErr(pReq, 409, "session is running") ? XS_OK : XS_OK;
		}
		/* 预占会话（sentinel）：慢速段（客户端/Recover）无并发窗口 */
		pS->pRun = (MdoRun*)1;
		/* 模型解析：显式 > 会话 > 默认 */
		if ( aModelId[0] == 0 )
			snprintf(aModelId, sizeof(aModelId), "%s",
				pS->aModelId[0] ? pS->aModelId : g_defaultModel);
		pModel = MdoModelFind(aModelId);
		if ( pModel == NULL ) {
			pS->pRun = NULL;
			xrtMutexUnlock(g_lock);
			PROMPT_FREE_IMAGES();
			return ReplyErr(pReq, 400, "unknown model") ? XS_OK : XS_OK;
		}
		xrtMutexUnlock(g_lock);

		if ( aEffort[0] != 0 ) {
			xrtMutexLock(g_lock);
			snprintf(pS->aEffort, sizeof(pS->aEffort), "%s", aEffort);
			xrtMutexUnlock(g_lock);
		}

		/* 客户端（惰性，锁外：TLS 栈初始化） */
		xllmErrorInit(&tErr);
		if ( MdoClientFor(pModel, &tErr) == NULL ) {
			snprintf(aErr, sizeof(aErr), "client init failed: %s",
				tErr.sMessage[0] ? tErr.sMessage : "?");
			PROMPT_FREE_IMAGES();
			xrtMutexLock(g_lock);
			pS->pRun = NULL;
			xrtMutexUnlock(g_lock);
			return ReplyErr(pReq, 400, aErr) ? XS_OK : XS_OK;
		}

		/* 会话恢复 + 绑定客户端（锁外：磁盘 IO + Recover） */
		if ( !MdoSessionEnsure(pS, NULL, pModel, aErr, sizeof(aErr)) ) {
			PROMPT_FREE_IMAGES();
			xrtMutexLock(g_lock);
			pS->pRun = NULL;
			xrtMutexUnlock(g_lock);
			return ReplyErr(pReq, 500, aErr) ? XS_OK : XS_OK;
		}

		uTurnId = MdoRunStart(pS, aText, pModel, NULL, false, aImages, nImages,
			aErr, sizeof(aErr));
		PROMPT_FREE_IMAGES();     /* RunStart 已深拷贝 */
		if ( uTurnId == 0 ) {
			xrtMutexLock(g_lock);
			if ( pS->pRun == (MdoRun*)1 ) pS->pRun = NULL;
			xrtMutexUnlock(g_lock);
			return ReplyErr(pReq, 500, aErr) ? XS_OK : XS_OK;
		}
		{
			static char aOut[128];
			snprintf(aOut, sizeof(aOut),
				"{\"ok\":true,\"turnId\":%llu}",
				(unsigned long long)uTurnId);
			return ReplyJSON(pReq, 200, aOut) ? XS_OK : XS_OK;
		}
	}

	if ( strcmp(aSeg[0], "turn") == 0 && aSeg[1] != NULL && aSeg[2] != NULL ) {
		uint64 uTurnId = MdoParseU64(aSeg[1]);
		MdoRun* pRun;

		if ( strcmp(aSeg[2], "events") == 0 && MDO_METHOD(GET) ) {
			uint64 uSince = 0;
			xstrview tQ = pReq->head->Target;
			const char* pQ = memchr(tQ.Data, '?', tQ.Size);

			if ( pQ != NULL ) {
				const char* pS = MdoFindBounded(pQ, tQ.Data + tQ.Size, "since=");
				if ( pS != NULL ) uSince = MdoParseU64(pS + 6);
			}
			pRun = MdoRunFind(uTurnId);
			if ( pRun == NULL )
				return ReplyJSON(pReq, 200,
					"{\"ok\":true,\"events\":[],\"done\":true}") ? XS_OK : XS_OK;
			{
				/* 队列行即合法 JSON 事件对象；快照后手工拼接 */
				MdoBuf tOut = {0};
				char** pCopy = NULL;
				size_t iNew = 0, i;
				bool bDone;

				xrtMutexLock(pRun->pQ->pLock);
				bDone = pRun->pQ->bDone;
				pCopy = (char**)xrtRealloc(NULL,
					(pRun->pQ->n ? pRun->pQ->n : 1) * sizeof(char*));
				if ( pCopy != NULL ) {
					for ( i = 0; i < pRun->pQ->n; i++ ) {
						const char* pLine = pRun->pQ->pLines[i];
						const char* pSeq = strstr(pLine, "\"seq\":");
						uint64 uSeq = pSeq ?
							MdoParseU64(pSeq + 6) : (uSince + 1);
						if ( uSeq <= uSince ) continue;
						pCopy[iNew++] = pRun->pQ->pLines[i];
					}
				}
				xrtMutexUnlock(pRun->pQ->pLock);

				MdoBufAppend(&tOut, "{\"ok\":true,\"events\":[", 21);
				for ( i = 0; i < iNew; i++ ) {
					if ( i > 0 ) MdoBufAppend(&tOut, ",", 1);
					MdoBufAppend(&tOut, pCopy[i], (unsigned)strlen(pCopy[i]));
				}
				xrtFree(pCopy);
				MdoBufAppend(&tOut, bDone ? "],\"done\":true}" : "],\"done\":false}",
					bDone ? 14 : 15);
				/* 排空即出表：done 且本次无新事件 → 标记并摘除（表引用同步回落） */
				if ( bDone && iNew == 0 ) {
					xrtMutexLock(g_lock);
					pRun->bDrained = true;
					for ( i = 0; i < g_nRuns; i++ )
						if ( g_runs[i] == pRun ) {
							memmove(&g_runs[i], &g_runs[i + 1],
								(g_nRuns - i - 1) * sizeof(MdoRun*));
							g_nRuns--;
							break;
						}
					if ( pRun->uRefs > 0 ) pRun->uRefs--;    /* 表引用 */
					xrtMutexUnlock(g_lock);
				}
				if ( tOut.pData == NULL ) {
					MdoRunRelease(pRun);
					return ReplyErr(pReq, 500, "encode failed") ? XS_OK : XS_OK;
				}
				{
					bool bOk = ReplyJSON(pReq, 200, tOut.pData);
					xrtFree(tOut.pData);
					MdoRunRelease(pRun);
					return bOk ? XS_OK : XS_OK;
				}
			}
		}

		if ( strcmp(aSeg[2], "approval") == 0 && MDO_METHOD(POST) ) {
			char aId[80], aDecision[24];
			int iDecision = 0;

			NEED_BODY();
			JStr(&tJ, "id", aId, sizeof(aId));
			JStr(&tJ, "decision", aDecision, sizeof(aDecision));
			BODY_DONE();
			if ( strcmp(aDecision, "allow-once") == 0 ) iDecision = 1;
			else if ( strcmp(aDecision, "allow-session") == 0 ) iDecision = 2;
			else iDecision = 0;
			pRun = MdoRunFind(uTurnId);
			if ( pRun == NULL )
				return ReplyErr(pReq, 404, "turn not found") ? XS_OK : XS_OK;
			xrtMutexLock(pRun->pALock);
			if ( strcmp(pRun->aApId, aId) == 0 && pRun->iApDecision < 0 ) {
				pRun->iApDecision = iDecision;
				xrtCondBroadcast(pRun->pACond);
			}
			xrtMutexUnlock(pRun->pALock);
			MdoRunRelease(pRun);
			return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
		}

		
if ( strcmp(aSeg[2], "cancel") == 0 && MDO_METHOD(POST) ) {
			pRun = MdoRunFind(uTurnId);
			if ( pRun == NULL )
				return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
			xrtCancelRequest(pRun->pCancel);
			xrtMutexLock(g_lock);
			if ( pRun->pAgent != NULL ) xworkAgentCancel(pRun->pAgent);
			xrtMutexUnlock(g_lock);
			/* 审批等待一并解除（按拒绝处理） */
			xrtMutexLock(pRun->pALock);
			if ( pRun->iApDecision < 0 ) {
				pRun->iApDecision = 0;
				xrtCondBroadcast(pRun->pACond);
			}
			xrtMutexUnlock(pRun->pALock);
			MdoRunRelease(pRun);
			return ReplyJSON(pReq, 200, "{\"ok\":true}") ? XS_OK : XS_OK;
		}
	}

#undef NEED_BODY
#undef BODY_DONE
#undef PROMPT_FREE_IMAGES

	return XS_FALLBACK;
}

/* ================================================================== */
/* 服务生命周期                                                         */
/* ================================================================== */

void ServiceInit(XS_HostInfo* pHost)
{
	if ( pHost != NULL && pHost->Path != NULL )
		snprintf(g_wwwRoot, sizeof(g_wwwRoot), "%s", pHost->Path);
	MdoStoreInit();
	MdoSchedInit();
	/* GUI 进程无控制台：printf 写无效 stdout 会崩（2026-09-20 打包崩溃根因）——
	 * 诊断信息改走 data/audit.log（MdoAuditLog 已覆盖关键事件） */
}

void ServiceUnit(XS_HostInfo* pHost)
{
	size_t i;
	(void)pHost;

	/* 取消所有在途回合并等它们收尾（取消令牌贯通模型调用与审批等待） */
	xrtMutexLock(g_lock);
	for ( i = 0; i < g_nRuns; i++ ) {
		xrtCancelRequest(g_runs[i]->pCancel);
		if ( g_runs[i]->pAgent != NULL ) xworkAgentCancel(g_runs[i]->pAgent);
	}
	xrtMutexUnlock(g_lock);
	for ( i = 0; i < 100 && g_nRuns > 0; i++ ) xrtSleep(50);

	/* 残余回合（前端没排空的）：强制回落表引用后释放 */
	xrtMutexLock(g_lock);
	while ( g_nRuns > 0 ) {
		MdoRun* pZ = g_runs[--g_nRuns];
		if ( pZ->uRefs > 0 ) pZ->uRefs--;
		if ( pZ->uRefs == 0 ) MdoRunFreeShallow(pZ);
	}
	xrtMutexUnlock(g_lock);

	MdoStoreUnit();
	if ( g_lock != NULL ) {
		xmutex* pLock = g_lock;
		g_lock = NULL;
		xrtMutexDestroy(pLock);
	}
	printf("[mdo] backend stopped\n");
}
