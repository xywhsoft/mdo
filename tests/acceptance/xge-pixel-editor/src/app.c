#include "editor.h"
#include "paths.h"
#include "xge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 340
#define WIN_H 480
#define CANVAS_X 16
#define CANVAS_Y 16
#define SWATCH_SIZE 28
#define PALETTE_Y 310

typedef struct app_t {
	editor_t tEditor;
	int iZoom; /* pixels per canvas pixel: 4..9 (32*9=288 fits width) */
	int iLastPixelX, iLastPixelY;
	int bHasLast;
	/* bounded smoke mode */
	int iSmokeFrames;
	int iFrame;
	char sSmokePath[300];
	unsigned char* pSmokePixels;
} app_t;

static const int s_iZoomLevels[] = { 4, 6, 8, 9 };

static xge_font_t s_tFont;
static int s_bFontOk = 0;

static void appDrawText(const char* sText, float fX, float fY, uint32_t iColor)
{
	if ( s_bFontOk ) {
		xgeTextDraw(&s_tFont, sText, fX, fY, iColor);
	}
}

static int appPixelFromMouse(app_t* pApp, float fX, float fY, int* pPX, int* pPY)
{
	int iSize = CANVAS_SIZE * pApp->iZoom;
	int iX = (int)fX - CANVAS_X;
	int iY = (int)fY - CANVAS_Y;
	if ( (iX < 0) || (iY < 0) || (iX >= iSize) || (iY >= iSize) ) {
		return 0;
	}
	*pPX = iX / pApp->iZoom;
	*pPY = iY / pApp->iZoom;
	return 1;
}

static void appHandleDrag(app_t* pApp, int iPX, int iPY, int bRightButton)
{
	int bErase = editorButtonErases(&pApp->tEditor, bRightButton);
	if ( pApp->bHasLast ) {
		editorStrokeEx(&pApp->tEditor, pApp->iLastPixelX, pApp->iLastPixelY, iPX, iPY, bErase);
	} else {
		editorStrokeEx(&pApp->tEditor, iPX, iPY, iPX, iPY, bErase);
	}
	pApp->bHasLast = 1;
	pApp->iLastPixelX = iPX;
	pApp->iLastPixelY = iPY;
}

static void appInput(app_t* pApp)
{
	editor_t* pEd = &pApp->tEditor;
	float fMX, fMY;
	int i, iPX, iPY;

	if ( xgeKeyPressed(XGE_KEY_ESCAPE) ) {
		xgeQuit();
		return;
	}
	if ( xgeKeyPressed('E') ) {
		pEd->iTool = (pEd->iTool == TOOL_PENCIL) ? TOOL_ERASER : TOOL_PENCIL;
	}
	if ( xgeKeyPressed('C') ) {
		editorClear(pEd);
	}
	if ( xgeKeyPressed('S') ) {
		char sPath[300];
		if ( (pathNextToExe(sPath, sizeof(sPath), "pixel_art.png") == 0) &&
			(editorSavePNG(pEd, sPath) == XGE_OK) ) {
			printf("saved: %s\n", sPath);
		} else {
			printf("save failed\n");
		}
	}
	if ( xgeKeyPressed(XGE_KEY_UP) ) {
		for ( i = 0; i < 3; i++ ) {
			if ( s_iZoomLevels[i] == pApp->iZoom ) { pApp->iZoom = s_iZoomLevels[i + 1]; break; }
		}
		pApp->bHasLast = 0;
	}
	if ( xgeKeyPressed(XGE_KEY_DOWN) ) {
		for ( i = 3; i > 0; i-- ) {
			if ( s_iZoomLevels[i] == pApp->iZoom ) { pApp->iZoom = s_iZoomLevels[i - 1]; break; }
		}
		pApp->bHasLast = 0;
	}
	for ( i = 0; i < PALETTE_COUNT; i++ ) {
		if ( xgeKeyPressed('1' + i) ) {
			pEd->iColorIndex = i;
		}
	}

	xgeMouseGet(&fMX, &fMY);
	if ( (fMY >= PALETTE_Y) && (fMY < PALETTE_Y + SWATCH_SIZE) ) {
		/* palette click: select color (right-click never erases swatches) */
		if ( (fMX >= CANVAS_X) && (fMX < CANVAS_X + PALETTE_COUNT * SWATCH_SIZE) && xgeMouseDown(XGE_MOUSE_LEFT) ) {
			pEd->iColorIndex = (int)(fMX - CANVAS_X) / SWATCH_SIZE;
		}
		pApp->bHasLast = 0;
	} else if ( appPixelFromMouse(pApp, fMX, fMY, &iPX, &iPY) ) {
		if ( xgeMouseDown(XGE_MOUSE_LEFT) ) {
			appHandleDrag(pApp, iPX, iPY, 0);
		} else if ( xgeMouseDown(XGE_MOUSE_RIGHT) ) {
			/* temporary erase; selected tool unchanged */
			appHandleDrag(pApp, iPX, iPY, 1);
		} else {
			pApp->bHasLast = 0;
		}
	} else {
		pApp->bHasLast = 0;
	}
}

static void appRender(app_t* pApp)
{
	editor_t* pEd = &pApp->tEditor;
	xge_rect_i_t tRect;
	const uint32_t* pPal = editorPalette();
	int i, iPX, iPY;

	xgeClear(XGE_COLOR_RGBA(24, 26, 32, 255));
	tRect.iX = CANVAS_X - 1;
	tRect.iY = CANVAS_Y - 1;
	tRect.iW = CANVAS_SIZE * pApp->iZoom + 2;
	tRect.iH = CANVAS_SIZE * pApp->iZoom + 2;
	xgeShapeRectFillPixels(tRect, XGE_COLOR_RGBA(90, 96, 110, 255));
	for ( iPY = 0; iPY < CANVAS_SIZE; iPY++ ) {
		for ( iPX = 0; iPX < CANVAS_SIZE; iPX++ ) {
			tRect.iX = CANVAS_X + iPX * pApp->iZoom;
			tRect.iY = CANVAS_Y + iPY * pApp->iZoom;
			tRect.iW = pApp->iZoom;
			tRect.iH = pApp->iZoom;
			xgeShapeRectFillPixels(tRect, editorGetPixel(pEd, iPX, iPY));
		}
	}
	for ( i = 0; i < PALETTE_COUNT; i++ ) {
		tRect.iX = CANVAS_X + i * SWATCH_SIZE;
		tRect.iY = PALETTE_Y;
		tRect.iW = SWATCH_SIZE - 2;
		tRect.iH = SWATCH_SIZE - 2;
		xgeShapeRectFillPixels(tRect, pPal[i]);
		if ( i == pEd->iColorIndex ) {
			xge_rect_t tSel;
			tSel.fX = (float)(CANVAS_X + i * SWATCH_SIZE - 2);
			tSel.fY = (float)(PALETTE_Y - 2);
			tSel.fW = (float)(SWATCH_SIZE + 2);
			tSel.fH = (float)(SWATCH_SIZE + 2);
			xgeShapeRectStroke(tSel, 2.0f, XGE_COLOR_RGBA(255, 255, 255, 255));
		}
	}

	appDrawText("LMB draw  RMB erase  1-8 color", 16, 360, XGE_COLOR_RGBA(220, 224, 232, 255));
	appDrawText("E tool  C clear  S save png", 16, 380, XGE_COLOR_RGBA(220, 224, 232, 255));
	appDrawText("UP/DOWN zoom  ESC quit", 16, 400, XGE_COLOR_RGBA(220, 224, 232, 255));
	appDrawText((pEd->iTool == TOOL_ERASER) ? "tool: ERASER" : "tool: PENCIL", 16, 432, XGE_COLOR_RGBA(120, 200, 255, 255));
}

/* Bounded smoke capture: save the window backbuffer and sanity-check a pixel. */
static void appSmokeCapture(app_t* pApp)
{
	xge_render_target_t tTarget;
	int iStride = WIN_W * 4;
	int iRet;

	memset(&tTarget, 0, sizeof(tTarget));
	iRet = xgeRenderTargetCreate(&tTarget, WIN_W, WIN_H);
	if ( iRet != XGE_OK ) {
		printf("smoke: render target create failed: %d\n", iRet);
		xgeQuit();
		return;
	}
	iRet = xgeRenderTargetCaptureCurrent(&tTarget, 0, 0);
	if ( iRet == XGE_OK ) {
		iRet = xgeRenderTargetReadPixels(&tTarget, pApp->pSmokePixels, iStride);
	}
	if ( iRet == XGE_OK ) {
		iRet = xgeImageSavePNG(pApp->sSmokePath, WIN_W, WIN_H, pApp->pSmokePixels, iStride);
	}
	xgeRenderTargetFree(&tTarget);
	if ( iRet == XGE_OK ) {
		printf("smoke: saved %s\n", pApp->sSmokePath);
	} else {
		printf("smoke: capture failed: %d\n", iRet);
	}
	xgeQuit();
}

static int appFrame(void* pUser)
{
	app_t* pApp = (app_t*)pUser;
	int iRet = xgeBegin();
	if ( iRet != XGE_OK ) {
		return iRet;
	}
	if ( pApp->iSmokeFrames <= 0 ) {
		appInput(pApp);
	} else if ( pApp->iFrame > 0 ) {
		/* deterministic smoke content: diagonal stripe */
		pApp->tEditor.iColorIndex = 1;
		editorStroke(&pApp->tEditor, pApp->iFrame, 0, 31 - pApp->iFrame, 31);
	}
	appRender(pApp);
	if ( (pApp->iSmokeFrames > 0) && (pApp->iFrame >= pApp->iSmokeFrames) ) {
		appSmokeCapture(pApp);
	}
	iRet = xgeEnd();
	if ( iRet != XGE_OK ) {
		return iRet;
	}
	pApp->iFrame++;
	return XGE_OK;
}

int appRun(int iSmokeFrames, const char* sSmokePath)
{
	app_t tApp;
	xge_desc_t tDesc;
	char sFont[300];
	int iRet;

	memset(&tApp, 0, sizeof(tApp));
	editorInit(&tApp.tEditor);
	tApp.iZoom = 8;
	tApp.iSmokeFrames = iSmokeFrames;
	if ( iSmokeFrames > 0 ) {
		if ( sSmokePath != NULL ) {
			snprintf(tApp.sSmokePath, sizeof(tApp.sSmokePath), "%s", sSmokePath);
		} else if ( pathNextToExe(tApp.sSmokePath, sizeof(tApp.sSmokePath), "smoke_window.png") != 0 ) {
			strcpy(tApp.sSmokePath, "smoke_window.png");
		}
		tApp.pSmokePixels = (unsigned char*)malloc((size_t)WIN_W * 4 * WIN_H);
		if ( tApp.pSmokePixels == NULL ) {
			printf("smoke: out of memory\n");
			return 1;
		}
	}

	memset(&tDesc, 0, sizeof(tDesc));
	tDesc.iWidth = WIN_W;
	tDesc.iHeight = WIN_H;
	tDesc.sTitle = "xge pixel editor";
	tDesc.iFlags = XGE_INIT_VSYNC;
	tDesc.iRunMode = XGE_RUN_GAME_LOOP;
	iRet = xgeInit(&tDesc);
	if ( iRet != XGE_OK ) {
		printf("xgeInit failed: %d\n", iRet);
		free(tApp.pSmokePixels);
		return 1;
	}
	if ( (pathNextToExe(sFont, sizeof(sFont), "res_font_KaTeX_Main-Regular.ttf") == 0) &&
		(xgeFontLoad(&s_tFont, sFont, 14.0f) == XGE_OK) ) {
		s_bFontOk = 1;
	} else {
		printf("warning: font load failed, text hidden\n");
	}
	iRet = xgeRun(appFrame, &tApp);
	if ( s_bFontOk ) {
		xgeFontFree(&s_tFont);
		s_bFontOk = 0;
	}
	free(tApp.pSmokePixels);
	xgeUnit();
	return (iRet == XGE_OK) ? 0 : 1;
}

int appRunInteractive(void)
{
	return appRun(0, NULL);
}
