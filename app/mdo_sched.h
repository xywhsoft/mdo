/* mdo_sched.h — 计划任务（闹钟）：数据模型 / cron 解析 / 调度线程 / 触发链
 *
 * 依赖顺序：mdo_base.h → mdo_store.h → mdo_engine.h → 本文件（main.c 单 TU）。
 *
 * 语义要点（2026-09-20 方案定稿）：
 *   - fire = 在任务绑定的项目桶（未绑定→_tasks）新建 ⏰ 前缀会话跑一回合；
 *     产物是普通会话，事件溯源可回放；运行结束从内存表摘除（文件保留）。
 *   - 无人在场（headless）：只读工具面 + 审批自动放行（引擎侧实现）。
 *   - 错过策略任务级可配：skip（重算下次，不补跑）| catchup（启动即补跑一次）。
 *   - 重叠：上一运行未结束又到期 → 跳过（bRunning 守护，不排队）。
 *   - 计划只在 mdo 运行期间触发；进程重启后 bRunning 归零（内存态）。
 */

#include <time.h>

#define MDO_SCHED_MAX   64u
#define MDO_SCHED_RUNS  50u     /* 任务内保留的运行记录条数 */

typedef struct {
	char aSessionId[24];
	uint64 uTime;
	char aStatus[12];           /* running | done | failed */
} MdoSchedRun;

typedef struct {
	char aId[24];
	char aTitle[120];
	char aPrompt[4000];
	char aProject[80];          /* slug；空或 _tasks = 任务桶 */
	char aModel[64];            /* 空 = 解析链（项目默认 > 全局默认） */
	char aKind[12];             /* cron | interval | once */
	char aCron[64];
	uint32 uIntervalMin;
	uint32 uDelayMin;
	char aMiss[12];             /* skip | catchup */
	bool bEnabled;
	uint32 uMaxRuns;            /* 0 = 无限；跑满自动停 */
	uint64 uCreated;
	uint64 uLastRun;
	uint64 uNextDue;
	uint32 uRunCount;
	char aLastResult[240];
	MdoSchedRun aRuns[MDO_SCHED_RUNS];
	size_t nRuns;
	bool bRunning;              /* 运行态（内存，不落盘） */
} MdoSchedule;

static MdoSchedule* g_scheds[MDO_SCHED_MAX];
static size_t g_nScheds;
static char g_schedDir[340];
static xmutex* g_schedLock;
static xcond* g_schedCond;

/* ---------------- cron：5 字段（分 时 日 月 周），本地时区 ---------------- */
/* 字段语法：* | a | a-b | a-b/n | */n | 逗号列表；周 7 视同 0（周日） */

static bool MdoSchedCronField(const char* sField, int iMin, int iMax, int v)
{
	const char* p = sField;

	if ( sField == NULL || sField[0] == 0 ) return false;
	for ( ; ; ) {
		const char* pPart = p;
		const char* pComma = strchr(p, ',');
		char aPart[40];
		size_t n = pComma ? (size_t)(pComma - p) : strlen(p);
		int lo = iMin, hi = iMax, step = 1;
		const char* pSlash;
		char aBase[32];

		if ( n >= sizeof(aPart) ) goto next;
		memcpy(aPart, p, n);
		aPart[n] = 0;
		pSlash = strchr(aPart, '/');
		if ( pSlash != NULL ) {
			size_t nBase = (size_t)(pSlash - aPart);
			if ( nBase >= sizeof(aBase) ) goto next;
			memcpy(aBase, aPart, nBase);
			aBase[nBase] = 0;
			step = atoi(pSlash + 1);
			if ( step <= 0 ) goto next;
		} else {
			snprintf(aBase, sizeof(aBase), "%s", aPart);
		}
		if ( strcmp(aBase, "*") == 0 ) {
			lo = iMin; hi = iMax;
		} else {
			const char* pDash = strchr(aBase, '-');
			if ( pDash != NULL ) {
				lo = atoi(aBase);
				hi = atoi(pDash + 1);
			} else {
				lo = hi = atoi(aBase);
			}
			if ( iMax == 6 && hi == 7 ) hi = 0;   /* 周字段的 7=周日 */
			if ( lo < iMin || hi > iMax || lo > hi ) goto next;
		}
		{
			int x;
			for ( x = lo; ; x++ ) {
				if ( x > iMax ) break;
				if ( x == v && (x - lo) % step == 0 ) return true;
				if ( x == hi ) break;
			}
		}
next:
		if ( pComma == NULL ) break;
		p = pComma + 1;
	}
	return false;
}

static bool MdoSchedCronMatch(const char* sCron, const struct tm* lt)
{
	char aFields[5][64];
	int n = sscanf(sCron, "%63s %63s %63s %63s %63s",
		aFields[0], aFields[1], aFields[2], aFields[3], aFields[4]);
	if ( n != 5 ) return false;
	return MdoSchedCronField(aFields[0], 0, 59, lt->tm_min) &&
		MdoSchedCronField(aFields[1], 0, 23, lt->tm_hour) &&
		MdoSchedCronField(aFields[2], 1, 31, lt->tm_mday) &&
		MdoSchedCronField(aFields[3], 1, 12, lt->tm_mon + 1) &&
		MdoSchedCronField(aFields[4], 0, 6, lt->tm_wday);
}

/* 从 uAfterMs 之后找下一个 cron 命中（分钟粒度，上限一年防不可达模式死循环） */
static bool MdoSchedCronNext(const char* sCron, uint64 uAfterMs, uint64* pOutMs)
{
	uint64 t = (uAfterMs / 60000u + 1u) * 60000u;
	uint64 i;
	for ( i = 0u; i < 527040u; i++, t += 60000u ) {
		time_t tt = (time_t)(t / 1000u);
		struct tm* lt = localtime(&tt);
		if ( lt != NULL && MdoSchedCronMatch(sCron, lt) ) {
			*pOutMs = t;
			return true;
		}
	}
	return false;
}

/* ---------------- 存储 ---------------- */

/* 文本字段写 JSON 转义（用户可输入引号/反斜杠/换行） */
static void MdoSchedEsc(FILE* f, const char* s)
{
	for ( ; s != NULL && *s != 0; s++ ) {
		switch ( *s ) {
		case '"':  fputs("\\\"", f); break;
		case '\\': fputs("\\\\", f); break;
		case '\n': fputs("\\n", f); break;
		case '\r': break;
		case '\t': fputs("\\t", f); break;
		default:   fputc(*s, f); break;
		}
	}
}

static void MdoSchedPath(const MdoSchedule* pT, char* pOut, size_t iCap)
{
	snprintf(pOut, iCap, "%s%s%s.json", g_schedDir,
		(g_schedDir[0] && g_schedDir[strlen(g_schedDir) - 1] != '/') ? "/" : "", pT->aId);
}

static void MdoSchedSaveLocked(const MdoSchedule* pT)
{
	char aPath[420];
	FILE* f;
	size_t i;

	MdoSchedPath(pT, aPath, sizeof(aPath));
	f = fopen(aPath, "wb");
	if ( f == NULL ) return;
	fprintf(f, "{\"id\":\"%s\",\"title\":\"", pT->aId);
	MdoSchedEsc(f, pT->aTitle);
	fprintf(f, "\",\"prompt\":\"");
	MdoSchedEsc(f, pT->aPrompt);
	fprintf(f, "\",\"project\":\"");
	MdoSchedEsc(f, pT->aProject);
	fprintf(f, "\",\"model\":\"");
	MdoSchedEsc(f, pT->aModel);
	fprintf(f, "\",\"kind\":\"%s\",\"cron\":\"", pT->aKind);
	MdoSchedEsc(f, pT->aCron);
	fprintf(f, "\",\"intervalMin\":%u,\"delayMin\":%u,",
		(unsigned)pT->uIntervalMin, (unsigned)pT->uDelayMin);
	fprintf(f, "\"miss\":\"%s\",\"enabled\":%s,\"maxRuns\":%u,",
		pT->aMiss, pT->bEnabled ? "true" : "false", (unsigned)pT->uMaxRuns);
	fprintf(f, "\"created\":%llu,\"lastRun\":%llu,\"nextDue\":%llu,\"runCount\":%u,",
		(unsigned long long)pT->uCreated, (unsigned long long)pT->uLastRun,
		(unsigned long long)pT->uNextDue, (unsigned)pT->uRunCount);
	fprintf(f, "\"lastResult\":\"");
	MdoSchedEsc(f, pT->aLastResult);
	fprintf(f, "\",\"runs\":[");
	for ( i = 0u; i < pT->nRuns; i++ )
		fprintf(f, "%s{\"sessionId\":\"%s\",\"time\":%llu,\"status\":\"%s\"}",
			i ? "," : "", pT->aRuns[i].aSessionId,
			(unsigned long long)pT->aRuns[i].uTime, pT->aRuns[i].aStatus);
	fprintf(f, "]}\n");
	fclose(f);
}

static MdoSchedule* MdoSchedFindLocked(const char* sId)
{
	size_t i;
	for ( i = 0u; i < g_nScheds; i++ )
		if ( strcmp(g_scheds[i]->aId, sId) == 0 ) return g_scheds[i];
	return NULL;
}

static void MdoSchedScan(void)
{
	xdir hDir;
	xdirentry tEntry;

	xrtDirCreateAll(g_schedDir);
	hDir = xrtDirOpen(g_schedDir, 0);
	if ( hDir == NULL ) return;
	while ( xrtDirNext(hDir, &tEntry) == XDIR_NEXT_ITEM && g_nScheds < MDO_SCHED_MAX ) {
		size_t nName = tEntry.Name.Size;
		char aPath[420], aId[24];
		size_t iSize = 0;
		str sText;
		MdoSchedule* pT;

		if ( nName < 6 || nName > 28 || strcmp(tEntry.Name.Data + nName - 5, ".json") != 0 )
			continue;
		memcpy(aId, tEntry.Name.Data, nName - 5);
		aId[nName - 5] = 0;
		snprintf(aPath, sizeof(aPath), "%s/%s", g_schedDir, tEntry.Name.Data);
		sText = xrtFileReadText(aPath, XENCODING_UTF8, XUTF_REPLACE, &iSize);
		if ( sText == NULL ) continue;
		pT = (MdoSchedule*)xrtRealloc(NULL, sizeof(MdoSchedule));
		if ( pT == NULL ) { xrtFree(sText); continue; }
		memset(pT, 0, sizeof(*pT));
		{
			MdoJson tJ;
			if ( !MdoJsonParse(&tJ, sText) ) { xrtFree(pT); xrtFree(sText); continue; }
			JStr(&tJ, "id", pT->aId, sizeof(pT->aId));
			JStr(&tJ, "title", pT->aTitle, sizeof(pT->aTitle));
			JStr(&tJ, "prompt", pT->aPrompt, sizeof(pT->aPrompt));
			JStr(&tJ, "project", pT->aProject, sizeof(pT->aProject));
			JStr(&tJ, "model", pT->aModel, sizeof(pT->aModel));
			JStr(&tJ, "kind", pT->aKind, sizeof(pT->aKind));
			JStr(&tJ, "cron", pT->aCron, sizeof(pT->aCron));
			JStr(&tJ, "miss", pT->aMiss, sizeof(pT->aMiss));
			JStr(&tJ, "lastResult", pT->aLastResult, sizeof(pT->aLastResult));
			pT->uIntervalMin = (uint32)JInt(&tJ, "intervalMin", 0);
			pT->uDelayMin = (uint32)JInt(&tJ, "delayMin", 0);
			pT->uMaxRuns = (uint32)JInt(&tJ, "maxRuns", 0);
			pT->uCreated = (uint64)JInt(&tJ, "created", 0);
			pT->uLastRun = (uint64)JInt(&tJ, "lastRun", 0);
			pT->uNextDue = (uint64)JInt(&tJ, "nextDue", 0);
			pT->uRunCount = (uint32)JInt(&tJ, "runCount", 0);
			{
				xvalue* pV = xrtValueObjectGet(tJ.pRoot, xrtStrView("enabled"));
				bool b = false;
				pT->bEnabled = (pV != NULL && xrtValueGetBool(pV, &b)) ? b : true;
			}
			{
				xvalue* pRuns = xrtValueObjectGet(tJ.pRoot, xrtStrView("runs"));
				size_t n = (pRuns != NULL && xrtValueType(pRuns) == XVALUE_ARRAY)
					? xrtValueCount(pRuns) : 0u;
				size_t k;
				if ( n > MDO_SCHED_RUNS ) n = MDO_SCHED_RUNS;
				for ( k = 0u; k < n; k++ ) {
					xvalue* pR = xrtValueArrayGet(pRuns, k);
					xvalue* pV;
					int64 v64 = 0;
					if ( pR == NULL || xrtValueType(pR) != XVALUE_OBJECT ) break;
					JStrIn(pR, "sessionId", pT->aRuns[k].aSessionId,
						sizeof(pT->aRuns[k].aSessionId));
					JStrIn(pR, "status", pT->aRuns[k].aStatus,
						sizeof(pT->aRuns[k].aStatus));
					pV = xrtValueObjectGet(pR, xrtStrView("time"));
					if ( pV != NULL && xrtValueGetInt(pV, &v64) )
						pT->aRuns[k].uTime = (uint64)v64;
					pT->nRuns++;
				}
			}
			MdoJsonFree(&tJ);
		}
		xrtFree(sText);
		if ( pT->aId[0] == 0 ) { xrtFree(pT); continue; }
		if ( pT->aMiss[0] == 0 ) snprintf(pT->aMiss, sizeof(pT->aMiss), "skip");
		if ( pT->aKind[0] == 0 ) snprintf(pT->aKind, sizeof(pT->aKind), "once");
		g_scheds[g_nScheds++] = pT;
	}
	xrtDirClose(hDir);

	/* 错过策略（任务级）：skip=重算不补跑；catchup=启动即补一次 */
	{
		uint64 now = MdoNowMs();
		size_t i;
		for ( i = 0u; i < g_nScheds; i++ ) {
			MdoSchedule* pT = g_scheds[i];
			if ( !pT->bEnabled || pT->uNextDue == 0 || pT->uNextDue >= now ) continue;
			if ( strcmp(pT->aMiss, "catchup") == 0 ) {
				pT->uNextDue = now;   /* 调度线程启动后 1s 内触发 */
				MdoAuditLog("schedule-missed-catchup", pT->aId);
			} else {
				if ( strcmp(pT->aKind, "once") == 0 ) {
					pT->bEnabled = false;
					pT->uNextDue = 0;
				} else if ( strcmp(pT->aKind, "interval") == 0 ) {
					pT->uNextDue = now + (uint64)pT->uIntervalMin * 60000u;
				} else if ( !MdoSchedCronNext(pT->aCron, now, &pT->uNextDue) ) {
					pT->bEnabled = false;
					pT->uNextDue = 0;
				}
				MdoAuditLog("schedule-missed-skip", pT->aId);
			}
			MdoSchedSaveLocked(pT);
		}
	}
}

/* ---------------- nextDue 重算（触发后 / 新建时） ---------------- */

static void MdoSchedRearm(MdoSchedule* pT, uint64 uNowMs)
{
	if ( strcmp(pT->aKind, "once") == 0 ) {
		pT->bEnabled = false;
		pT->uNextDue = 0;
	} else if ( strcmp(pT->aKind, "interval") == 0 ) {
		pT->uNextDue = uNowMs + (uint64)pT->uIntervalMin * 60000u;
	} else {
		if ( !MdoSchedCronNext(pT->aCron, uNowMs, &pT->uNextDue) ) {
			pT->bEnabled = false;
			pT->uNextDue = 0;
		}
	}
	if ( pT->bEnabled && pT->uMaxRuns > 0u && pT->uRunCount >= pT->uMaxRuns ) {
		pT->bEnabled = false;
		pT->uNextDue = 0;
	}
}

/* ---------------- 触发链 ---------------- */

static void MdoSchedRecordRun(MdoSchedule* pT, const char* sSessionId, const char* sStatus)
{
	MdoSchedRun* pR;
	if ( pT->nRuns == MDO_SCHED_RUNS ) {
		memmove(&pT->aRuns[0], &pT->aRuns[1], (MDO_SCHED_RUNS - 1u) * sizeof(MdoSchedRun));
		pT->nRuns--;
	}
	pR = &pT->aRuns[pT->nRuns++];
	memset(pR, 0, sizeof(*pR));
	snprintf(pR->aSessionId, sizeof(pR->aSessionId), "%s", sSessionId);
	pR->uTime = MdoNowMs();
	snprintf(pR->aStatus, sizeof(pR->aStatus), "%s", sStatus);
}

static void MdoSchedFire(MdoSchedule* pT)
{
	MdoProject* pProj = NULL;
	MdoSession* pS = NULL;
	MdoModel* pModel = NULL;
	xllm_error tErr;
	char aTitle[220], aErr[512], aDetail[300];
	uint64 now = MdoNowMs();
	time_t tt = (time_t)(now / 1000u);
	struct tm* lt = localtime(&tt);

	snprintf(aTitle, sizeof(aTitle), "⏰ %s · %02d-%02d %02d:%02d",
		pT->aTitle,
		lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
		lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);

	/* 建会话（绑定桶）+ 模型解析——g_lock 内 */
	xrtMutexLock(g_lock);
	if ( pT->aProject[0] != 0 && strcmp(pT->aProject, "_tasks") != 0 ) {
		size_t i;
		for ( i = 0u; i < g_nProjects; i++ )
			if ( strcmp(g_projects[i].aSlug, pT->aProject) == 0 ) { pProj = &g_projects[i]; break; }
	}
	pS = MdoSessionCreateLocked(pProj, aTitle,
		pT->aModel[0] != 0 ? pT->aModel : NULL, 0, NULL);
	if ( pS != NULL ) {
		pModel = MdoModelFind(pS->aModelId);
		if ( pModel != NULL ) pS->pRun = (MdoRun*)1;   /* 预占 */
	}
	xrtMutexUnlock(g_lock);

	if ( pS == NULL || pModel == NULL ) {
		snprintf(aDetail, sizeof(aDetail), "%s model-or-session", pT->aId);
		MdoAuditLog("schedule-run-failed", aDetail);
		xrtMutexLock(g_schedLock);
		pT->bRunning = false;
		pT->uLastRun = now;
		MdoSchedRecordRun(pT, pS ? pS->aId : "", "failed");
		MdoSchedRearm(pT, now);
		MdoSchedSaveLocked(pT);
		xrtMutexUnlock(g_schedLock);
		if ( pS != NULL && pModel == NULL ) {
			/* 会话已建但模型不可用：留在桶内（空会话），不阻塞任务 */
			xrtMutexLock(g_lock);
			if ( pS->pRun == (MdoRun*)1 ) pS->pRun = NULL;
			xrtMutexUnlock(g_lock);
		}
		return;
	}

	/* 客户端 + 会话恢复（锁外：TLS/磁盘） */
	xllmErrorInit(&tErr);
	if ( MdoClientFor(pModel, &tErr) == NULL ) {
		snprintf(aDetail, sizeof(aDetail), "%s client", pT->aId);
		MdoAuditLog("schedule-run-failed", aDetail);
		xrtMutexLock(g_lock);
		if ( pS->pRun == (MdoRun*)1 ) pS->pRun = NULL;
		xrtMutexUnlock(g_lock);
		xrtMutexLock(g_schedLock);
		pT->bRunning = false;
		pT->uLastRun = now;
		MdoSchedRecordRun(pT, pS->aId, "failed");
		MdoSchedRearm(pT, now);
		MdoSchedSaveLocked(pT);
		xrtMutexUnlock(g_schedLock);
		return;
	}
	if ( !MdoSessionEnsure(pS, pProj, pModel, aErr, sizeof(aErr)) ) {
		snprintf(aDetail, sizeof(aDetail), "%s ensure", pT->aId);
		MdoAuditLog("schedule-run-failed", aDetail);
		xrtMutexLock(g_lock);
		if ( pS->pRun == (MdoRun*)1 ) pS->pRun = NULL;
		xrtMutexUnlock(g_lock);
		xrtMutexLock(g_schedLock);
		pT->bRunning = false;
		pT->uLastRun = now;
		MdoSchedRecordRun(pT, pS->aId, "failed");
		MdoSchedRearm(pT, now);
		MdoSchedSaveLocked(pT);
		xrtMutexUnlock(g_schedLock);
		return;
	}

	/* 起回合（headless） */
	{
		uint64 uTurnId = MdoRunStart(pS, pT->aPrompt, pModel, pProj, true,
			NULL, 0, aErr, sizeof(aErr));
		xrtMutexLock(g_schedLock);
		pT->uLastRun = now;
		pT->uRunCount++;
		if ( uTurnId == 0 ) {
			pT->bRunning = false;
			MdoSchedRecordRun(pT, pS->aId, "failed");
			snprintf(aDetail, sizeof(aDetail), "%s start", pT->aId);
			MdoAuditLog("schedule-run-failed", aDetail);
		} else {
			MdoSchedRecordRun(pT, pS->aId, "running");
			snprintf(aDetail, sizeof(aDetail), "%s %s turn=%llu", pT->aId, pS->aId,
				(unsigned long long)uTurnId);
			MdoAuditLog("schedule-fire", aDetail);
		}
		MdoSchedRearm(pT, now);
		MdoSchedSaveLocked(pT);
		xrtMutexUnlock(g_schedLock);
	}
}

/* 引擎回调：headless 回合终态回写（MdoRunFinish 调用，运行线程上下文） */
static void MdoSchedNotifyRunEnd(const char* sSessionId, bool bOk, const char* sResult)
{
	MdoSchedule* pT = NULL;
	size_t i;

	xrtMutexLock(g_schedLock);
	for ( i = 0u; i < g_nScheds; i++ ) {
		MdoSchedule* pC = g_scheds[i];
		if ( pC->nRuns > 0u && strcmp(pC->aRuns[pC->nRuns - 1u].aSessionId, sSessionId) == 0 &&
		     strcmp(pC->aRuns[pC->nRuns - 1u].aStatus, "running") == 0 ) { pT = pC; break; }
	}
	if ( pT != NULL ) {
		snprintf(pT->aRuns[pT->nRuns - 1u].aStatus,
			sizeof(pT->aRuns[0].aStatus), "%s", bOk ? "done" : "failed");
		pT->bRunning = false;
		if ( bOk && sResult != NULL && sResult[0] != 0 ) {
			size_t nClip = Utf8Clip(sResult, strlen(sResult), sizeof(pT->aLastResult) - 1);
			memcpy(pT->aLastResult, sResult, nClip);
			pT->aLastResult[nClip] = 0;
		}
		MdoSchedSaveLocked(pT);
	}
	xrtMutexUnlock(g_schedLock);

	/* 会话从内存表摘除：文件留在绑定桶，全库扫描（?all=1/项目切换）自然可见 */
	xrtMutexLock(g_lock);
	{
		MdoSession* pS = MdoSessionFindLocked(sSessionId);
		if ( pS != NULL && pS->pRun == NULL ) {
			size_t k;
			for ( k = 0u; k < g_nSessions; k++ ) {
				if ( g_sessions[k] == pS ) {
					memmove(&g_sessions[k], &g_sessions[k + 1],
						(g_nSessions - k - 1u) * sizeof(MdoSession*));
					g_nSessions--;
					break;
				}
			}
			MdoSessionCloseHandles(pS);
			xrtFree(pS);
		}
	}
	xrtMutexUnlock(g_lock);
}

/* ---------------- 调度线程 ---------------- */

static int32 MdoSchedThread(void* pArg)
{
	(void)pArg;
	for ( ; ; ) {
		uint64 now = MdoNowMs();
		MdoSchedule* pDue = NULL;
		uint64 uBest = 0;
		bool bClaim = false;
		size_t i;

		xrtMutexLock(g_schedLock);
		for ( i = 0u; i < g_nScheds; i++ ) {
			MdoSchedule* pT = g_scheds[i];
			if ( !pT->bEnabled || pT->bRunning || pT->uNextDue == 0 ) continue;
			if ( pDue == NULL || pT->uNextDue < uBest ) { pDue = pT; uBest = pT->uNextDue; }
		}
		if ( pDue == NULL )
			xrtCondWaitFor(g_schedCond, g_schedLock, 600u * 1000u * 1000u);
		else if ( uBest > now + 50u )
			xrtCondWaitFor(g_schedCond, g_schedLock, (uint32)(uBest - now) * 1000u);
		else {
			pDue->bRunning = true;   /* 认领（重叠守护） */
			bClaim = true;
		}
		xrtMutexUnlock(g_schedLock);
		if ( bClaim && pDue != NULL ) MdoSchedFire(pDue);
	}
	return 0;
}

static void MdoSchedWake(void)
{
	if ( g_schedCond != NULL ) xrtCondSignal(g_schedCond);
}

static void MdoSchedInit(void)
{
	char* pDir = MdoPathJoin(g_mdoHome, "schedules");
	snprintf(g_schedDir, sizeof(g_schedDir), "%s", pDir ? pDir : "schedules");
	xrtFree(pDir);

	g_schedLock = xrtMutexCreate();
	g_schedCond = xrtCondCreate();
	if ( g_schedLock == NULL || g_schedCond == NULL ) return;

	xrtMutexLock(g_schedLock);
	MdoSchedScan();
	xrtMutexUnlock(g_schedLock);

	xrtThreadCreate(MdoSchedThread, NULL, 0);
}
