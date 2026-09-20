/*
 * mdo_ask.h — ask_user 工具（设计文档"人一件"）
 *
 * 模型结构化向用户提问（question + 可选 options），UI 弹卡片，
 * 用户点选项或输入自由文本，答案作为工具结果返回模型。
 * 等待缝复用审批机制同款：MdoRun.pALock/pACond + MdoEmitFn 事件。
 * 无实际超时（4h 兜底防永久挂起）；回合取消时立即返回"未回答"。
 * 本工具自身即与用户交互，不再走二次审批（MdoOnPermission 侧放行）。
 * 注意：本头文件须在 mdo_engine.h 内 MdoRun/MdoEmitFn 定义之后 include。
 */

#ifndef MDO_ASK_H
#define MDO_ASK_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MDO_ASK_ANSWER_MAX   1024
#define MDO_ASK_OPTIONS_MAX  8
#define MDO_ASK_TIMEOUT_MS   (4u * 60u * 60u * 1000u)   /* 4h 兜底 */

typedef struct {
	const char* sId;
	const char* sQuestion;
	const xvalue* pOptions;   /* 借用：字符串数组，可 NULL */
	const char* sAnswer;      /* resolved 用；requested 时 NULL */
} MdoAskCtx;

/* requested: {id, question, options[]}  resolved: {id, answer} */
static void MdoWriteAsk(xjsonwriter* pW, void* pCtx)
{
	MdoAskCtx* pA = (MdoAskCtx*)pCtx;

	JWName(pW, "id");
	JWStr(pW, pA->sId);
	if ( pA->sAnswer == NULL ) {
		JWName(pW, "question");
		JWStr(pW, pA->sQuestion);
		JWName(pW, "options");
		xrtJsonWriterArray(pW);
		if ( pA->pOptions != NULL && xrtValueType(pA->pOptions) == XVALUE_ARRAY ) {
			size_t i, n = xrtValueCount(pA->pOptions);
			for ( i = 0; i < n && i < MDO_ASK_OPTIONS_MAX; i++ ) {
				xvalue* pItem = xrtValueArrayGet(pA->pOptions, (int64)i);
				xstrview tV;
				if ( pItem != NULL && xrtValueGetString(pItem, &tV) )
					xrtJsonWriterString(pW, tV);
			}
		}
		xrtJsonWriterEnd(pW);
	} else {
		JWName(pW, "answer");
		JWStr(pW, pA->sAnswer);
	}
}

static xwork_result MdoAskToolExec(void* pUserData, const xwork_tool_context* pContext,
	const char* sArgumentsJson, xwork_tool_output* pOutput, xwork_error* pError)
{
	xvalue* tArgs = NULL;
	char* sQuestion = NULL;
	xvalue* pOptions = NULL;
	MdoRun* pRun = (MdoRun*)pUserData;
	MdoAskCtx tA;
	char aId[80];
	char aAnswer[MDO_ASK_ANSWER_MAX];
	uint64 uWaitStart;

	(void)pContext;
	if ( pRun == NULL ) return MdoWebFail(pOutput, "no run context");
	if ( !sArgumentsJson ) return MdoWebFail(pOutput, "invalid arguments");
	tArgs = xrtJsonParse(xrtStrView(sArgumentsJson));
	if ( tArgs == NULL || xrtValueType(tArgs) != XVALUE_OBJECT ) {
		if ( tArgs != NULL ) xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "invalid arguments: expected a JSON object");
	}
	sQuestion = MdoWebArgText(tArgs, "question");
	if ( sQuestion == NULL || sQuestion[0] == 0 ) {
		free(sQuestion); xrtValueRelease(tArgs);
		return MdoWebFail(pOutput, "question is required");
	}
	pOptions = xrtValueObjectGet(tArgs, xrtStrView("options"));
	if ( pOptions != NULL && xrtValueType(pOptions) != XVALUE_ARRAY ) pOptions = NULL;

	/* 生成事件 id 并广播提问（写穿 UI 日志 + 实时队列；emit 同步完成后才释放 args） */
	xrtMutexLock(pRun->pALock);
	snprintf(aId, sizeof(aId), "au%u", ++pRun->uAskSeq);
	snprintf(pRun->aAskId, sizeof(pRun->aAskId), "%s", aId);
	pRun->aAskAnswer[0] = 0;
	pRun->iAskState = -1;
	xrtMutexUnlock(pRun->pALock);

	{
		MdoAskCtx tReq = { aId, sQuestion, pOptions, NULL };
		MdoEmitFn(pRun, "ask_user/requested", MdoWriteAsk, &tReq);
	}

	/* 等用户作答（回合取消立即返回） */
	uWaitStart = MdoNowMs();
	xrtMutexLock(pRun->pALock);
	while ( pRun->iAskState < 0 ) {
		if ( xrtCancelRequested(pRun->pCancel) ) { pRun->iAskState = 0; break; }
		if ( MdoNowMs() - uWaitStart > MDO_ASK_TIMEOUT_MS ) { pRun->iAskState = 0; break; }
		xrtCondWaitFor(pRun->pACond, pRun->pALock, 500 * 1000);
	}
	fprintf(stderr, "[ask] tool woke, state=%d ans=%s\n", pRun->iAskState, pRun->aAskAnswer);
	snprintf(aAnswer, sizeof(aAnswer), "%s", pRun->aAskAnswer);
	xrtMutexUnlock(pRun->pALock);

	{
		MdoAskCtx tRes = { aId, NULL, NULL, aAnswer };
		MdoEmitFn(pRun, "ask_user/resolved", MdoWriteAsk, &tRes);
	}
	fprintf(stderr, "[ask] resolved emitted\n");

	free(sQuestion);
	xrtValueRelease(tArgs);

	if ( aAnswer[0] == 0 ) {
		xworkToolOutputSet(pOutput, true, "[user did not answer; continue without asking again]");
	} else {
		xworkToolOutputSet(pOutput, true, aAnswer);
	}
	return XWORK_RESULT_OK;
}

static bool MdoAskRegisterTool(xwork_agent* pAgent, MdoRun* pRun, xwork_error* pError)
{
	xwork_tool_definition tTool;

	memset(&tTool, 0, sizeof(tTool));
	tTool.sName = "ask_user";
	tTool.sDescription =
		"Ask the user a structured question and wait for their answer. "
		"Use when intent is ambiguous or a decision needs the user's confirmation; "
		"provide short options when possible, free text is always allowed.";
	tTool.sParametersJson =
		"{\"type\":\"object\",\"properties\":{"
		"\"question\":{\"type\":\"string\",\"minLength\":1},"
		"\"options\":{\"type\":\"array\",\"maxItems\":8,\"items\":{\"type\":\"string\",\"minLength\":1}},"
		"\"allow_free_text\":{\"type\":\"boolean\"}"
		"},\"required\":[\"question\"],\"additionalProperties\":false}";
	tTool.bStrict = true;
	tTool.eEffect = XWORK_TOOL_EFFECT_READ_ONLY;
	tTool.OnExecute = MdoAskToolExec;
	tTool.pUserData = pRun;
	tTool.sSource = "mdo-ask";
	return xworkAgentRegisterTool(pAgent, &tTool, pError);
}

#endif /* MDO_ASK_H */
