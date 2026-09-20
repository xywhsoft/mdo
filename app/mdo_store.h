/*
 * mdo_store.h — 数据根、模型表、项目分桶、会话元数据与 UI 事件日志
 *
 * 数据根（便携优先，用户需求：便携/易备份/重装系统不受影响）：
 *   1. 环境变量 MDO_HOME 覆盖
 *   2. 默认：程序自身所在目录下的 data/（xrtPathExecutable）
 *   3. 首启迁移：旧 ~/.mdo 存在且新根为空 → 整树复制过来（旧目录保留作备份）
 *
 * 磁盘布局（会话按工作目录分桶，mdo 设计文档 §五.1）：
 *   <data>/config.json                      模型表 + 默认模型 + 活动项目
 *   <data>/memory/MEMORY.md                 全局记忆索引（每次会话注入提示词）
 *   <data>/memory/<slug>.md                 全局记忆事实文件（一条一事+frontmatter）
 *   <data>/projects/<slug>/project.json     { name, path }
 *   <data>/projects/<slug>/memory/…         项目记忆（同构，仅本项目会话注入）
 *   <data>/projects/<slug>/sessions/<id>.meta.json   会话元数据
 *   <data>/projects/<slug>/sessions/<id>.jsonl       xllm-session 日志（模型权威）
 *   <data>/projects/<slug>/sessions/<id>.snap        回合结束原子快照
 *   <data>/projects/<slug>/sessions/<id>.ui.jsonl    UI 事件日志（前端重放）
 *
 * 记忆（mdo §12.3 铁律 + 工具集终裁④）：零专用工具、系统提示词约定、明文可审计。
 * 模型用 read/write/edit 直接维护；写数据桶路径走既有审批门。
 */

#ifndef MDO_STORE_H
#define MDO_STORE_H

/* ------------------------------------------------------------------ */
/* 数据模型                                                            */
/* ------------------------------------------------------------------ */

#define MDO_MAX_MODELS   24
#define MDO_MAX_PROJECTS 32
#define MDO_MAX_SESSIONS 96
#define MDO_MAX_EVENTS   4096

/* 内置模型（zcode GLM / codex GPT 同类形态）：免配置、随程序分发、不可删除。
 * 端点为自建 GPU 服务（Let's Encrypt 证书）；用户可在设置里改字段，但删不掉。 */
#define MDO_BUILTIN_MODEL_ID "ling-gpu"
static const char* const c_sBuiltinModelKey =
	"a59048aa00184acc7a7d9540c95c84f8259ff21fc35110a7";

/* 默认分类「任务」：不属于任何项目的会话都保存在此（伪项目，永在列表末尾，不可删除） */
#define MDO_TASKS_SLUG "_tasks"
#define MDO_TASKS_NAME "任务"

typedef struct {
	char aId[64];
	char aName[128];
	char aBaseUrl[280];
	char aApiKey[200];
	char aModel[96];
	char aDialect[16];     /* openai | glm | anthropic */
	char aReasoning[12];   /* "" | low | medium | high */
	char aCaPem[280];      /* 可选：自建 CA PEM 文件路径 */
	uint64 uContextWindow;
	uint32 uMaxOutput;     /* 0 = 库默认 */
	/* 运行态（不落盘） */
	xllm_client* pClient;  /* 惰性创建；销毁于 ServiceUnit */
	char* sCaPemText;      /* 已加载 PEM 文本（客户端借用） */
} MdoModel;

typedef struct {
	char aSlug[80];
	char aName[128];
	char aPath[300];
	char aDefaultModel[64];  /* 项目级默认模型（空 = 跟随全局） */
	bool bValidPath;         /* 工作目录仍存在（失效时 UI 标记） */
} MdoProject;

typedef struct MdoSession {
	char aId[40];
	char aTitle[128];
	char aModelId[64];
	uint64 uContextWindow; /* 建会话时的模型窗口（会话期固定） */
	uint64 uCreatedAt;
	uint64 uUpdatedAt;
	uint32 uTurns;
	bool bPinned;           /* 置顶（服务端持久，重载不丢） */
	char aUserPrompt[2048]; /* 建会话时快照的用户全局指令（追加进系统提示词） */
	char aEffort[12];       /* 运行态：本回合思考力度（prompt 参数，随写随用） */
	/* 运行态 */
	xllm_session_hooks tHooks;  /* effort 渲染钩子（挂在本对象上，指针稳定） */
	bool bLoaded;          /* xllm_session 已 Recover */
	xllm_session* pSession;
	FILE* fUi;             /* UI 事件日志追加句柄 */
	struct MdoRun* pRun;   /* 活动回合；NULL = 空闲 */
} MdoSession;

/* ------------------------------------------------------------------ */
/* 全局状态（g_lock 保护）                                              */
/* ------------------------------------------------------------------ */

static xmutex* g_lock = NULL;
static char g_mdoHome[300] = "";          /* ~/.mdo */
static char g_toolsDir[300] = "";         /* <exe>/tools：随程序分发的工具目录（注入 PATH） */
static bool g_bPythonBundled = false;     /* tools/python313 是否存在 */
static char g_cfgPath[320] = "";
static char g_projectsDir[340] = "";

static MdoModel g_models[MDO_MAX_MODELS];
static size_t g_nModels = 0;
static char g_defaultModel[64] = "";

static MdoProject g_projects[MDO_MAX_PROJECTS];
static size_t g_nProjects = 0;
static char g_activeProject[80] = "";

static MdoSession* g_sessions[MDO_MAX_SESSIONS];   /* 指针稳定：pRun->pSess 不因重排悬垂 */

/* 前置声明（定义在记忆节，注册项目/启动时即需调用） */
static void MdoMemoryDir(const MdoProject* pProj, bool bProject, char* pOut, size_t iCap);
static MdoModel* MdoModelFind(const char* sId);
static void MdoMemoryEnsure(const char* sDir);
static bool MdoMemoryReadIndex(const char* sDir, char* pOut, size_t iCap);

/* 前置声明（定义在数据安全节，删除路径即需调用） */
static void MdoAuditLog(const char* sAction, const char* sDetail);
static bool MdoTrashFile(const char* sPath, const char* sAction);
static bool MdoTrashDir(const char* sPath, const char* sAction);
static size_t g_nSessions = 0;

/* ---- 全局设置（服务端权威，<data>/config.json 的 settings 节） ---- */
typedef struct {
	char aTheme[12];        /* auto | light | dark */
	char aFontSize[8];      /* sm | md | lg */
	char aLang[8];          /* zh | en | ru */
	char aInteractMode[8];  /* queue | guide */
	bool bSound;            /* 完成提示音 */
	bool bAutoApprove;      /* 审批自动放行 */
	char aSystemPrompt[2048];
	/* 网络 */
	bool bProxyEnabled;
	char aProxyHost[200];
	uint32 uProxyPort;
	char aProxyUser[120];
	char aProxyPass[120];
	char aProxyBypass[600]; /* 不走代理的主机/域，逗号或分号分隔；* 通配 */
	/* 证书 */
	char aCaCertPath[280];  /* 全局自定义 CA：PEM 文件或目录（目录=拼全部 .pem/.crt） */
	/* 系统 */
	bool bPreventSleep;     /* 阻止系统休眠（SetThreadExecutionState） */
	/* 窗口（前端上报，xs 侧恢复用） */
	int iWinX, iWinY, iWinW, iWinH;
	bool bWinMax;
} MdoSettings;

static MdoSettings g_settings;
static char* g_sCaBundle = NULL;   /* 全局 CA 合成 PEM（惰性加载，客户端借用） */

/* ------------------------------------------------------------------ */
/* 路径工具                                                            */
/* ------------------------------------------------------------------ */

static char* MdoStrDupN(const char* s, size_t n)
{
	char* p = (char*)malloc(n + 1u);
	if ( p != NULL ) { memcpy(p, s, n); p[n] = 0; }
	return p;
}

static char* MdoPathJoin(const char* a, const char* b) /* xrtFree 释放 */
{
	return xrtPathJoin(a, b);
}

/* 返回活动项目；无则 NULL */
static MdoProject* MdoActiveProject(void)
{
	size_t i;
	for ( i = 0; i < g_nProjects; i++ )
		if ( strcmp(g_projects[i].aSlug, g_activeProject) == 0 )
			return &g_projects[i];
	return NULL;
}

/* 会话文件都住在 ~/.mdo/projects/<slug>/sessions/ 下（与工作区分离） */
static void MdoSessionPaths(const MdoProject* pProj, const char* sId,
	char* pMeta, size_t iMetaCap, char* pJournal, size_t iJournalCap,
	char* pSnap, size_t iSnapCap, char* pUi, size_t iUiCap)
{
	char* pSlugDir = MdoPathJoin(g_projectsDir, pProj ? pProj->aSlug : MDO_TASKS_SLUG);
	char* pSess = pSlugDir ? MdoPathJoin(pSlugDir, "sessions") : NULL;
	char* pBase = pSess ? MdoPathJoin(pSess, sId) : NULL;

	if ( pMeta )   snprintf(pMeta, iMetaCap, "%s.meta.json", pBase ? pBase : "");
	if ( pJournal )snprintf(pJournal, iJournalCap, "%s.jsonl", pBase ? pBase : "");
	if ( pSnap )   snprintf(pSnap, iSnapCap, "%s.snap", pBase ? pBase : "");
	if ( pUi )     snprintf(pUi, iUiCap, "%s.ui.jsonl", pBase ? pBase : "");
	xrtFree(pBase);
	xrtFree(pSess);
	xrtFree(pSlugDir);
}

/* ------------------------------------------------------------------ */
/* config.json 读写                                                     */
/* ------------------------------------------------------------------ */

static void MdoConfigSaveLocked(void)
{
	xjsonwriter* pW = JWOpen();
	size_t i;
	char* sText;

	xrtJsonWriterObject(pW);
	JWName(pW, "models");
	xrtJsonWriterArray(pW);
	for ( i = 0; i < g_nModels; i++ ) {
		const MdoModel* pM = &g_models[i];
		if ( strcmp(pM->aId, MDO_BUILTIN_MODEL_ID) == 0 ) continue;   /* 内置模型不落盘（key 在程序内） */
		xrtJsonWriterObject(pW);
		JWName(pW, "id");        JWStr(pW, pM->aId);
		JWName(pW, "name");      JWStr(pW, pM->aName);
		JWName(pW, "baseUrl");   JWStr(pW, pM->aBaseUrl);
		JWName(pW, "apiKey");    JWStr(pW, pM->aApiKey);
		JWName(pW, "model");     JWStr(pW, pM->aModel);
		JWName(pW, "dialect");   JWStr(pW, pM->aDialect);
		JWName(pW, "reasoning"); JWStr(pW, pM->aReasoning);
		JWName(pW, "caPem");     JWStr(pW, pM->aCaPem);
		JWName(pW, "contextWindow"); JWUInt(pW, pM->uContextWindow);
		JWName(pW, "maxOutput"); JWUInt(pW, pM->uMaxOutput);
		xrtJsonWriterEnd(pW);
	}
	xrtJsonWriterEnd(pW);
	JWName(pW, "defaultModel");  JWStr(pW, g_defaultModel);
	JWName(pW, "activeProject"); JWStr(pW, g_activeProject);
	JWName(pW, "settings");
	xrtJsonWriterObject(pW);
	JWName(pW, "theme");        JWStr(pW, g_settings.aTheme);
	JWName(pW, "lang");         JWStr(pW, g_settings.aLang);
	JWName(pW, "interactMode"); JWStr(pW, g_settings.aInteractMode);
	JWName(pW, "fontSize");     JWStr(pW, g_settings.aFontSize);
	JWName(pW, "sound");        JWBool(pW, g_settings.bSound);
	JWName(pW, "autoApprove");  JWBool(pW, g_settings.bAutoApprove);
	JWName(pW, "systemPrompt"); JWStr(pW, g_settings.aSystemPrompt);
	JWName(pW, "proxyEnabled"); JWBool(pW, g_settings.bProxyEnabled);
	JWName(pW, "proxyHost");    JWStr(pW, g_settings.aProxyHost);
	JWName(pW, "proxyPort");    JWUInt(pW, g_settings.uProxyPort);
	JWName(pW, "proxyUser");    JWStr(pW, g_settings.aProxyUser);
	JWName(pW, "proxyPass");    JWStr(pW, g_settings.aProxyPass);
	JWName(pW, "proxyBypass");  JWStr(pW, g_settings.aProxyBypass);
	JWName(pW, "caCertPath");   JWStr(pW, g_settings.aCaCertPath);
	JWName(pW, "preventSleep"); JWBool(pW, g_settings.bPreventSleep);
	JWName(pW, "winX"); JWInt(pW, g_settings.iWinX);
	JWName(pW, "winY"); JWInt(pW, g_settings.iWinY);
	JWName(pW, "winW"); JWInt(pW, g_settings.iWinW);
	JWName(pW, "winH"); JWInt(pW, g_settings.iWinH);
	JWName(pW, "winMax"); JWBool(pW, g_settings.bWinMax);
	xrtJsonWriterEnd(pW);
	xrtJsonWriterEnd(pW);

	sText = JWTake(pW);
	if ( sText != NULL ) {
		xrtFileWriteTextAtomic(g_cfgPath, xrtStrView(sText),
			XENCODING_UTF8, XUTF_REPLACE, false);
		xrtFree(sText);
	}
}

static void MdoModelFromJson(MdoModel* pM, xvalue* pObj)
{
	JStrIn(pObj, "id", pM->aId, sizeof(pM->aId));
	JStrIn(pObj, "name", pM->aName, sizeof(pM->aName));
	JStrIn(pObj, "baseUrl", pM->aBaseUrl, sizeof(pM->aBaseUrl));
	JStrIn(pObj, "apiKey", pM->aApiKey, sizeof(pM->aApiKey));
	JStrIn(pObj, "model", pM->aModel, sizeof(pM->aModel));
	JStrIn(pObj, "dialect", pM->aDialect, sizeof(pM->aDialect));
	JStrIn(pObj, "reasoning", pM->aReasoning, sizeof(pM->aReasoning));
	JStrIn(pObj, "caPem", pM->aCaPem, sizeof(pM->aCaPem));
	{
		xvalue* pV = xrtValueObjectGet(pObj, xrtStrView("contextWindow"));
		int64 iV = 0;
		if ( pV && xrtValueGetInt(pV, &iV) && iV > 0 )
			pM->uContextWindow = (uint64)iV;
		pV = xrtValueObjectGet(pObj, xrtStrView("maxOutput"));
		if ( pV && xrtValueGetInt(pV, &iV) && iV > 0 )
			pM->uMaxOutput = (uint32)iV;
	}
	if ( pM->aDialect[0] == 0 )
		snprintf(pM->aDialect, sizeof(pM->aDialect), "openai");
}

/* 确保内置模型在表内（置于首位）且默认模型可用；g_lock 内调用。
 * 已有同 id 条目时尊重用户改过的字段（仅补位，不覆盖）。 */
static void MdoBuiltinEnsureLocked(void)
{
	size_t i;
	bool bHas = false;

	for ( i = 0; i < g_nModels; i++ )
		if ( strcmp(g_models[i].aId, MDO_BUILTIN_MODEL_ID) == 0 ) { bHas = true; break; }
	if ( !bHas ) {
		MdoModel* pM;
		if ( g_nModels >= MDO_MAX_MODELS ) g_nModels = MDO_MAX_MODELS - 1;
		memmove(&g_models[1], &g_models[0], g_nModels * sizeof(MdoModel));
		g_nModels++;
		pM = &g_models[0];
		memset(pM, 0, sizeof(*pM));
		snprintf(pM->aId, sizeof(pM->aId), "%s", MDO_BUILTIN_MODEL_ID);
		snprintf(pM->aName, sizeof(pM->aName), "Ling-3.0-tiny · 内置");
		snprintf(pM->aBaseUrl, sizeof(pM->aBaseUrl), "https://ai.xywhsoft.com:8444/v1");
		snprintf(pM->aApiKey, sizeof(pM->aApiKey), "%s", c_sBuiltinModelKey);
		snprintf(pM->aModel, sizeof(pM->aModel), "ling-3.0-tiny");
		snprintf(pM->aDialect, sizeof(pM->aDialect), "openai");
		pM->uContextWindow = 131072;
	} else if ( i > 0 ) {
		/* 已存在但不在首位：移到首位（UI 分组首见序 = 内置组优先展示） */
		MdoModel tTmp = g_models[i];
		memmove(&g_models[1], &g_models[0], i * sizeof(MdoModel));
		g_models[0] = tTmp;
	}
	/* 默认模型为空或指向不存在的条目 → 回退内置（免配置即用） */
	if ( MdoModelFind(g_defaultModel) == NULL )
		snprintf(g_defaultModel, sizeof(g_defaultModel), "%s", MDO_BUILTIN_MODEL_ID);
}

static void MdoConfigLoadLocked(void)
{
	size_t iSize = 0;
	str sText = xrtFileReadText(g_cfgPath, XENCODING_UTF8, XUTF_REPLACE, &iSize);
	MdoJson tJ;

	if ( sText == NULL ) {
		/* 首启：种子一个 GLM 空钥条目（zcode 式：用户在设置里补 key） */
		MdoModel* pM = &g_models[g_nModels++];
		memset(pM, 0, sizeof(*pM));
		snprintf(pM->aId, sizeof(pM->aId), "glm-coding");
		snprintf(pM->aName, sizeof(pM->aName), "GLM Coding Plan (glm-5.2)");
		snprintf(pM->aBaseUrl, sizeof(pM->aBaseUrl), "https://open.bigmodel.cn/api/paas/v4");
		snprintf(pM->aModel, sizeof(pM->aModel), "glm-5.2");
		snprintf(pM->aDialect, sizeof(pM->aDialect), "openai");
		pM->uContextWindow = 1000000;
		snprintf(g_settings.aTheme, sizeof(g_settings.aTheme), "auto");
		snprintf(g_settings.aFontSize, sizeof(g_settings.aFontSize), "md");
		MdoBuiltinEnsureLocked();   /* 内置模型入表 + 默认模型 = 内置 */
		MdoConfigSaveLocked();
		return;
	}
	if ( MdoJsonParse(&tJ, sText) ) {
		xvalue* pArr = JArr(&tJ, "models");
		size_t i, n = pArr ? xrtValueCount(pArr) : 0;
		if ( n > MDO_MAX_MODELS ) n = MDO_MAX_MODELS;
		for ( i = 0; i < n; i++ ) {
			xvalue* pItem = xrtValueArrayAt(pArr, (int64)i);
			if ( pItem == NULL ||
			     xrtValueType(pItem) != XVALUE_OBJECT ) continue;
			{
				char aIdChk[64] = "";
				JStrIn(pItem, "id", aIdChk, sizeof(aIdChk));
				if ( strcmp(aIdChk, MDO_BUILTIN_MODEL_ID) == 0 ) continue;   /* 内置条目不读盘，恒用程序内常量 */
			}
			MdoModelFromJson(&g_models[g_nModels], pItem);
			if ( g_models[g_nModels].aId[0] ) g_nModels++;
		}
		JStr(&tJ, "defaultModel", g_defaultModel, sizeof(g_defaultModel));
		JStr(&tJ, "activeProject", g_activeProject, sizeof(g_activeProject));
		{
			xvalue* pS = JObj(&tJ, "settings");
			if ( pS != NULL ) {
				bool b = false;
				int64 iV = 0;
				JStrIn(pS, "theme", g_settings.aTheme, sizeof(g_settings.aTheme));
			JStrIn(pS, "lang", g_settings.aLang, sizeof(g_settings.aLang));
			JStrIn(pS, "interactMode", g_settings.aInteractMode, sizeof(g_settings.aInteractMode));
				JStrIn(pS, "fontSize", g_settings.aFontSize, sizeof(g_settings.aFontSize));
				JStrIn(pS, "systemPrompt", g_settings.aSystemPrompt, sizeof(g_settings.aSystemPrompt));
				JStrIn(pS, "proxyHost", g_settings.aProxyHost, sizeof(g_settings.aProxyHost));
				JStrIn(pS, "proxyUser", g_settings.aProxyUser, sizeof(g_settings.aProxyUser));
				JStrIn(pS, "proxyPass", g_settings.aProxyPass, sizeof(g_settings.aProxyPass));
				JStrIn(pS, "proxyBypass", g_settings.aProxyBypass, sizeof(g_settings.aProxyBypass));
				JStrIn(pS, "caCertPath", g_settings.aCaCertPath, sizeof(g_settings.aCaCertPath));
				b = false;
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("sound")), &b) ) g_settings.bSound = b;
				b = false;
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("autoApprove")), &b) ) g_settings.bAutoApprove = b;
				b = false;
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("proxyEnabled")), &b) ) g_settings.bProxyEnabled = b;
				b = false;
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("preventSleep")), &b) ) g_settings.bPreventSleep = b;
				iV = 0;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("proxyPort")), &iV) ) g_settings.uProxyPort = (uint32)iV;
				iV = 0;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("winX")), &iV) ) g_settings.iWinX = (int)iV;
				iV = 0;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("winY")), &iV) ) g_settings.iWinY = (int)iV;
				iV = 0;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("winW")), &iV) ) g_settings.iWinW = (int)iV;
				iV = 0;
				if ( xrtValueGetInt(xrtValueObjectGet(pS, xrtStrView("winH")), &iV) ) g_settings.iWinH = (int)iV;
				b = false;
				if ( xrtValueGetBool(xrtValueObjectGet(pS, xrtStrView("winMax")), &b) ) g_settings.bWinMax = b;
			}
		}
		MdoJsonFree(&tJ);
	}
	xrtFree(sText);
	MdoBuiltinEnsureLocked();   /* 存量配置也补内置 + 默认模型兜底 */
}

/* ------------------------------------------------------------------ */
/* 设置应用：全局 CA bundle / 阻止休眠                                   */
/* ------------------------------------------------------------------ */

/* 归一默认（老配置无 settings 节时补默认值） */
static void MdoSettingsNormalize(void)
{
	if ( g_settings.aLang[0] == 0 )
		snprintf(g_settings.aLang, sizeof(g_settings.aLang), "zh");
	if ( g_settings.aInteractMode[0] == 0 )
		snprintf(g_settings.aInteractMode, sizeof(g_settings.aInteractMode), "queue");
	if ( g_settings.aTheme[0] == 0 )
		snprintf(g_settings.aTheme, sizeof(g_settings.aTheme), "auto");
	if ( g_settings.aFontSize[0] == 0 )
		snprintf(g_settings.aFontSize, sizeof(g_settings.aFontSize), "md");
}

/* 读取一个 PEM 文件文本追加进 bundle */
static void MdoCaAppendFile(char** psBundle, const char* sPath)
{
	size_t iSize = 0;
	str sText = xrtFileReadText(sPath, XENCODING_UTF8, XUTF_REPLACE, &iSize);
	size_t iOld, iAdd;
	char* pNew;

	if ( sText == NULL || iSize == 0 ) { xrtFree(sText); return; }
	iOld = *psBundle ? strlen(*psBundle) : 0;
	iAdd = strlen(sText);
	pNew = (char*)xrtRealloc(*psBundle, iOld + iAdd + 2);
	if ( pNew == NULL ) { xrtFree(sText); return; }
	memcpy(pNew + iOld, sText, iAdd);
	pNew[iOld + iAdd] = '\n';
	pNew[iOld + iAdd + 1] = 0;
	*psBundle = pNew;
	xrtFree(sText);
}

/* 全局自定义 CA：路径为文件→直接读；目录→拼全部 .pem/.crt/.cer */
static const char* MdoCaBundle(void)
{
	xdir hDir;
	xdirentry tEntry;

	if ( g_sCaBundle != NULL ) return g_sCaBundle[0] ? g_sCaBundle : NULL;
	if ( g_settings.aCaCertPath[0] == 0 ) return NULL;
	if ( xrtFileExists(g_settings.aCaCertPath) ) {
		MdoCaAppendFile(&g_sCaBundle, g_settings.aCaCertPath);
		return g_sCaBundle ? g_sCaBundle : NULL;
	}
	if ( !xrtDirExists(g_settings.aCaCertPath) ) return NULL;
	hDir = xrtDirOpen(g_settings.aCaCertPath, 0);
	if ( hDir == NULL ) return NULL;
	while ( xrtDirNext(hDir, &tEntry) == XDIR_NEXT_ITEM ) {
		size_t n = tEntry.Name.Size;
		const char* sName = tEntry.Name.Data;
		if ( n > 4 && (strcmp(sName + n - 4, ".pem") == 0 ||
		     strcmp(sName + n - 4, ".crt") == 0 ||
		     strcmp(sName + n - 4, ".cer") == 0) ) {
			char* pFile = MdoPathJoin(g_settings.aCaCertPath, sName);
			if ( pFile != NULL ) {
				MdoCaAppendFile(&g_sCaBundle, pFile);
				xrtFree(pFile);
			}
		}
	}
	xrtDirClose(hDir);
	return g_sCaBundle && g_sCaBundle[0] ? g_sCaBundle : NULL;
}

/* 修改 CA 路径后调用（作废缓存） */
static void MdoCaInvalidate(void)
{
	xrtFree(g_sCaBundle);
	g_sCaBundle = NULL;
}

/* 阻止系统休眠（Windows：SetThreadExecutionState；kernel32 经标准导入） */
#if defined(_WIN32) || defined(_WIN64)
__declspec(dllimport) unsigned long __stdcall SetThreadExecutionState(unsigned long iFlags);
#endif
#define MDO_ES_CONTINUOUS      0x80000000ul
#define MDO_ES_SYSTEM_REQUIRED 0x00000001ul

static void MdoApplyPreventSleep(bool bOn)
{
#if defined(_WIN32) || defined(_WIN64)
	if ( bOn ) {
		SetThreadExecutionState(MDO_ES_CONTINUOUS | MDO_ES_SYSTEM_REQUIRED);
	} else {
		SetThreadExecutionState(MDO_ES_CONTINUOUS);
	}
#endif
}

/* ------------------------------------------------------------------ */
/* 模型客户端缓存                                                       */
/* ------------------------------------------------------------------ */

static xllm_provider MdoDialectProvider(const char* sDialect)
{
	if ( strcmp(sDialect, "anthropic") == 0 )  return XLLM_PROVIDER_ANTHROPIC;
	if ( strcmp(sDialect, "responses") == 0 )  return XLLM_PROVIDER_OPENAI_RESPONSES;
	if ( strcmp(sDialect, "glm") == 0 )        return XLLM_PROVIDER_GLM;
	return XLLM_PROVIDER_OPENAI_COMPAT;
}

/* 惰性创建/复用模型客户端；失败返回 NULL（pErr 可选） */
static xllm_client* MdoClientFor(MdoModel* pM, xllm_error* pErr)
{
	xllm_client_config tCfg;

	if ( pM->pClient != NULL ) return pM->pClient;
	if ( pM->aBaseUrl[0] == 0 || pM->aModel[0] == 0 ) {
		if ( pErr ) { xllmErrorInit(pErr); snprintf(pErr->sMessage, sizeof(pErr->sMessage),
			"model '%s' missing baseUrl/model", pM->aId); }
		return NULL;
	}
	xllmClientConfigInit(&tCfg);
	tCfg.sBaseUrl = pM->aBaseUrl;
	/* 全局代理：设置启用且目标主机不在 bypass 列表 → 注入（模型级直连不受影响） */
	if ( g_settings.bProxyEnabled && g_settings.aProxyHost[0] != 0
		&& g_settings.uProxyPort != 0
		&& pM->aBaseUrl[0] != 0
		&& strncmp(pM->aBaseUrl, "http://127.0.0.1", 16) != 0
		&& strncmp(pM->aBaseUrl, "http://localhost", 15) != 0 ) {
		const char* sTarget = pM->aBaseUrl;
		char aHost[200] = {0};
		/* 从 URL 提取主机（https?://host[:port]/...）做 bypass 匹配 */
		if ( strncmp(sTarget, "http://", 7) == 0 ) sTarget += 7;
		else if ( strncmp(sTarget, "https://", 8) == 0 ) sTarget += 8;
		{
			size_t n = 0;
			while ( sTarget[n] && sTarget[n] != '/' && sTarget[n] != ':' && n < sizeof(aHost) - 1 )
				{ aHost[n] = sTarget[n]; n++; }
		}
		if ( aHost[0] != 0 ) {
			/* bypass 简版匹配：精确/前缀通配（xllm 侧还有完整实现兜底） */
			bool bBypass = false;
			const char* p = g_settings.aProxyBypass;
			while ( *p && !bBypass ) {
				char aEntry[64];
				size_t nE = 0;
				while ( *p && *p != ',' && *p != ';' && nE < sizeof(aEntry) - 1 )
					{ aEntry[nE++] = *p++; }
				aEntry[nE] = 0;
				if ( *p == ',' || *p == ';' ) p++;
				if ( nE == 1 && aEntry[0] == '*' ) bBypass = true;
				else if ( nE > 1 && aEntry[nE - 1] == '*' ) {
					aEntry[nE - 1] = 0;
					bBypass = strncmp(aHost, aEntry, nE - 1) == 0;
				}
				else if ( nE > 0 ) {
					bBypass = strcmp(aHost, aEntry) == 0;
				}
			}
			if ( !bBypass ) {
				tCfg.eProxyKind = 2;   /* http_connect */
				tCfg.sProxyHost = g_settings.aProxyHost;
				tCfg.uProxyPort = (uint16)g_settings.uProxyPort;
				tCfg.sProxyUser = g_settings.aProxyUser[0] ? g_settings.aProxyUser : NULL;
				tCfg.sProxyPass = g_settings.aProxyPass[0] ? g_settings.aProxyPass : NULL;
				tCfg.sProxyBypass = g_settings.aProxyBypass;
			}
		}
	}
	tCfg.sApiKey = pM->aApiKey[0] ? pM->aApiKey : NULL;
	tCfg.sModel = pM->aModel;
	tCfg.sReasoningEffort = pM->aReasoning[0] ? pM->aReasoning : NULL;
	tCfg.eProvider = MdoDialectProvider(pM->aDialect);
	{
		const char* sGlobalCa = MdoCaBundle();
		if ( pM->aCaPem[0] ) {
			size_t iSize = 0;
			if ( pM->sCaPemText == NULL )
				pM->sCaPemText = xrtFileReadText(pM->aCaPem, XENCODING_UTF8,
					XUTF_REPLACE, &iSize);
			if ( sGlobalCa != NULL && pM->sCaPemText != NULL ) {
				/* 全局 CA + 模型 CA 拼接（全局在前） */
				size_t nG = strlen(sGlobalCa), nM = strlen(pM->sCaPemText);
				char* pBoth = (char*)xrtRealloc(NULL, nG + nM + 2);
				if ( pBoth != NULL ) {
					memcpy(pBoth, sGlobalCa, nG);
					pBoth[nG] = '\n';
					memcpy(pBoth + nG + 1, pM->sCaPemText, nM + 1);
					xrtFree(pM->sCaPemText);
					pM->sCaPemText = pBoth;
				}
			}
			tCfg.sCaPem = pM->sCaPemText;
			tCfg.bVerifyPeer = true;
		} else if ( sGlobalCa != NULL ) {
			tCfg.sCaPem = sGlobalCa;
			tCfg.bVerifyPeer = true;
		}
	}
	pM->pClient = xllmClientCreate(&tCfg, pErr);
	return pM->pClient;
}

static void MdoModelDropClient(MdoModel* pM)
{
	if ( pM->pClient != NULL ) {
		xllmClientDestroy(pM->pClient);
		pM->pClient = NULL;
	}
}

static MdoModel* MdoModelFind(const char* sId)
{
	size_t i;
	if ( sId == NULL || sId[0] == 0 ) return NULL;
	for ( i = 0; i < g_nModels; i++ )
		if ( strcmp(g_models[i].aId, sId) == 0 ) return &g_models[i];
	return NULL;
}

/* ------------------------------------------------------------------ */
/* 项目注册表                                                           */
/* ------------------------------------------------------------------ */

static uint64 MdoPathHash(const char* sPath)
{
	uint64 h = 1469598103934665603ull;
	const unsigned char* p = (const unsigned char*)sPath;
	while ( *p ) { h ^= *p++; h *= 1099511628211ull; }
	return h;
}

static void MdoSlugFromPath(const char* sPath, char* pOut, size_t iCap)
{
	const char* pBase = strrchr(sPath, '/');
	const char* pBase2 = strrchr(sPath, '\\');
	char aClean[32];
	size_t n = 0;

	if ( pBase2 != NULL && (pBase == NULL || pBase2 > pBase) ) pBase = pBase2;
	if ( pBase == NULL ) pBase = sPath; else pBase++;
	while ( *pBase && n < sizeof(aClean) - 8 ) {
		char c = *pBase++;
		if ( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		     (c >= '0' && c <= '9') || c == '-' || c == '_' )
			aClean[n++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
	}
	aClean[n] = 0;
	if ( n == 0 ) snprintf(aClean, sizeof(aClean), "proj");
	snprintf(pOut, iCap, "%s-%08llx", aClean,
		(unsigned long long)(MdoPathHash(sPath) & 0xFFFFFFFFull));
}

static void MdoProjectSaveLocked(const MdoProject* pProj)
{
	char aPath[340];
	char* sText;
	xjsonwriter* pW = JWOpen();

	xrtJsonWriterObject(pW);
	JWName(pW, "name"); JWStr(pW, pProj->aName);
	JWName(pW, "path"); JWStr(pW, pProj->aPath);
	JWName(pW, "defaultModel"); JWStr(pW, pProj->aDefaultModel);
	xrtJsonWriterEnd(pW);
	sText = JWTake(pW);
	if ( sText != NULL ) {
		char* pDir = MdoPathJoin(g_projectsDir, pProj->aSlug);
		if ( pDir != NULL ) {
			xrtDirCreateAll(pDir);
			snprintf(aPath, sizeof(aPath), "%s", pDir);
			xrtFree(pDir);
			{
				char* pFile = MdoPathJoin(aPath, "project.json");
				if ( pFile != NULL ) {
					xrtFileWriteTextAtomic(pFile, xrtStrView(sText),
						XENCODING_UTF8, XUTF_REPLACE, false);
					xrtFree(pFile);
				}
			}
		}
		xrtFree(sText);
	}
	/* sessions 子目录 + 项目记忆目录 */
	{
		char* pDir = MdoPathJoin(g_projectsDir, pProj->aSlug);
		if ( pDir != NULL ) {
			char* pSess = MdoPathJoin(pDir, "sessions");
			if ( pSess != NULL ) { xrtDirCreateAll(pSess); xrtFree(pSess); }
			xrtFree(pDir);
		}
	}
	{
		char aMem[300];
		MdoMemoryDir(pProj, true, aMem, sizeof(aMem));
		MdoMemoryEnsure(aMem);
	}
}

static void MdoProjectsScanLocked(void)
{
	xdir hDir = xrtDirOpen(g_projectsDir, 0);
	xdirentry tEntry;

	g_nProjects = 0;
	if ( hDir == NULL ) return;
	while ( xrtDirNext(hDir, &tEntry) == XDIR_NEXT_ITEM ) {
		char aPP[300];
		char* pFile;
		size_t iSize = 0;
		str sText;

		if ( (tEntry.Flags & XDIR_ENTRY_UTF8) == 0 ) continue;
		if ( tEntry.Name.Size == 0 || tEntry.Name.Data[0] == '.' ) continue;
		pFile = MdoPathJoin(g_projectsDir, tEntry.Name.Data);
		if ( pFile == NULL ) continue;
		snprintf(aPP, sizeof(aPP), "%s", pFile);
		xrtFree(pFile);
		pFile = MdoPathJoin(aPP, "project.json");
		if ( pFile == NULL ) continue;
		sText = xrtFileReadText(pFile, XENCODING_UTF8, XUTF_REPLACE, &iSize);
		xrtFree(pFile);
		if ( sText == NULL ) continue;
		{
			MdoJson tJ;
			if ( MdoJsonParse(&tJ, sText) && g_nProjects < MDO_MAX_PROJECTS ) {
				MdoProject* pP = &g_projects[g_nProjects];
				memset(pP, 0, sizeof(*pP));
				snprintf(pP->aSlug, sizeof(pP->aSlug), "%s", tEntry.Name.Data);
				JStr(&tJ, "name", pP->aName, sizeof(pP->aName));
				JStr(&tJ, "path", pP->aPath, sizeof(pP->aPath));
				JStr(&tJ, "defaultModel", pP->aDefaultModel, sizeof(pP->aDefaultModel));
				pP->bValidPath = pP->aPath[0] && xrtDirExists(pP->aPath);
				if ( pP->aName[0] && pP->aPath[0] ) g_nProjects++;
				MdoJsonFree(&tJ);
			}
		}
		xrtFree(sText);
	}
	xrtDirClose(hDir);
}

/* 项目注销：bPurge=true 连数据桶一起删（会话全灭）；活动项目被删则清空选择 */
static bool MdoProjectDeleteLocked(const char* sSlug, bool bPurge)
{
	size_t i;
	bool bFound = false;

	for ( i = 0; i < g_nProjects; i++ )
		if ( strcmp(g_projects[i].aSlug, sSlug) == 0 ) { bFound = true; break; }
	if ( !bFound ) return false;
	/* 运行中的会话禁止清理其数据桶 */
	if ( bPurge ) {
		size_t j;
		for ( j = 0; j < g_nSessions; j++ )
			if ( g_sessions[j]->pRun != NULL ) return false;
	}
	if ( bPurge ) {
		char* pDir = MdoPathJoin(g_projectsDir, sSlug);
		if ( pDir != NULL ) {
			MdoTrashDir(pDir, "project-purge");
			xrtFree(pDir);
		}
	}
	memmove(&g_projects[i], &g_projects[i + 1],
		(g_nProjects - i - 1) * sizeof(MdoProject));
	g_nProjects--;
	if ( strcmp(g_activeProject, sSlug) == 0 ) g_activeProject[0] = 0;
	MdoConfigSaveLocked();
	return true;
}

/* ------------------------------------------------------------------ */
/* 会话表（内存缓存 + meta 落盘）                                        */
/* ------------------------------------------------------------------ */

static void MdoSessionSaveMetaLocked(const MdoSession* pS)
{
	MdoProject* pProj = MdoActiveProject();
	char aMeta[380];
	char* sText;
	xjsonwriter* pW;

	MdoSessionPaths(pProj, pS->aId, aMeta, sizeof(aMeta), NULL, 0, NULL, 0, NULL, 0);
	(void)pProj;
	pW = JWOpen();
	xrtJsonWriterObject(pW);
	JWName(pW, "id");        JWStr(pW, pS->aId);
	JWName(pW, "title");     JWStr(pW, pS->aTitle);
	JWName(pW, "model");     JWStr(pW, pS->aModelId);
	JWName(pW, "contextWindow"); JWUInt(pW, pS->uContextWindow);
	JWName(pW, "createdAt"); JWUInt(pW, pS->uCreatedAt);
	JWName(pW, "updatedAt"); JWUInt(pW, pS->uUpdatedAt);
	JWName(pW, "turns");     JWUInt(pW, pS->uTurns);
	JWName(pW, "pinned");    JWBool(pW, pS->bPinned);
	JWName(pW, "userPrompt"); JWStr(pW, pS->aUserPrompt);
	xrtJsonWriterEnd(pW);
	sText = JWTake(pW);
	if ( sText != NULL ) {
		xrtFileWriteTextAtomic(aMeta, xrtStrView(sText),
			XENCODING_UTF8, XUTF_REPLACE, false);
		xrtFree(sText);
	}
}

static MdoSession* MdoSessionFindLocked(const char* sId)
{
	size_t i;
	for ( i = 0; i < g_nSessions; i++ )
		if ( strcmp(g_sessions[i]->aId, sId) == 0 ) return g_sessions[i];
	return NULL;
}

static MdoSession* MdoSessionCreateLocked(const char* sTitle, const char* sModelId,
	uint64 uWindow, const char* sUserPrompt)
{
	MdoSession* pS;
	char aId[40];
	MdoProject* pProj = MdoActiveProject();
	char aMeta[380], aJournal[380], aUi[380];

	if ( g_nSessions >= MDO_MAX_SESSIONS ) return NULL;
	pS = (MdoSession*)xrtRealloc(NULL, sizeof(MdoSession));
	if ( pS == NULL ) return NULL;
	memset(pS, 0, sizeof(*pS));
	snprintf(aId, sizeof(aId), "s%08x%04x",
		(unsigned)(MdoNowMs() & 0xFFFFFFFFu), (unsigned)(MdoRandHex() & 0xFFFFu));
	g_sessions[g_nSessions++] = pS;
	snprintf(pS->aId, sizeof(pS->aId), "%s", aId);
	snprintf(pS->aTitle, sizeof(pS->aTitle), "%s",
		(sTitle && sTitle[0]) ? sTitle : "新的会话");
	/* 模型解析链：显式 > 项目默认 > 全局默认 */
	{
		const char* sFallback = g_defaultModel;
		if ( pProj != NULL && pProj->aDefaultModel[0] ) sFallback = pProj->aDefaultModel;
		snprintf(pS->aModelId, sizeof(pS->aModelId), "%s",
			(sModelId && sModelId[0]) ? sModelId : sFallback);
	}
	pS->uContextWindow = uWindow ? uWindow : 128000;
	pS->uCreatedAt = pS->uUpdatedAt = MdoNowMs();
	pS->uTurns = 0;
	if ( sUserPrompt != NULL && sUserPrompt[0] ) {
		size_t nU = strlen(sUserPrompt);
		if ( nU > sizeof(pS->aUserPrompt) - 1 ) nU = sizeof(pS->aUserPrompt) - 1;
		memcpy(pS->aUserPrompt, sUserPrompt, nU);
		pS->aUserPrompt[nU] = 0;
	}

	MdoSessionPaths(pProj, pS->aId, aMeta, sizeof(aMeta), aJournal,
		sizeof(aJournal), NULL, 0, aUi, sizeof(aUi));
	xrtFileWriteTextAtomic(aMeta, xrtStrView("{}"), XENCODING_UTF8, XUTF_REPLACE, false);
	xrtFileWriteTextAtomic(aUi, xrtStrView(""), XENCODING_UTF8, XUTF_REPLACE, false);
	MdoSessionSaveMetaLocked(pS);
	return pS;
}

static void MdoSessionCloseHandles(MdoSession* pS)
{
	if ( pS->pSession != NULL ) {
		xllmSessionDestroy(pS->pSession);
		pS->pSession = NULL;
	}
	if ( pS->fUi != NULL ) {
		fclose(pS->fUi);
		pS->fUi = NULL;
	}
	pS->bLoaded = false;
}

static bool MdoSessionDeleteLocked(const char* sId)
{
	MdoSession* pS = MdoSessionFindLocked(sId);
	MdoProject* pProj;
	char aMeta[380], aJournal[380], aSnap[380], aUi[380];
	size_t i;

	if ( pS == NULL ) return false;
	if ( pS->pRun != NULL ) return false;    /* 运行中禁删 */
	pProj = MdoActiveProject();   /* 任务态为 NULL：路径落到 _tasks 桶 */
	MdoSessionCloseHandles(pS);
	MdoSessionPaths(pProj, sId, aMeta, sizeof(aMeta), aJournal,
		sizeof(aJournal), aSnap, sizeof(aSnap), aUi, sizeof(aUi));
	MdoTrashFile(aMeta, "session-delete");
	MdoTrashFile(aJournal, "session-delete");
	MdoTrashFile(aSnap, "session-delete");
	MdoTrashFile(aUi, "session-delete");
	for ( i = 0; i < g_nSessions; i++ )
		if ( g_sessions[i] == pS ) break;
	if ( i < g_nSessions ) {
		memmove(&g_sessions[i], &g_sessions[i + 1],
			(g_nSessions - i - 1) * sizeof(MdoSession*));
		g_nSessions--;
	}
	xrtFree(pS);
	return true;
}

/* 扫描活动项目的会话元数据进内存表（先清空非运行项） */
/* sSlug==NULL → 活动项目（显式点名优先：项目切换路径避免与 activate 乱序竞态） */
static void MdoSessionsScanProjectLocked(const char* sSlug)
{
	const char* sUse = (sSlug && sSlug[0]) ? sSlug :
		(g_activeProject[0] ? g_activeProject : MDO_TASKS_SLUG);
	char aSessDir[340];
	xdir hDir;
	xdirentry tEntry;

	if ( sUse[0] == 0 ) { g_nSessions = 0; return; }
	{
		char* pSlugDir = MdoPathJoin(g_projectsDir, sUse);
		char* pDir = pSlugDir ? MdoPathJoin(pSlugDir, "sessions") : NULL;
		xrtFree(pSlugDir);
		if ( pDir == NULL ) { g_nSessions = 0; return; }
		snprintf(aSessDir, sizeof(aSessDir), "%s", pDir);
		xrtFree(pDir);
	}

	/* 保留运行中的会话对象，其余释放后重扫 */
	{
		MdoSession* aKeep[MDO_MAX_SESSIONS];
		size_t nKeep = 0, i;
		for ( i = 0; i < g_nSessions; i++ )
			if ( g_sessions[i]->pRun != NULL )
				aKeep[nKeep++] = g_sessions[i];
		for ( i = 0; i < g_nSessions; i++ )
			if ( g_sessions[i]->pRun == NULL ) {
				MdoSessionCloseHandles(g_sessions[i]);
				xrtFree(g_sessions[i]);
			}
		memcpy(g_sessions, aKeep, nKeep * sizeof(MdoSession*));
		g_nSessions = nKeep;
	}

	hDir = xrtDirOpen(aSessDir, 0);
	if ( hDir == NULL ) return;
	while ( xrtDirNext(hDir, &tEntry) == XDIR_NEXT_ITEM &&
	        g_nSessions < MDO_MAX_SESSIONS ) {
		char aMeta[420], aId[40];
		char* pFile;
		size_t iSize = 0;
		size_t nName = tEntry.Name.Size;
		str sText;

		if ( nName < 14 || nName > 44 ) continue;          /* s<hex12>.meta.json */
		if ( strcmp(tEntry.Name.Data + nName - 10, ".meta.json") != 0 ) continue;
		memcpy(aId, tEntry.Name.Data, nName - 10);
		aId[nName - 10] = 0;
		pFile = MdoPathJoin(aSessDir, tEntry.Name.Data);
		if ( pFile == NULL ) continue;
		snprintf(aMeta, sizeof(aMeta), "%s", pFile);
		xrtFree(pFile);
		sText = xrtFileReadText(aMeta, XENCODING_UTF8, XUTF_REPLACE, &iSize);
		if ( sText == NULL ) continue;
		{
			MdoJson tJ;
			if ( MdoJsonParse(&tJ, sText) ) {
				MdoSession* pS = (MdoSession*)xrtRealloc(NULL, sizeof(MdoSession));
				if ( pS == NULL ) { xrtFree(sText); continue; }
				memset(pS, 0, sizeof(*pS));
				snprintf(pS->aId, sizeof(pS->aId), "%s", aId);
				JStr(&tJ, "title", pS->aTitle, sizeof(pS->aTitle));
				JStr(&tJ, "model", pS->aModelId, sizeof(pS->aModelId));
				pS->uContextWindow = (uint64)JInt(&tJ, "contextWindow", 128000);
				pS->uCreatedAt = (uint64)JInt(&tJ, "createdAt", 0);
				pS->uUpdatedAt = (uint64)JInt(&tJ, "updatedAt", 0);
				pS->uTurns = (uint32)JInt(&tJ, "turns", 0);
				{
					xvalue* pV = xrtValueObjectGet(tJ.pRoot, xrtStrView("pinned"));
					bool b = false;
					pS->bPinned = (pV != NULL && xrtValueGetBool(pV, &b)) ? b : false;
				}
				JStr(&tJ, "userPrompt", pS->aUserPrompt, sizeof(pS->aUserPrompt));
				if ( pS->uUpdatedAt == 0 ) pS->uUpdatedAt = pS->uCreatedAt;
				if ( pS->aTitle[0] ) g_sessions[g_nSessions++] = pS;
				else xrtFree(pS);
				MdoJsonFree(&tJ);
			}
		}
		xrtFree(sText);
	}
	xrtDirClose(hDir);
}

/* 全库轻量扫描（/api/sessions?all=1）：不动 g_sessions 运行态表，
 * 逐桶读 *.meta.json 直接写 JSON 数组项（侧栏跨项目分组用） */
static void MdoSessionsScanAllLocked(xjsonwriter* pW)
{
	const char* aSlugs[MDO_MAX_PROJECTS + 1];
	size_t nSlugs = 0, i, k;

	for ( i = 0; i < g_nProjects; i++ ) aSlugs[nSlugs++] = g_projects[i].aSlug;
	aSlugs[nSlugs++] = MDO_TASKS_SLUG;
	for ( k = 0; k < nSlugs; k++ ) {
		char aSessDir[340];
		xdir hDir;
		xdirentry tEntry;
		{
			char* pSlugDir = MdoPathJoin(g_projectsDir, aSlugs[k]);
			char* pDir = pSlugDir ? MdoPathJoin(pSlugDir, "sessions") : NULL;
			xrtFree(pSlugDir);
			if ( pDir == NULL ) continue;
			snprintf(aSessDir, sizeof(aSessDir), "%s", pDir);
			xrtFree(pDir);
		}
		hDir = xrtDirOpen(aSessDir, 0);
		if ( hDir == NULL ) continue;
		while ( xrtDirNext(hDir, &tEntry) == XDIR_NEXT_ITEM ) {
			char aMeta[460];
			char* pFile;
			size_t iSize = 0;
			size_t nName = tEntry.Name.Size;
			str sText;

			if ( nName < 14 || nName > 44 ) continue;
			if ( strcmp(tEntry.Name.Data + nName - 10, ".meta.json") != 0 ) continue;
			pFile = MdoPathJoin(aSessDir, tEntry.Name.Data);
			if ( pFile == NULL ) continue;
			snprintf(aMeta, sizeof(aMeta), "%s", pFile);
			xrtFree(pFile);
			sText = xrtFileReadText(aMeta, XENCODING_UTF8, XUTF_REPLACE, &iSize);
			if ( sText == NULL ) continue;
			{
				MdoJson tJ;
				if ( MdoJsonParse(&tJ, sText) ) {
					char aTitle[128], aModel[64];
					uint64 uUpdated = 0, uCreated = 0;
					uint32 uTurns2 = 0;
					bool bPin = false;
					JStr(&tJ, "title", aTitle, sizeof(aTitle));
					JStr(&tJ, "model", aModel, sizeof(aModel));
					uCreated = (uint64)JInt(&tJ, "createdAt", 0);
					uUpdated = (uint64)JInt(&tJ, "updatedAt", 0);
					uTurns2 = (uint32)JInt(&tJ, "turns", 0);
					{
						xvalue* pV = xrtValueObjectGet(tJ.pRoot, xrtStrView("pinned"));
						bool b = false;
						bPin = (pV != NULL && xrtValueGetBool(pV, &b)) ? b : false;
					}
					if ( uUpdated == 0 ) uUpdated = uCreated;
					if ( aTitle[0] ) {
						xrtJsonWriterObject(pW);
						JWName(pW, "id");
						JWStrN(pW, tEntry.Name.Data, nName - 10);
						JWName(pW, "title");   JWStr(pW, aTitle);
						JWName(pW, "model");   JWStr(pW, aModel);
						JWName(pW, "updatedAt"); xrtJsonWriterUInt(pW, uUpdated);
						JWName(pW, "turns");   xrtJsonWriterUInt(pW, uTurns2);
						JWName(pW, "pinned");  JWBool(pW, bPin);
						JWName(pW, "project"); JWStr(pW, aSlugs[k]);
						xrtJsonWriterEnd(pW);
					}
					MdoJsonFree(&tJ);
				}
			}
			xrtFree(sText);
		}
		xrtDirClose(hDir);
	}
}

/* ------------------------------------------------------------------ */
/* UI 事件日志（NDJSON；前端重放与回合轮询共用一份格式）                  */
/* ------------------------------------------------------------------ */

static void MdoUiAppend(MdoSession* pS, const char* sLine)
{
	MdoProject* pProj;
	char aUi[380];

	if ( pS->fUi == NULL ) {
		pProj = MdoActiveProject();   /* 任务态为 NULL：路径落到 _tasks 桶 */
		MdoSessionPaths(pProj, pS->aId, NULL, 0, NULL, 0, NULL, 0, aUi, sizeof(aUi));
		pS->fUi = fopen(aUi, "ab");
		if ( pS->fUi == NULL ) return;
	}
	fputs(sLine, pS->fUi);
	fputc('\n', pS->fUi);
	fflush(pS->fUi);
}

/* 读整个 UI 日志到堆缓冲（caller xrtFree）；行数写入 pCount */
static char* MdoUiReadAll(const MdoSession* pS, size_t* pCount)
{
	MdoProject* pProj;
	char aUi[380];
	size_t iSize = 0;
	bytes pData;
	size_t i, n = 0;

	*pCount = 0;
	pProj = MdoActiveProject();   /* 任务态为 NULL：路径落到 _tasks 桶 */
	MdoSessionPaths(pProj, pS->aId, NULL, 0, NULL, 0, NULL, 0, aUi, sizeof(aUi));
	pData = xrtFileReadAll(aUi, &iSize);
	if ( pData == NULL || iSize == 0 ) { xrtFree(pData); return NULL; }
	for ( i = 0; i < iSize; i++ ) if ( pData[i] == '\n' ) n++;
	*pCount = n;
	return (char*)pData;
}

/* ------------------------------------------------------------------ */
/* 系统提示词（mdo 设计文档 §七：环境块 + AGENTS.md）                     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 记忆（§12.3 + 终裁④：零工具、提示词约定、明文可审计）                  */
/* ------------------------------------------------------------------ */

#define MDO_MEMORY_INDEX_MAX 2048u    /* 单个 MEMORY.md 注入截断 */

/* 记忆目录路径：bProject=false → 全局 <data>/memory；true → 项目桶 memory/ */
static void MdoMemoryDir(const MdoProject* pProj, bool bProject, char* pOut, size_t iCap)
{
	if ( bProject && pProj != NULL ) {
		char* pSlugDir = MdoPathJoin(g_projectsDir, pProj->aSlug);
		char* pMem = pSlugDir ? MdoPathJoin(pSlugDir, "memory") : NULL;
		snprintf(pOut, iCap, "%s", pMem ? pMem : "");
		xrtFree(pMem);
		xrtFree(pSlugDir);
	} else {
		char* pMem = MdoPathJoin(g_mdoHome, "memory");
		snprintf(pOut, iCap, "%s", pMem ? pMem : "");
		xrtFree(pMem);
	}
}

/* 首启确保目录 + 种子索引（幂等；已存在不动） */
static void MdoMemoryEnsure(const char* sDir)
{
	char aIndex[380];
	char* pFile;

	if ( sDir == NULL || sDir[0] == 0 ) return;
	xrtDirCreateAll(sDir);
	pFile = MdoPathJoin(sDir, "MEMORY.md");
	if ( pFile == NULL ) return;
	snprintf(aIndex, sizeof(aIndex), "%s", pFile);
	xrtFree(pFile);
	if ( !xrtFileExists(aIndex) ) {
		xrtFileWriteTextAtomic(aIndex, xrtStrView(
			"# 记忆索引\n"
			"\n"
			"<!-- 一行一条：- [标题](文件名.md) — 一句话钩子 -->\n"
			"<!-- 新记忆 = 新建文件（一条一事，frontmatter 标 type）+ 在此补一行；\n"
			"     优先新建而非改旧文；仓库/文档已记录的事实不存 -->\n"),
			XENCODING_UTF8, XUTF_REPLACE, false);
	}
}

/* 读索引：只取真实条目行（"- " 开头），模板注释不注入；无条目返回 false */
static bool MdoMemoryReadIndex(const char* sDir, char* pOut, size_t iCap)
{
	char aIndex[380];
	char* pFile;
	size_t iSize = 0;
	str sText;
	size_t n = 0;
	const char* p;

	pOut[0] = 0;
	if ( sDir == NULL || sDir[0] == 0 ) return false;
	pFile = MdoPathJoin(sDir, "MEMORY.md");
	if ( pFile == NULL ) return false;
	snprintf(aIndex, sizeof(aIndex), "%s", pFile);
	xrtFree(pFile);
	sText = xrtFileReadText(aIndex, XENCODING_UTF8, XUTF_REPLACE, &iSize);
	if ( sText == NULL ) return false;
	p = sText;
	while ( *p != 0 && n < iCap - 2 ) {
		const char* pEnd = strchr(p, '\n');
		size_t iLen = pEnd ? (size_t)(pEnd - p) : strlen(p);
		if ( iLen >= 2 && p[0] == '-' && p[1] == ' ' ) {
			size_t iCopy = iLen;
			if ( iCopy > iCap - 2 - n ) iCopy = iCap - 2 - n;
			memcpy(pOut + n, p, iCopy);
			n += iCopy;
			pOut[n++] = '\n';
		}
		p = pEnd ? pEnd + 1 : p + iLen;
	}
	pOut[n] = 0;
	n = Utf8Clip(pOut, n, MDO_MEMORY_INDEX_MAX);
	pOut[n] = 0;
	xrtFree(sText);
	return n > 0;
}

static void MdoBuildSystemPrompt(const MdoProject* pProj, const MdoSession* pSess,
	char* pOut, size_t iCap)
{
	size_t n = 0;
	char aAgents[380];
	char* sAgents = NULL;
	char aMemGlobal[300], aMemProject[300];
	char aIdxG[MDO_MEMORY_INDEX_MAX + 8], aIdxP[MDO_MEMORY_INDEX_MAX + 8];

		/* 工作目录：项目态=项目路径；任务态=_tasks 桶真实路径（模型不可见时必幻觉路径） */
	char aWorkDir[400];
	if ( pProj != NULL && pProj->aPath[0] ) {
		snprintf(aWorkDir, sizeof(aWorkDir), "%s", pProj->aPath);
	} else {
		char* pSlugDir = MdoPathJoin(g_projectsDir, MDO_TASKS_SLUG);
		snprintf(aWorkDir, sizeof(aWorkDir), "%s", pSlugDir ? pSlugDir : ".");
		xrtFree(pSlugDir);
	}
	const char* sWorkDir = aWorkDir;
	n += (size_t)snprintf(pOut + n, iCap - n,
		"你是 mdo（墨斗），一个 agent 工作台。\n\n"
		"环境: Windows\n"
		"工作目录: %s（工具的相对路径以此为基准）\n"
		"命令: exec/spawn 的参数是字符串数组、直接执行；没有 shell，不支持管道、重定向、通配符。已配置 curl、git、python，直接按程序名调用。\n"
		"工具: 遍历目录用 ls；按文件名找文件用 glob；搜文件内容用 grep；读文件用 read；写文件用 write；改文件用 edit；跑命令用 exec（同步等待）或 spawn（后台任务）；搜索网络用 web_search；需要用户决定时用 ask_user。\n",
		sWorkDir);

	if ( pProj != NULL && pProj->aPath[0] ) {
		char* pFile = MdoPathJoin(pProj->aPath, "AGENTS.md");
		if ( pFile != NULL ) {
			size_t iAgentsSize = 0;
			snprintf(aAgents, sizeof(aAgents), "%s", pFile);
			xrtFree(pFile);
			sAgents = xrtFileReadText(aAgents, XENCODING_UTF8, XUTF_REPLACE, &iAgentsSize);
		}
	}
	if ( sAgents != NULL ) {
		size_t iLen = strlen(sAgents);
		size_t iClip = Utf8Clip(sAgents, iLen, 8000);
		n += (size_t)snprintf(pOut + n, iCap - n,
			"\n项目指令 (AGENTS.md):\n%.*s\n", (int)iClip, sAgents);
		xrtFree(sAgents);
	}

	/* 记忆块：只给目录事实；索引有真实条目才注入，模板注释不注入 */
	MdoMemoryDir(pProj, false, aMemGlobal, sizeof(aMemGlobal));
	MdoMemoryDir(pProj, true, aMemProject, sizeof(aMemProject));
	n += (size_t)snprintf(pOut + n, iCap - n,
		"\n记忆（明文文件，用文件工具直接维护；MEMORY.md 为索引，一条一事一文件）:\n"
		"全局: %s\n"
		"项目: %s\n",
		aMemGlobal, aMemProject);
	if ( MdoMemoryReadIndex(aMemGlobal, aIdxG, sizeof(aIdxG)) )
		n += (size_t)snprintf(pOut + n, iCap - n, "\n全局记忆索引:\n%s\n", aIdxG);
	if ( MdoMemoryReadIndex(aMemProject, aIdxP, sizeof(aIdxP)) )
		n += (size_t)snprintf(pOut + n, iCap - n, "\n项目记忆索引:\n%s\n", aIdxP);

	/* 用户全局指令（建会话时快照，仅新会话生效） */
	if ( pSess != NULL && pSess->aUserPrompt[0] ) {
		n += (size_t)snprintf(pOut + n, iCap - n,
			"\n用户全局指令:\n%.*s\n", (int)(sizeof(pSess->aUserPrompt) - 1),
			pSess->aUserPrompt);
	}
}

/* ------------------------------------------------------------------ */
/* 数据安全：回收站 + 审计（一切删除先入 <data>/.trash/ 并留痕）          */
/* ------------------------------------------------------------------ */

static void MdoAuditLog(const char* sAction, const char* sDetail)
{
	char aPath[340];
	char* pFile;
	FILE* f;

	pFile = MdoPathJoin(g_mdoHome, "audit.log");
	if ( pFile == NULL ) return;
	snprintf(aPath, sizeof(aPath), "%s", pFile);
	xrtFree(pFile);
	f = fopen(aPath, "ab");
	if ( f == NULL ) return;
	fprintf(f, "%llu\t%s\t%s\n",
		(unsigned long long)MdoNowMs(), sAction, sDetail ? sDetail : "");
	fclose(f);
}

/* 文件入回收站（移动失败则保留原文件不删——失败安全方向=不丢数据） */
static bool MdoTrashFile(const char* sPath, const char* sAction)
{
	char aTrash[340], aTarget[380], aName[128];
	char* pDir;
	const char* pBase;
	const char* pSlash;
	const char* pSlash2;
	size_t n;

	if ( sPath == NULL || sPath[0] == 0 || !xrtFileExists(sPath) ) return true;
	pDir = MdoPathJoin(g_mdoHome, ".trash");
	if ( pDir == NULL ) return false;
	snprintf(aTrash, sizeof(aTrash), "%s", pDir);
	xrtFree(pDir);
	xrtDirCreateAll(aTrash);
	pSlash = strrchr(sPath, '/');
	pSlash2 = strrchr(sPath, '\\');
	pBase = (pSlash2 != NULL && (pSlash == NULL || pSlash2 > pSlash))
		? pSlash2 + 1 : (pSlash ? pSlash + 1 : sPath);
	n = 0;
	while ( pBase[n] && n < sizeof(aName) - 24 ) { aName[n] = pBase[n]; n++; }
	aName[n] = 0;
	snprintf(aTarget, sizeof(aTarget), "%s/%llu-%s", aTrash,
		(unsigned long long)MdoNowMs(), aName);
	if ( !xrtFileMove(sPath, aTarget, true) ) {
		MdoAuditLog("trash-failed", sPath);
		return false;   /* 移不动就不删 */
	}
	MdoAuditLog(sAction ? sAction : "trash", sPath);
	return true;
}

/* 目录入回收站（项目 purge 用） */
static bool MdoTrashDir(const char* sPath, const char* sAction)
{
	char aTrash[340], aTarget[380];
	char* pDir;

	if ( sPath == NULL || sPath[0] == 0 || !xrtDirExists(sPath) ) return true;
	pDir = MdoPathJoin(g_mdoHome, ".trash");
	if ( pDir == NULL ) return false;
	snprintf(aTrash, sizeof(aTrash), "%s", pDir);
	xrtFree(pDir);
	xrtDirCreateAll(aTrash);
	snprintf(aTarget, sizeof(aTarget), "%s/%llu-purge", aTrash,
		(unsigned long long)MdoNowMs());
	if ( !xrtDirMove(sPath, aTarget, true) ) {
		MdoAuditLog("trash-dir-failed", sPath);
		return false;
	}
	MdoAuditLog(sAction ? sAction : "trash-dir", sPath);
	return true;
}

/* ------------------------------------------------------------------ */
/* 启动/停止                                                           */
/* ------------------------------------------------------------------ */

static void MdoStoreInit(void)
{
	str sTmp = NULL;
	char* pJoin = NULL;

	g_lock = xrtMutexCreate();

	/* 工具目录：<exe>/tools，注入本进程 PATH（子进程 exec/spawn 直接可用） */
	{
		char* pExe = xrtPathExecutable();
		if ( pExe != NULL ) {
			char* pDir = strrchr(pExe, '/');
			char* pDir2 = strrchr(pExe, '\\');
			char* pLast = (pDir2 != NULL && (pDir == NULL || pDir2 > pDir)) ? pDir2 : pDir;
			char* pTools = NULL;
			char* pPy = NULL;
			if ( pLast != NULL ) pExe[pLast - pExe] = 0;
			pTools = MdoPathJoin(pExe, "tools");
			snprintf(g_toolsDir, sizeof(g_toolsDir), "%s", pTools ? pTools : "tools");
			xrtFree(pTools);
			if ( xrtDirExists((str)g_toolsDir) ) {
				str sOld = xrtEnvGet("PATH");
				char* pGitCmd = MdoPathJoin(g_toolsDir, "git\\cmd");
				char* pPy = MdoPathJoin(g_toolsDir, "python313\\python.exe");
				char* pPyDir = NULL;
				char* pJoined = NULL;
				size_t iLen;
				/* python313 存在判定须先于 PATH 组装（目录本身入 PATH） */
				g_bPythonBundled = (pPy != NULL && xrtFileExists((str)pPy));
				if ( g_bPythonBundled ) {
					char* pSlash = strrchr(pPy, '\\');
					if ( pSlash != NULL ) {
						*pSlash = 0;
						pPyDir = MdoStrDupN(pPy, strlen(pPy));
						*pSlash = '\\';
					}
				}
				iLen = strlen(g_toolsDir) + (pGitCmd ? strlen(pGitCmd) : 0)
					+ (pPyDir ? strlen(pPyDir) : 0) + (sOld ? strlen(sOld) : 0) + 24u;
				pJoined = (char*)malloc(iLen);
				if ( pJoined != NULL ) {
					snprintf(pJoined, iLen, "%s;%s;%s;%s", g_toolsDir, pPyDir ? pPyDir : "",
						pGitCmd ? pGitCmd : "", sOld ? sOld : "");
					xrtEnvSet("PATH", pJoined);
					free(pJoined);
				}
				xrtFree(pPyDir);
				xrtFree(pGitCmd);
				xrtFree(pPy);
			}
			xrtFree(pExe);
		}
	}

	/* 数据根：MDO_HOME 覆盖 > 程序目录/data（xrtPathExecutable；
	 * 失败回退 xsAppPath）> 家目录兜底 */
	sTmp = xrtEnvGet("MDO_HOME");
	if ( sTmp != NULL && sTmp[0] ) {
		snprintf(g_mdoHome, sizeof(g_mdoHome), "%s", sTmp);
		xrtFree(sTmp);
	} else {
		xrtFree(sTmp);
		sTmp = xrtPathExecutable();
		if ( sTmp != NULL && sTmp[0] ) {
			char* pSlash = strrchr(sTmp, '\\');
			char* pSlash2 = strrchr(sTmp, '/');
			char* pLast = (pSlash2 != NULL && (pSlash == NULL || pSlash2 > pSlash))
				? pSlash2 : pSlash;
			if ( pLast != NULL ) {
				*pLast = 0;
				pJoin = MdoPathJoin(sTmp, "data");
			}
		}
		xrtFree(sTmp);
		if ( pJoin != NULL ) {
			snprintf(g_mdoHome, sizeof(g_mdoHome), "%s", pJoin);
			xrtFree(pJoin);
			pJoin = NULL;
		} else if ( xsAppPath() != NULL && xsAppPath()[0] ) {
			pJoin = MdoPathJoin(xsAppPath(), "data");
			snprintf(g_mdoHome, sizeof(g_mdoHome), "%s", pJoin ? pJoin : "data");
			xrtFree(pJoin);
		}
	}
	if ( g_mdoHome[0] == 0 ) {
		/* 兜底：家目录（老布局） */
		str sUser = xrtPathHome();
		pJoin = sUser ? xrtPathJoin(sUser, ".mdo") : NULL;
		snprintf(g_mdoHome, sizeof(g_mdoHome), "%s", pJoin ? pJoin : ".mdo");
		xrtFree(pJoin);
		xrtFree(sUser);
	}

	/* 一次性迁移（在建目录之前：目标必须不存在，xrtDirCopy 才是干净的整树复制）：
	 * 旧 ~/.mdo 存在且新根无 config.json → 复制（旧目录原样保留作备份；
	 * 成功后新根有 config.json，不会重迁） */
	{
		str sUser = xrtPathHome();
		if ( sUser != NULL && sUser[0] ) {
			char aOld[300];
			char* pOld = xrtPathJoin(sUser, ".mdo");
			snprintf(aOld, sizeof(aOld), "%s", pOld ? pOld : "");
			xrtFree(pOld);
			if ( aOld[0] && strcmp(aOld, g_mdoHome) != 0 && xrtDirExists(aOld) ) {
				char aNewCfg[340], aOldCfg[340];
				char* pCfg = MdoPathJoin(g_mdoHome, "config.json");
				char* pOldCfg = MdoPathJoin(aOld, "config.json");
				snprintf(aNewCfg, sizeof(aNewCfg), "%s", pCfg ? pCfg : "");
				snprintf(aOldCfg, sizeof(aOldCfg), "%s", pOldCfg ? pOldCfg : "");
				xrtFree(pCfg);
				xrtFree(pOldCfg);
				if ( aNewCfg[0] && !xrtFileExists(aNewCfg) &&
				     xrtFileExists(aOldCfg) ) {
					if ( xrtDirCopy(aOld, g_mdoHome, false) ) {
						printf("[mdo] migrated legacy data: %s -> %s (old kept as backup)\n",
							aOld, g_mdoHome);
					} else {
						printf("[mdo] legacy migration FAILED (keep new root): %s\n", aOld);
					}
				}
			}
		}
		xrtFree(sUser);
	}

	xrtDirCreateAll(g_mdoHome);

	pJoin = MdoPathJoin(g_mdoHome, "config.json");
	snprintf(g_cfgPath, sizeof(g_cfgPath), "%s", pJoin ? pJoin : "config.json");
	xrtFree(pJoin);
	pJoin = MdoPathJoin(g_mdoHome, "projects");
	snprintf(g_projectsDir, sizeof(g_projectsDir), "%s", pJoin ? pJoin : "projects");
	xrtFree(pJoin);
	xrtDirCreateAll(g_projectsDir);
	{   /* 「任务」默认分类的会话桶（不属于任何项目的会话归宿） */
		char* pSlugDir = MdoPathJoin(g_projectsDir, MDO_TASKS_SLUG);
		char* pSess = pSlugDir ? MdoPathJoin(pSlugDir, "sessions") : NULL;
		if ( pSess != NULL ) { xrtDirCreateAll(pSess); xrtFree(pSess); }
		xrtFree(pSlugDir);
	}

	xrtMutexLock(g_lock);
	MdoConfigLoadLocked();
	MdoProjectsScanLocked();
	MdoSettingsNormalize();
	xrtMutexUnlock(g_lock);
	MdoApplyPreventSleep(g_settings.bPreventSleep);

	/* 记忆目录：全局 + 各已注册项目（幂等种子 MEMORY.md；须在扫描后） */
	{
		char aMem[300];
		size_t iM;
		MdoMemoryDir(NULL, false, aMem, sizeof(aMem));
		MdoMemoryEnsure(aMem);
		for ( iM = 0; iM < g_nProjects; iM++ ) {
			MdoMemoryDir(&g_projects[iM], true, aMem, sizeof(aMem));
			MdoMemoryEnsure(aMem);
		}
	}
}

static void MdoStoreUnit(void)
{
	size_t i;
	for ( i = 0; i < g_nSessions; i++ ) {
		MdoSessionCloseHandles(g_sessions[i]);
		xrtFree(g_sessions[i]);
	}
	g_nSessions = 0;
	for ( i = 0; i < g_nModels; i++ ) MdoModelDropClient(&g_models[i]);
}

#endif /* MDO_STORE_H */


static void MdoSessionsScanLocked(void)
{
	MdoSessionsScanProjectLocked(NULL);
}
