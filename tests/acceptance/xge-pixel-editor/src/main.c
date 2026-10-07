#include "editor.h"
#include "paths.h"
#include "xge.h"

#include <stdio.h>
#include <string.h>

int appRunInteractive(void);
int appRun(int iSmokeFrames, const char* sSmokePath);

/* Bounded self-test: paint, erase, clear, tool semantics, sample export; no window. */
static int selfTest(void)
{
	editor_t tEd;
	char sPath[300];
	int i, iFail = 0;
	const uint32_t* pPal = editorPalette();
	const uint32_t uWhite = XGE_COLOR_RGBA(255, 255, 255, 255);

	editorInit(&tEd);
	if ( !editorIsClear(&tEd) ) { printf("FAIL: initial canvas not clear\n"); iFail++; }

	/* paint a red diagonal */
	tEd.iColorIndex = 1;
	for ( i = 0; i < CANVAS_SIZE; i++ ) {
		editorPaint(&tEd, i, i);
	}
	for ( i = 0; i < CANVAS_SIZE; i++ ) {
		if ( editorGetPixel(&tEd, i, i) != pPal[1] ) { printf("FAIL: paint at %d,%d\n", i, i); iFail++; break; }
	}
	if ( editorGetPixel(&tEd, 5, 6) == pPal[1] ) { printf("FAIL: paint leaked off diagonal\n"); iFail++; }

	/* erase one painted pixel */
	editorErase(&tEd, 0, 0);
	if ( editorGetPixel(&tEd, 0, 0) != uWhite ) { printf("FAIL: erase\n"); iFail++; }

	/* tool semantics: eraser selected -> left click erases */
	tEd.iTool = TOOL_ERASER;
	editorStroke(&tEd, 3, 3, 3, 3);
	if ( editorGetPixel(&tEd, 3, 3) != uWhite ) { printf("FAIL: left click with eraser tool did not erase\n"); iFail++; }
	/* tool semantics: pencil selected, right button erases temporarily */
	tEd.iTool = TOOL_PENCIL;
	tEd.iColorIndex = 1;
	editorStrokeEx(&tEd, 4, 4, 4, 4, editorButtonErases(&tEd, 1));
	if ( editorGetPixel(&tEd, 4, 4) != uWhite ) { printf("FAIL: right button did not erase\n"); iFail++; }
	if ( tEd.iTool != TOOL_PENCIL ) { printf("FAIL: right button changed selected tool\n"); iFail++; }
	/* tool semantics: pencil selected, left button paints palette color */
	editorStrokeEx(&tEd, 5, 5, 5, 5, editorButtonErases(&tEd, 0));
	if ( editorGetPixel(&tEd, 5, 5) != pPal[1] ) { printf("FAIL: left button with pencil did not paint\n"); iFail++; }

	/* export sample next to the executable */
	if ( pathNextToExe(sPath, sizeof(sPath), "selftest_sample.png") != 0 ) {
		snprintf(sPath, sizeof(sPath), "selftest_sample.png");
	}
	if ( editorSavePNG(&tEd, sPath) != XGE_OK ) { printf("FAIL: save png\n"); iFail++; }
	else { printf("saved: %s\n", sPath); }

	/* clear */
	editorClear(&tEd);
	if ( !editorIsClear(&tEd) ) { printf("FAIL: clear\n"); iFail++; }

	if ( iFail == 0 ) {
		printf("self-test OK (paint/erase/clear/tools/save)\n");
		return 0;
	}
	printf("self-test FAILED: %d error(s)\n", iFail);
	return 1;
}

int main(int argc, char** argv)
{
	if ( (argc > 1) && (strcmp(argv[1], "--self-test") == 0) ) {
		return selfTest();
	}
	if ( (argc > 1) && (strcmp(argv[1], "--smoke") == 0) ) {
		return appRun(5, NULL); /* render 5 frames in a real window, capture, exit */
	}
	return appRunInteractive();
}
