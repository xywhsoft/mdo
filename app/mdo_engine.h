/*
 * mdo_engine.h — 回合引擎：事件队列、worker 线程、流式/工具/审批桥接
 *
 * 一次 POST /api/prompt = 一个 MdoRun（worker 线程跑 RunWithTools）。
 * 三个观察通道（xllm-session 头文件语义）：
 *   1. xllm 流回调（引擎 worker 线程）→ assistant/chunk | assistant/reasoning | assistant/usage
 *   2. pOnRound（回合线程，响应已入账）→ assistant/message | session/stats
 *   3. xwork OnEvent / OnPermission（回合线程）→ tool/result | approval/*
 * 所有事件先进本回合队列（前端 GET /api/turn/<id>/events 轮询），
 * 同时写穿到会话 UI 日志（前端刷新后 GET /api/sessions/<id>/events 重放）。
 */

#ifndef MDO_ENGINE_H
#define MDO_ENGINE_H

#define MDO_MAX_RUNS 16
#define MDO_APPROVAL_TIMEOUT_MS (30u * 60u * 1000u)
#define MDO_MAX_IMAGES 4
#define MDO_IMAGE_MAX_BYTES (4u * 1024u * 1024u)

/* 多模态附件：dataUrl 供 UI 事件回放，解码字节供 xllm IMAGE part */
typedef struct {
	char* sDataUrl;             /* 拥有（含 data:image/...;base64, 前缀全串） */
	unsigned char* pBytes;      /* 拥有（xrt 堆） */
	size_t iSize;
	char aMime[40];
} MdoImage;

/* ------------------------------------------------------------------ */
/* 事件队列                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
	xmutex* pLock;
	char** pLines;          /* 每项一行 JSON（拥有，xrtFree） */
	size_t n, cap;
	uint64 uNextSeq;        /* 从 1 起 */
	bool bDone;
} MdoEventQ;

static bool MdoQInit(MdoEventQ* pQ)
{
	pQ->pLock = xrtMutexCreate();
	pQ->pLines = NULL;
	pQ->n = pQ->cap = 0;
	pQ->uNextSeq = 1;
	pQ->bDone = false;
	return pQ->pLock != NULL;
}

static void MdoQUnit(MdoEventQ* pQ)
{
	size_t i;
	if ( pQ->pLines != NULL ) {
		for ( i = 0; i < pQ->n; i++ ) xrtFree(pQ->pLines[i]);
		xrtFree(pQ->pLines);
	}
	if ( pQ->pLock != NULL ) xrtMutexDestroy(pQ->pLock);
	memset(pQ, 0, sizeof(*pQ));
}

/* ------------------------------------------------------------------ */
/* 回合                                                                */
/* ------------------------------------------------------------------ */

typedef struct MdoRun {
	uint64 uTurnId;
	MdoSession* pSess;      /* 借用 */
	MdoEventQ* pQ;          /* 拥有 */
	xcancel* pCancel;       /* 拥有 */
	char* sPrompt;          /* 拥有 */
	char aModelId[64];
	MdoModel* pModel;       /* 借用（客户端已就绪） */

	/* 工具耗时表（callId → 开始 ms；仅回合线程访问） */
	struct { char aId[64]; uint64 uStartMs; } aTools[64];
	size_t nTools;

	/* 审批等待（OnPermission 在回合线程阻塞，HTTP 线程解除） */
	xmutex* pALock;
	xcond* pACond;
	char aApId[80];
	int iApDecision;        /* -1 等待 | 0 拒绝 | 1 允许一次 | 2 本会话均允许 */
	bool bAutoAllow;        /* allow-session 粘滞 */
	uint32 uApSeq;
	/* ask_user（人一件）：等待用户结构化作答；与审批共用 pALock/pACond */
	char aAskId[80];
	char aAskAnswer[1024];
	int iAskState;          /* -1 等待 | 0 未答/取消 | 1 已答 */
	uint32 uAskSeq;

	/* 回合线程写、HTTP 线程读（g_lock 下换手） */
	xwork_agent* pAgent;

	/* 末轮合成：库在末轮（无工具调用）不触发 pOnRound——流回调累积
	 * reasoning，收尾时与 sFinalText 合成 assistant/message（重放保真）。
	 * 仅回合线程访问；pOnRound 时清零重计。 */
	char* sReasonBuf;
	size_t iReasonLen, iReasonCap;

	/* 计时/出账累加（末轮 stats 的 ms/tps 来源；引擎回调线程与回合线程竞争，
	 * 只做 += 与赋值——64 位对齐平台上足够（统计用途，偶发丢加不致错） */
	uint64 uLlmMs;
	uint64 uOutTokens;
	uint64 uRoundStartMs;

	/* 多模态附件（多张图 + 文本一条 user 消息；GLM anthropic 要求 image+text 成对） */
	MdoImage aImages[MDO_MAX_IMAGES];
	size_t nImages;

	/* 生命周期：完成回合留在表内等前端排空拉取；引用计数守护释放 */
	uint32 uRefs;           /* 1 = 表持有；MdoRunFind +1 / Release -1 */
	bool bDrained;          /* 前端已取空（bDone 且本次轮询无新事件） */
} MdoRun;

static MdoRun* g_runs[MDO_MAX_RUNS];
static size_t g_nRuns = 0;
static uint64 g_uTurnSeq = 0;

static void MdoRunFreeShallow(MdoRun* pRun);

/* 引用释放；归零即释放全部资源（最后一个持有者调用，线程已分离） */
static void MdoRunRelease(MdoRun* pRun)
{
	bool bFree = false;

	if ( pRun == NULL ) return;
	xrtMutexLock(g_lock);
	if ( pRun->uRefs > 0 ) pRun->uRefs--;
	if ( pRun->uRefs == 0 ) bFree = true;
	xrtMutexUnlock(g_lock);
	if ( bFree ) MdoRunFreeShallow(pRun);
}

static MdoRun* MdoRunFind(uint64 uTurnId)
{
	size_t i;
	MdoRun* pR = NULL;
	xrtMutexLock(g_lock);
	for ( i = 0; i < g_nRuns; i++ )
		if ( g_runs[i] != NULL && g_runs[i]->uTurnId == uTurnId ) {
			pR = g_runs[i];
			pR->uRefs++;
			break;
		}
	xrtMutexUnlock(g_lock);
	return pR;
}

static void MdoToolMark(MdoRun* pRun, const char* sCallId)
{
	if ( pRun->nTools < 64 ) {
		snprintf(pRun->aTools[pRun->nTools].aId, sizeof(pRun->aTools[0].aId),
			"%s", sCallId ? sCallId : "");
		pRun->aTools[pRun->nTools].uStartMs = MdoNowMs();
		pRun->nTools++;
	}
}

static uint64 MdoToolMs(MdoRun* pRun, const char* sCallId)
{
	size_t i;
	for ( i = 0; i < pRun->nTools; i++ )
		if ( strcmp(pRun->aTools[i].aId, sCallId ? sCallId : "") == 0 )
			return MdoNowMs() - pRun->aTools[i].uStartMs;
	return 0;
}

/* ------------------------------------------------------------------ */
/* 事件发射（data 对象由回调填充；队列 + UI 日志写穿）                     */
/* ------------------------------------------------------------------ */

typedef void (*MdoDataFn)(xjsonwriter* pW, void* pCtx);

static void MdoEmitFn(MdoRun* pRun, const char* sType, MdoDataFn fn, void* pCtx)
{
	xjsonwriter* pW = JWOpen();

	if ( pW == NULL ) return;
	xrtMutexLock(pRun->pQ->pLock);
	{
		uint64 uSeq = pRun->pQ->uNextSeq++;
		xrtJsonWriterObject(pW);
		JWName(pW, "seq"); xrtJsonWriterUInt(pW, uSeq);
		JWName(pW, "time"); xrtJsonWriterUInt(pW, MdoNowMs());
		JWName(pW, "type"); JWStr(pW, sType);
		JWName(pW, "data");
		xrtJsonWriterObject(pW);
		if ( fn != NULL ) fn(pW, pCtx);
		xrtJsonWriterEnd(pW);        /* data */
		xrtJsonWriterEnd(pW);        /* event */
		{
			char* sLine = JWTake(pW);
			if ( sLine != NULL ) {
				while ( pRun->pQ->n >= pRun->pQ->cap ) {
					size_t iNewCap = pRun->pQ->cap ? pRun->pQ->cap * 2 : 64;
					char** pNew = (char**)xrtRealloc(pRun->pQ->pLines,
						iNewCap * sizeof(char*));
					if ( pNew == NULL ) break;
					pRun->pQ->pLines = pNew;
					pRun->pQ->cap = iNewCap;
				}
				if ( pRun->pQ->n < pRun->pQ->cap ) {
					pRun->pQ->pLines[pRun->pQ->n++] = sLine;
					MdoUiAppend(pRun->pSess, sLine);
				} else {
					xrtFree(sLine);
				}
			}
		}
	}
	xrtMutexUnlock(pRun->pQ->pLock);
}

/* —— 通用单字符串字段 data —— */
typedef struct { const char* sKey; const char* sVal; } MdoKv;

static void MdoWriteKv(xjsonwriter* pW, void* pCtx)
{
	MdoKv* pKv = (MdoKv*)pCtx;
	JWName(pW, pKv->sKey);
	JWStr(pW, pKv->sVal ? pKv->sVal : "");
}

static void MdoEmitKv(MdoRun* pRun, const char* sType,
	const char* sKey, const char* sVal)
{
	MdoKv tKv = { sKey, sVal };
	MdoEmitFn(pRun, sType, MdoWriteKv, &tKv);
}

static void MdoWriteEmpty(xjsonwriter* pW, void* pCtx)
{
	(void)pW; (void)pCtx;
}

static void MdoEmitEmpty(MdoRun* pRun, const char* sType)
{
	MdoEmitFn(pRun, sType, MdoWriteEmpty, NULL);
}

/* —— session/running {value} —— */
static void MdoWriteRunning(xjsonwriter* pW, void* pCtx)
{
	JWName(pW, "value");
	JWBool(pW, pCtx != NULL);
}

static void MdoEmitRunning(MdoRun* pRun, bool bValue)
{
	MdoEmitFn(pRun, "session/running", MdoWriteRunning,
		bValue ? (void*)1 : (void*)0);
}

/* —— assistant/chunk | assistant/reasoning {delta} —— */
typedef struct { const char* sData; size_t iLen; } MdoDelta;

static void MdoWriteDelta(xjsonwriter* pW, void* pCtx)
{
	MdoDelta* pD = (MdoDelta*)pCtx;
	JWName(pW, "delta");
	JWStrN(pW, pD->sData, pD->iLen);
}

static void MdoEmitDelta(MdoRun* pRun, const char* sType,
	const char* sData, size_t iLen)
{
	MdoDelta tD = { sData, iLen };
	MdoEmitFn(pRun, sType, MdoWriteDelta, &tD);
}

/* —— user/message {content:[{type:"image",dataUrl,name}..., {type:"text",text}]} —— */
typedef struct {
	const char* sText;
	const MdoImage* aImages;
	size_t nImages;
} MdoUserMsgCtx;

static void MdoWriteUserMessage(xjsonwriter* pW, void* pCtx)
{
	MdoUserMsgCtx* pU = (MdoUserMsgCtx*)pCtx;
	size_t i;

	JWName(pW, "content");
	xrtJsonWriterArray(pW);
	for ( i = 0; i < pU->nImages && i < MDO_MAX_IMAGES; i++ ) {
		xrtJsonWriterObject(pW);
		JWName(pW, "type"); JWStr(pW, "image");
		JWName(pW, "dataUrl"); JWStr(pW, pU->aImages[i].sDataUrl);
		JWName(pW, "name");
		JWStr(pW, pU->aImages[i].aMime);
		xrtJsonWriterEnd(pW);
	}
	xrtJsonWriterObject(pW);
	JWName(pW, "type"); JWStr(pW, "text");
	JWName(pW, "text"); JWStr(pW, pU->sText);
	xrtJsonWriterEnd(pW);
	xrtJsonWriterEnd(pW);
}

/* —— session/title {title} 用 MdoWriteKv —— */

/* ------------------------------------------------------------------ */
/* 通道 1：xllm 流回调（引擎 worker 线程；快进快出）                       */
/* ------------------------------------------------------------------ */

static bool MdoOnStreamEvent(void* pUserData, const xllm_event* pEvent)
{
	MdoRun* pRun = (MdoRun*)pUserData;

	switch ( pEvent->eKind ) {
	case XLLM_EVENT_RESPONSE_START:
		pRun->uRoundStartMs = MdoNowMs();
		break;
	case XLLM_EVENT_RESPONSE_DONE:
		if ( pRun->uRoundStartMs != 0 ) {
			pRun->uLlmMs += MdoNowMs() - pRun->uRoundStartMs;
			pRun->uRoundStartMs = 0;
		}
		break;
	case XLLM_EVENT_TEXT_DELTA:
		MdoEmitDelta(pRun, "assistant/chunk",
			pEvent->as.tText.sData, pEvent->as.tText.iLen);
		break;
	case XLLM_EVENT_REASONING_DELTA:
		MdoEmitDelta(pRun, "assistant/reasoning",
			pEvent->as.tText.sData, pEvent->as.tText.iLen);
		/* 末轮合成用（pOnRound 清零） */
		if ( pEvent->as.tText.sData != NULL && pEvent->as.tText.iLen > 0 ) {
			if ( pRun->iReasonLen + pEvent->as.tText.iLen + 1 > pRun->iReasonCap ) {
				size_t iNewCap = pRun->iReasonCap ? pRun->iReasonCap : 1024;
				char* pNew;
				while ( pRun->iReasonLen + pEvent->as.tText.iLen + 1 > iNewCap )
					iNewCap *= 2;
				pNew = (char*)xrtRealloc(pRun->sReasonBuf, iNewCap);
				if ( pNew != NULL ) {
					pRun->sReasonBuf = pNew;
					pRun->iReasonCap = iNewCap;
				}
			}
			if ( pRun->iReasonLen + pEvent->as.tText.iLen < pRun->iReasonCap ) {
				memcpy(pRun->sReasonBuf + pRun->iReasonLen,
					pEvent->as.tText.sData, pEvent->as.tText.iLen);
				pRun->iReasonLen += pEvent->as.tText.iLen;
				pRun->sReasonBuf[pRun->iReasonLen] = 0;
			}
		}
		break;
	default:
		break;
	}
	return true;    /* false = 取消 */
}

/* ------------------------------------------------------------------ */
/* 通道 2：pOnRound（回合线程；响应已入账、工具未执行）                    */
/* ------------------------------------------------------------------ */

static void MdoWriteAssistantMessage(xjsonwriter* pW, void* pCtx)
{
	const xllm_response* pResp = (const xllm_response*)pCtx;
	size_t i;

	JWName(pW, "message");
	xrtJsonWriterObject(pW);
	JWName(pW, "model");
	JWStr(pW, pResp->sModel);
	JWName(pW, "content");
	xrtJsonWriterArray(pW);
	for ( i = 0; i < pResp->iBlockCount; i++ ) {
		const xllm_block* pB = &pResp->pBlocks[i];
		if ( pB->eKind == XLLM_BLOCK_TEXT ) {
			char* sHTML = (pB->sText && pB->sText[0]) ?
				MdoRenderHTML(pB->sText, strlen(pB->sText)) : NULL;
			xrtJsonWriterObject(pW);
			JWName(pW, "type"); JWStr(pW, "text");
			JWName(pW, "text"); JWStr(pW, pB->sText);
			if ( sHTML != NULL ) {
				JWName(pW, "html");
				JWStr(pW, sHTML);       /* md4c 预渲染（NOHTML 已转义） */
				xrtFree(sHTML);
			}
			xrtJsonWriterEnd(pW);
		} else if ( pB->eKind == XLLM_BLOCK_REASONING ) {
			xrtJsonWriterObject(pW);
			JWName(pW, "type"); JWStr(pW, "reasoning");
			JWName(pW, "text"); JWStr(pW, pB->sText);
			xrtJsonWriterEnd(pW);
		} else if ( pB->eKind == XLLM_BLOCK_TOOL_CALL &&
		            pB->iToolIndex < pResp->iToolCallCount ) {
			const xllm_tool_call* pCall = &pResp->pToolCalls[pB->iToolIndex];
			xrtJsonWriterObject(pW);
			JWName(pW, "type"); JWStr(pW, "tool-call");
			JWName(pW, "callId"); JWStr(pW, pCall->sId);
			JWName(pW, "name"); JWStr(pW, pCall->sName);
			JWName(pW, "input"); JWStr(pW, pCall->sArgumentsJson);
			xrtJsonWriterEnd(pW);
		}
	}
	xrtJsonWriterEnd(pW);       /* content */
	JWName(pW, "usage");
	xrtJsonWriterObject(pW);
	JWName(pW, "prompt");
	xrtJsonWriterUInt(pW, pResp->tUsage.uInputTokens);
	JWName(pW, "completion");
	xrtJsonWriterUInt(pW, pResp->tUsage.uOutputTokens);
	xrtJsonWriterEnd(pW);
	xrtJsonWriterEnd(pW);       /* message */
}

typedef struct {
	xllm_session_stats* pStats;
	const xllm_response* pResp;
} MdoStatsCtx;

/* 末轮合成：库对无工具调用的末轮不触发 pOnRound（run.c 在 pOnRound 前跳出） */
typedef struct {
	const char* sModel;
	const char* sText;        /* summary.sFinalText */
	const char* sReasoning;   /* 流回调累积（pOnRound 清零） */
	const xllm_usage* pUsage; /* summary.tLastUsage */
} MdoFinalMsgCtx;

static void MdoWriteFinalMessage(xjsonwriter* pW, void* pCtx)
{
	MdoFinalMsgCtx* pF = (MdoFinalMsgCtx*)pCtx;

	JWName(pW, "message");
	xrtJsonWriterObject(pW);
	JWName(pW, "model");
	JWStr(pW, pF->sModel);
	JWName(pW, "content");
	xrtJsonWriterArray(pW);
	if ( pF->sReasoning[0] ) {
		xrtJsonWriterObject(pW);
		JWName(pW, "type"); JWStr(pW, "reasoning");
		JWName(pW, "text"); JWStr(pW, pF->sReasoning);
		xrtJsonWriterEnd(pW);
	}
	{
		char* sHTML = pF->sText[0] ?
			MdoRenderHTML(pF->sText, strlen(pF->sText)) : NULL;
		xrtJsonWriterObject(pW);
		JWName(pW, "type"); JWStr(pW, "text");
		JWName(pW, "text"); JWStr(pW, pF->sText);
		if ( sHTML != NULL ) {
			JWName(pW, "html");
			JWStr(pW, sHTML);
			xrtFree(sHTML);
		}
		xrtJsonWriterEnd(pW);
	}
	xrtJsonWriterEnd(pW);
	JWName(pW, "usage");
	xrtJsonWriterObject(pW);
	JWName(pW, "prompt");
	xrtJsonWriterUInt(pW, pF->pUsage->uInputTokens);
	JWName(pW, "completion");
	xrtJsonWriterUInt(pW, pF->pUsage->uOutputTokens);
	xrtJsonWriterEnd(pW);
	xrtJsonWriterEnd(pW);
}

typedef struct {
	xllm_session_stats* pStats;
	const xllm_usage* pUsage;
	uint64 uLlmMs;        /* 全回合模型耗时（流事件计时累加） */
	uint64 uOutTokens;    /* 全回合输出 token（含工具轮） */
} MdoFinalStatsCtx;

static void MdoWriteFinalStats(xjsonwriter* pW, void* pCtx)
{
	MdoFinalStatsCtx* pF = (MdoFinalStatsCtx*)pCtx;

	JWName(pW, "ctxTokens");
	xrtJsonWriterUInt(pW, pF->pStats->uRenderedActiveTokens);
	JWName(pW, "ms");
	xrtJsonWriterUInt(pW, pF->uLlmMs);
	JWName(pW, "tps");
	if ( pF->uLlmMs > 0 ) {
		char aTps[16];
		long double fTps = (long double)pF->uOutTokens * 1000.0L /
			(long double)pF->uLlmMs;
		snprintf(aTps, sizeof(aTps), "%.1f", (double)fTps);
		JWStr(pW, aTps);
	} else {
		JWStr(pW, "");
	}
	JWName(pW, "promptTokens");
	xrtJsonWriterUInt(pW, pF->pUsage->uInputTokens);
	JWName(pW, "completionTokens");
	xrtJsonWriterUInt(pW, pF->uOutTokens);
}

static void MdoWriteStats(xjsonwriter* pW, void* pCtx)
{
	MdoStatsCtx* pS = (MdoStatsCtx*)pCtx;

	JWName(pW, "ctxTokens");
	xrtJsonWriterUInt(pW, pS->pStats->uRenderedActiveTokens);
	JWName(pW, "ms");
	xrtJsonWriterUInt(pW, pS->pResp->tStats.uTotalMs);
	JWName(pW, "tps");
	xrtJsonWriterFloat(pW, pS->pResp->tStats.fOutputTokensPerSec);
	JWName(pW, "promptTokens");
	xrtJsonWriterUInt(pW, pS->pResp->tUsage.uInputTokens);
	JWName(pW, "completionTokens");
	xrtJsonWriterUInt(pW, pS->pResp->tUsage.uOutputTokens);
}

static bool MdoOnRound(xllm_session* pSession, uint32 uRound,
	const xllm_response* pResponse, size_t iPendingToolCalls, void* pUserData)
{
	MdoRun* pRun = (MdoRun*)pUserData;
	xllm_session_stats tStats;

	(void)uRound; (void)iPendingToolCalls;

	MdoEmitFn(pRun, "assistant/message", MdoWriteAssistantMessage,
		(void*)pResponse);
	pRun->uOutTokens += pResponse->tUsage.uOutputTokens;

	if ( xllmSessionGetStats(pSession, &tStats) ) {
		MdoStatsCtx tCtx = { &tStats, pResponse };
		MdoEmitFn(pRun, "session/stats", MdoWriteStats, &tCtx);
	}
	/* 本轮已落账：末轮合成缓冲清零重计 */
	pRun->iReasonLen = 0;
	if ( pRun->sReasonBuf != NULL ) pRun->sReasonBuf[0] = 0;
	return true;    /* false = 挂起工具停止回合 */
}

/* ------------------------------------------------------------------ */
/* 通道 3：xwork 事件 + 审批（回合线程）                                 */
/* ------------------------------------------------------------------ */

typedef struct {
	const xwork_event* pEvent;
	uint64 uMs;
} MdoToolCtx;

static void MdoWriteToolResult(xjsonwriter* pW, void* pCtx)
{
	MdoToolCtx* pT = (MdoToolCtx*)pCtx;
	const xwork_event* pE = pT->pEvent;

	JWName(pW, "callId");
	JWStr(pW, pE->sToolCallId ? pE->sToolCallId : "");
	JWName(pW, "output");
	JWStrN(pW, pE->sText, pE->iTextLength);
	JWName(pW, "isError");
	JWBool(pW, !pE->bSuccess);
	JWName(pW, "ms");
	xrtJsonWriterUInt(pW, pT->uMs);
}

static bool MdoOnXworkEvent(void* pUserData, const xwork_event* pEvent)
{
	MdoRun* pRun = (MdoRun*)pUserData;

	switch ( pEvent->eKind ) {
	case XWORK_EVENT_TOOL_START:
		if ( pEvent->sToolCallId != NULL )
			MdoToolMark(pRun, pEvent->sToolCallId);
		break;
	case XWORK_EVENT_TOOL_DONE:
		{
			MdoToolCtx tCtx = { pEvent, MdoToolMs(pRun, pEvent->sToolCallId) };
			MdoEmitFn(pRun, "tool/result", MdoWriteToolResult, &tCtx);
		}
		break;
	default:
		break;
	}
	return true;    /* false = 请求取消 */
}

typedef struct {
	const char* sId;
	const char* sKind;
	const char* sTitle;
	const char* sDetail;
	size_t iDetailLen;
	const char* sDecision;   /* resolved 用；requested 时 NULL */
} MdoApCtx;

static void MdoWriteApproval(xjsonwriter* pW, void* pCtx)
{
	MdoApCtx* pA = (MdoApCtx*)pCtx;

	JWName(pW, "id");
	JWStr(pW, pA->sId);
	if ( pA->sDecision == NULL ) {
		JWName(pW, "kind");
		JWStr(pW, pA->sKind);
		JWName(pW, "title");
		JWStr(pW, pA->sTitle);
		JWName(pW, "detail");
		JWStrN(pW, pA->sDetail, pA->iDetailLen);
	} else {
		JWName(pW, "decision");
		JWStr(pW, pA->sDecision);
	}
}

static const char* MdoDecisionName(int iDecision)
{
	switch ( iDecision ) {
	case 1:  return "allow-once";
	case 2:  return "allow-session";
	default: return "rejected";
	}
}

#include "mdo_ask.h"

static xwork_permission_decision MdoOnPermission(void* pUserData,
	const xwork_permission_request* pRequest)
{
	MdoRun* pRun = (MdoRun*)pUserData;
	char aId[80];
	char aTitle[220];
	int iDecision;
	uint64 uWaitStart = MdoNowMs();
	const char* sArgs = pRequest->sArgumentsJson ? pRequest->sArgumentsJson : "";
	if ( pRequest->sToolName != NULL && strcmp(pRequest->sToolName, "ask_user") == 0 )
		return XWORK_PERMISSION_ALLOW;   /* ask_user 自身即与用户交互，不二次审批 */

	snprintf(aId, sizeof(aId), "ap%u", ++pRun->uApSeq);
	snprintf(aTitle, sizeof(aTitle), "%s%s%s",
		pRequest->sToolName ? pRequest->sToolName : "?",
		(pRequest->sResource && pRequest->sResource[0]) ? " · " : "",
		(pRequest->sResource && pRequest->sResource[0]) ?
			pRequest->sResource : "");
	{
		MdoApCtx tA = { aId,
			pRequest->sToolName ? pRequest->sToolName : "",
			aTitle, sArgs,
			Utf8Clip(sArgs, strlen(sArgs), 2048), NULL };
		MdoEmitFn(pRun, "approval/requested", MdoWriteApproval, &tA);
	}

	iDecision = 1;
	xrtMutexLock(pRun->pALock);
	if ( !pRun->bAutoAllow ) {
		snprintf(pRun->aApId, sizeof(pRun->aApId), "%s", aId);
		pRun->iApDecision = -1;
		while ( pRun->iApDecision < 0 ) {
			if ( xrtCancelRequested(pRun->pCancel) ) { pRun->iApDecision = 0; break; }
			if ( MdoNowMs() - uWaitStart > MDO_APPROVAL_TIMEOUT_MS ) {
				pRun->iApDecision = 0;
				break;
			}
			xrtCondWaitFor(pRun->pACond, pRun->pALock, 200 * 1000);
		}
		iDecision = pRun->iApDecision;
	}
	if ( iDecision == 2 ) pRun->bAutoAllow = true;
	xrtMutexUnlock(pRun->pALock);

	{
		MdoApCtx tA = { aId, "", "", "", 0, MdoDecisionName(iDecision) };
		MdoEmitFn(pRun, "approval/resolved", MdoWriteApproval, &tA);
	}

	return iDecision == 0 ? XWORK_PERMISSION_DENY : XWORK_PERMISSION_ALLOW;
}

/* ------------------------------------------------------------------ */
/* worker 线程                                                          */
/* ------------------------------------------------------------------ */

/* 回合线程收尾：置 done、解除会话占位。
 * 回合留在 g_runs 里等前端排空拉取（表引用不在此回落——出表时才降）；
 * 资源释放由 MdoRunRelease 在最后一个引用持有者处统一执行。 */
static void MdoRunFinish(MdoRun* pRun)
{
	xrtMutexLock(g_lock);
	if ( pRun->pSess != NULL ) pRun->pSess->pRun = NULL;
	xrtMutexUnlock(g_lock);

	xrtMutexLock(pRun->pQ->pLock);
	pRun->pQ->bDone = true;
	xrtMutexUnlock(pRun->pQ->pLock);
}

static int32 MdoRunThread(void* pArg)
{
	MdoRun* pRun = (MdoRun*)pArg;
	MdoSession* pSess = pRun->pSess;
	xllm_error tErr;
	xllm_run_policy tPolicy;
	xllm_run_summary tSummary;
	xllm_stream_callbacks tCb;
	xllm_executor tExec;
	xwork_agent_config tCfg;
	xwork_error tWErr;
	xwork_agent* pAgent;
	xllm_result eRes;
	bool bBound;
	MdoProject* pProj;
	char aSnap[380];
	xllm_session_stats tStats;

	xllmErrorInit(&tErr);
	xllmRunSummaryUnit(&tSummary);
	pProj = MdoActiveProject();

	MdoEmitRunning(pRun, true);

	/* 首用注入身份：环境块 + AGENTS.md（宿主拥有身份，mdo 设计 §七） */
	if ( xllmSessionGetStats(pSess->pSession, &tStats) &&
	     tStats.uEntryCount == 0 && pProj != NULL ) {
		char aSys[20000];
		MdoBuildSystemPrompt(pProj, pSess, aSys, sizeof(aSys));
		xllmSessionSetSystemPrompt(pSess->pSession, aSys, &tErr);
	}

	/* xwork agent：每回合重建（注册表轻；权限/事件闭包直取本回合） */
	xworkErrorInit(&tWErr);
	xworkAgentConfigInit(&tCfg);
	tCfg.pClient = pRun->pModel->pClient;
	tCfg.pSession = pSess->pSession;
	{
		/* 任务态根 = _tasks 桶（与系统提示词的工作目录一致，否则模型 ls "src" 必报不存在） */
		static char aTasksRoot[400];
		char* pSlug = MdoPathJoin(g_projectsDir, MDO_TASKS_SLUG);
		snprintf(aTasksRoot, sizeof(aTasksRoot), "%s", pSlug ? pSlug : ".");
		xrtFree(pSlug);
		tCfg.sWorkspaceRoot = pProj ? pProj->aPath : aTasksRoot;
	}
	tCfg.eApprovalMode = XWORK_APPROVAL_CALLBACK;
	tCfg.OnPermission = MdoOnPermission;
	tCfg.pPermissionUserData = pRun;
	tCfg.OnEvent = MdoOnXworkEvent;
	tCfg.pEventUserData = pRun;
	tCfg.bRegisterBuiltinTools = true;
	/* 工具输出内联截断线 = 动态预算（与会话拒绝线同源：预算+1KB），大结果落 artifact 续读 */
	tCfg.iMaxInlineToolBytes = (size_t)MdoToolBudgetBytes(pSess->uContextWindow);
	/* 探索三件（ls/glob/grep 进程内实现）：不依赖外部程序 */
	tCfg.bRegisterExploreTools = true;
	/* python 三态：优先用随程序分发的 tools/python313，否则回退 PATH */
	{
		char* pExe = xrtPathExecutable();          /* 调用方 xrtFree */
		char* pPy = pExe ? MdoPathJoin(pExe, "tools\\python313\\python.exe") : NULL;
		if ( pPy != NULL && xrtFileExists((str)pPy) )
			tCfg.sPythonPath = pPy;                /* 仅创建期间借用；agent 内部 strdup */
		else
			xrtFree(pPy);
		xrtFree(pExe);
	}
	tCfg.bRegisterPythonTool = true;
	tCfg.eEolPolicy = XWORK_EOL_AUTO;
	pAgent = xworkAgentCreate(&tCfg, &tWErr);
	if ( pAgent != NULL && !MdoWebRegisterTools(pAgent, &tWErr) )
		{ xworkErrorInit(&tWErr); }   /* 搜索三件注册失败不阻断会话（非核心件） */
	if ( pAgent != NULL && !MdoAskRegisterTool(pAgent, pRun, &tWErr) )
		{ xworkErrorInit(&tWErr); }   /* ask_user 注册失败不阻断会话 */
	bBound = pAgent != NULL && xworkExecutorBind(&tExec, pAgent, &tWErr);
	if ( bBound ) xworkAgentRunBegin(pAgent, &tWErr), bBound = (tWErr.eCode == 0 ||
		tWErr.sMessage[0] == 0);
	if ( pAgent == NULL || !bBound ) {
		MdoEmitKv(pRun, "session/error", "message",
			tWErr.sMessage[0] ? tWErr.sMessage : "xwork agent 初始化失败");
		if ( pAgent != NULL ) xworkAgentDestroy(pAgent);
		MdoEmitRunning(pRun, false);
		xrtMutexLock(pRun->pQ->pLock);
		pRun->pQ->bDone = true;
		xrtMutexUnlock(pRun->pQ->pLock);
		MdoRunFinish(pRun);
		return 0;
	}
	{
		xrtMutexLock(g_lock);
		pRun->pAgent = pAgent;
		xrtMutexUnlock(g_lock);
	}

	memset(&tCb, 0, sizeof(tCb));
	tCb.pUserData = pRun;
	tCb.OnEvent = MdoOnStreamEvent;

	xllmRunPolicyInit(&tPolicy);
	tPolicy.uMaxRounds = 64;              /* 库默认护栏；更强策略走 pOnRound */
	tPolicy.pCancel = pRun->pCancel;
	tPolicy.pOnRound = MdoOnRound;
	tPolicy.pUserData = pRun;

	/* 多模态：预挂 image+text 成对的 user 消息（GLM anthropic 要求成对），
	 * 然后 RunWithTools(NULL) 从既有尾部续跑（run.c 的 resume 分支） */
	if ( pRun->nImages > 0 ) {
		uint64 uTurn = xllmSessionBeginTurn(pSess->pSession);
		xllm_message tMsg;
		size_t i;
		bool bOk = uTurn != 0;

		xllmMessageInit(&tMsg, XLLM_ROLE_USER);
		for ( i = 0; bOk && i < pRun->nImages; i++ ) {
			xllm_part tPart;
			xllmPartInit(&tPart, XLLM_PART_IMAGE);
			if ( !xllmPartSetImageData(&tPart, pRun->aImages[i].pBytes,
			     pRun->aImages[i].iSize, pRun->aImages[i].aMime) ||
			     !xllmMessageAddPart(&tMsg, &tPart) ) {
				bOk = false;
			}
			xllmPartUnit(&tPart);
		}
		if ( bOk ) {
			xllm_part tText;
			xllmPartInit(&tText, XLLM_PART_TEXT);
			if ( xllmPartSetText(&tText, pRun->sPrompt) &&
			     xllmMessageAddPart(&tMsg, &tText) ) {
				xllmPartUnit(&tText);
			} else {
				xllmPartUnit(&tText);
				bOk = false;
			}
		}
		if ( bOk )
			bOk = xllmSessionAddMessage(pSess->pSession, uTurn, &tMsg, 0);
		xllmMessageUnit(&tMsg);
		if ( !bOk ) {
			xworkAgentRunEnd(pAgent);
			xworkExecutorUnbind(&tExec);
			xworkAgentDestroy(pAgent);
			MdoEmitKv(pRun, "session/error", "message", "多模态消息构造失败");
			MdoEmitRunning(pRun, false);
			xrtMutexLock(pRun->pQ->pLock);
			pRun->pQ->bDone = true;
			xrtMutexUnlock(pRun->pQ->pLock);
			MdoRunFinish(pRun);
			return 0;
		}
		eRes = xllmSessionRunWithTools(pSess->pSession, NULL, &tExec,
			&tCb, &tPolicy, &tSummary, &tErr);
	} else {
		eRes = xllmSessionRunWithTools(pSess->pSession, pRun->sPrompt, &tExec,
			&tCb, &tPolicy, &tSummary, &tErr);
	}

	xrtMutexLock(g_lock);
	pRun->pAgent = NULL;
	xrtMutexUnlock(g_lock);
	xworkAgentRunEnd(pAgent);
	xworkExecutorUnbind(&tExec);
	xworkAgentDestroy(pAgent);

	/* 终局事件 */
	if ( eRes == XLLM_RESULT_OK ) {
		/* 末轮（无工具调用）不触发 pOnRound：用 sFinalText + 累积 reasoning
		 * 合成末轮 assistant/message，并补一份收尾 stats */
		MdoFinalMsgCtx tFin = { pRun->pModel->aModel,
			tSummary.sFinalText ? tSummary.sFinalText : "",
			(pRun->sReasonBuf && pRun->iReasonLen > 0) ? pRun->sReasonBuf : "",
			&tSummary.tLastUsage };
		MdoEmitFn(pRun, "assistant/message", MdoWriteFinalMessage, &tFin);
		{
			xllm_session_stats tStats2;
			if ( xllmSessionGetStats(pSess->pSession, &tStats2) ) {
				MdoFinalStatsCtx tFs = { &tStats2, &tSummary.tLastUsage,
				pRun->uLlmMs,
				pRun->uOutTokens + tSummary.tLastUsage.uOutputTokens };
				MdoEmitFn(pRun, "session/stats", MdoWriteFinalStats, &tFs);
			}
		}
		xllmFree(tSummary.sFinalText);
	} else if ( eRes == XLLM_RESULT_CANCELLED ||
	            xrtCancelRequested(pRun->pCancel) ) {
		MdoEmitEmpty(pRun, "session/interrupted");
	} else {
		char aMsg[600];
		snprintf(aMsg, sizeof(aMsg), "%s%s%s",
			tErr.sMessage[0] ? tErr.sMessage : "模型调用失败",
			tErr.sProviderMessage[0] ? " · " : "",
			tErr.sProviderMessage[0] ? tErr.sProviderMessage : "");
		MdoEmitKv(pRun, "session/error", "message", aMsg);
	}

	/* 原子快照：全量状态 + 已覆盖日志记录裁剪 */
	if ( pProj != NULL ) {
		xllm_error tSnapErr;
		xllmErrorInit(&tSnapErr);
		MdoSessionPaths(pProj, pSess->aId, NULL, 0, NULL, 0,
			aSnap, sizeof(aSnap), NULL, 0);
		xllmSessionCheckpoint(pSess->pSession, aSnap, &tSnapErr);
	}

	/* meta 收尾 */
	xrtMutexLock(g_lock);
	pSess->uTurns++;
	pSess->uUpdatedAt = MdoNowMs();
	snprintf(pSess->aModelId, sizeof(pSess->aModelId), "%s", pRun->aModelId);
	MdoSessionSaveMetaLocked(pSess);
	xrtMutexUnlock(g_lock);

	MdoEmitRunning(pRun, false);
	xrtMutexLock(pRun->pQ->pLock);
	pRun->pQ->bDone = true;
	xrtMutexUnlock(pRun->pQ->pLock);

	MdoRunFinish(pRun);
	return 0;
}


/* 思考力度注入：pRenderComplete 尾部设置（body 顶层字段，不扰 messages 前缀缓存） */
static bool MdoEffortRenderComplete(xllm_session* pSession, xllm_request* pRequest, void* pUserData)
{
	MdoSession* pS = (MdoSession*)pUserData;
	(void)pSession;
	if ( pS != NULL && pS->aEffort[0] != 0 ) {
		xllmRequestSetReasoningEffort(pRequest, pS->aEffort);
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* 回合启动（HTTP 线程；pSession/pClient 已就绪）                         */
/* ------------------------------------------------------------------ */

static void MdoRunFreeShallow(MdoRun* pRun)
{
	size_t i;
	xrtFree(pRun->sPrompt);
	xrtFree(pRun->sReasonBuf);
	for ( i = 0; i < pRun->nImages && i < MDO_MAX_IMAGES; i++ ) {
		xrtFree(pRun->aImages[i].sDataUrl);
		xrtFree(pRun->aImages[i].pBytes);
	}
	if ( pRun->pCancel != NULL ) xrtCancelDestroy(pRun->pCancel);
	if ( pRun->pALock != NULL ) xrtMutexDestroy(pRun->pALock);
	if ( pRun->pACond != NULL ) xrtCondDestroy(pRun->pACond);
	MdoQUnit(pRun->pQ);
	xrtFree(pRun->pQ);
	xrtFree(pRun);
}

static uint64 MdoRunStart(MdoSession* pSess, const char* sPrompt,
	MdoModel* pModel, const MdoImage* aImages, size_t nImages,
	char* pErrOut, size_t iErrCap)
{
	MdoRun* pRun;
	uint64 uTurnId;
	size_t iSlot;
	size_t i;

	/* 满表先清已排空的僵尸回合（bDone 且前端已拉空） */
	if ( g_nRuns >= MDO_MAX_RUNS ) {
		MdoRun* aFree[MDO_MAX_RUNS];
		size_t nFree = 0;
		xrtMutexLock(g_lock);
		{
			size_t i = 0;
			while ( i < g_nRuns && g_nRuns >= MDO_MAX_RUNS ) {
				MdoRun* pZ = g_runs[i];
				if ( pZ != NULL && pZ->pQ->bDone && pZ->bDrained ) {
					memmove(&g_runs[i], &g_runs[i + 1],
						(g_nRuns - i - 1) * sizeof(MdoRun*));
					g_nRuns--;
					if ( pZ->uRefs > 0 ) pZ->uRefs--;
					if ( pZ->uRefs == 0 ) aFree[nFree++] = pZ;
					continue;
				}
				i++;
			}
		}
		xrtMutexUnlock(g_lock);
		for ( iSlot = 0; iSlot < nFree; iSlot++ ) MdoRunFreeShallow(aFree[iSlot]);
		if ( g_nRuns >= MDO_MAX_RUNS ) {
			snprintf(pErrOut, iErrCap, "concurrent run limit reached");
			return 0;
		}
	}
	pRun = (MdoRun*)xrtRealloc(NULL, sizeof(MdoRun));
	if ( pRun == NULL ) { snprintf(pErrOut, iErrCap, "oom"); return 0; }
	memset(pRun, 0, sizeof(MdoRun));
	pRun->pQ = (MdoEventQ*)xrtRealloc(NULL, sizeof(MdoEventQ));
	if ( pRun->pQ == NULL ) { xrtFree(pRun); snprintf(pErrOut, iErrCap, "oom"); return 0; }
	if ( !MdoQInit(pRun->pQ) ) { MdoQUnit(pRun->pQ); xrtFree(pRun->pQ); xrtFree(pRun);
		snprintf(pErrOut, iErrCap, "queue init failed"); return 0; }
	pRun->pCancel = xrtCancelCreate();
	pRun->pALock = xrtMutexCreate();
	pRun->pACond = xrtCondCreate();
	pRun->sPrompt = (char*)xrtRealloc(NULL, strlen(sPrompt) + 1);
	if ( pRun->sPrompt != NULL ) memcpy(pRun->sPrompt, sPrompt, strlen(sPrompt) + 1);
	if ( pRun->pCancel == NULL || pRun->pALock == NULL || pRun->pACond == NULL ||
	     pRun->sPrompt == NULL ) {
		MdoRunFreeShallow(pRun);
		snprintf(pErrOut, iErrCap, "run resources failed");
		return 0;
	}
	pRun->iApDecision = -1;
	pRun->uRefs = 1;        /* 表引用 */
	pRun->pSess = pSess;
	pRun->pModel = pModel;
	snprintf(pRun->aModelId, sizeof(pRun->aModelId), "%s", pModel->aId);
	/* 多模态附件深拷贝（dataUrl + 解码字节都归 run） */
	for ( i = 0; i < nImages && i < MDO_MAX_IMAGES; i++ ) {
		MdoImage* pDst = &pRun->aImages[pRun->nImages];
		const MdoImage* pSrc = &aImages[i];
		size_t nUrl = strlen(pSrc->sDataUrl) + 1;
		pDst->sDataUrl = (char*)xrtRealloc(NULL, nUrl);
		pDst->pBytes = (unsigned char*)xrtRealloc(NULL, pSrc->iSize);
		if ( pDst->sDataUrl != NULL )
			memcpy(pDst->sDataUrl, pSrc->sDataUrl, nUrl);
		if ( pDst->pBytes != NULL && pSrc->iSize > 0 )
			memcpy(pDst->pBytes, pSrc->pBytes, pSrc->iSize);
		pDst->iSize = pSrc->iSize;
		snprintf(pDst->aMime, sizeof(pDst->aMime), "%s", pSrc->aMime);
		if ( pDst->sDataUrl == NULL || (pSrc->iSize > 0 && pDst->pBytes == NULL) ) {
			xrtFree(pDst->sDataUrl);
			xrtFree(pDst->pBytes);
			pDst->sDataUrl = NULL;
			pDst->pBytes = NULL;
			pDst->iSize = 0;
			break;
		}
		pRun->nImages++;
	}

	xrtMutexLock(g_lock);
	uTurnId = ++g_uTurnSeq;
	pRun->uTurnId = uTurnId;
	g_runs[g_nRuns++] = pRun;
	pSess->pRun = pRun;
	/* 首轮标题推导（UTF-8 安全截 24 字节内） */
	if ( pSess->uTurns == 0 && strcmp(pSess->aTitle, "新的会话") == 0 ) {
		size_t iClip = Utf8Clip(sPrompt, strlen(sPrompt), 24);
		size_t n = 0;
		const char* p = sPrompt;
		/* 压掉换行后的首行 */
		while ( p[n] && p[n] != '\n' && p[n] != '\r' && n < iClip ) n++;
		if ( n > 0 ) {
			memcpy(pSess->aTitle, sPrompt, n);
			pSess->aTitle[n] = 0;
			MdoSessionSaveMetaLocked(pSess);
		}
	}
	iSlot = g_nRuns - 1;
	xrtMutexUnlock(g_lock);

	/* user/message + 首轮标题入列（线程未起，无竞态） */
	{
		MdoUserMsgCtx tU = { sPrompt, pRun->aImages, pRun->nImages };
		MdoEmitFn(pRun, "user/message", MdoWriteUserMessage, &tU);
	}
	if ( pSess->uTurns == 0 && strcmp(pSess->aTitle, "新的会话") != 0 ) {
		MdoEmitKv(pRun, "session/title", "title", pSess->aTitle);
	}

	{
		xthread* hThread = xrtThreadCreate(MdoRunThread, pRun, 0);
		if ( hThread == NULL ) {
			xrtMutexLock(g_lock);
			if ( g_nRuns > iSlot && g_runs[iSlot] == pRun ) {
				memmove(&g_runs[iSlot], &g_runs[iSlot + 1],
					(g_nRuns - iSlot - 1) * sizeof(MdoRun*));
				g_nRuns--;
			}
			pSess->pRun = NULL;
			xrtMutexUnlock(g_lock);
			MdoRunFreeShallow(pRun);
			snprintf(pErrOut, iErrCap, "thread create failed");
			return 0;
		}
		/* 分离：运行线程自持引用，立即归还创建引用 */
		xrtThreadDestroy(hThread);
	}
	return uTurnId;
}

#endif /* MDO_ENGINE_H */
