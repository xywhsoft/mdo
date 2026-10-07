#include "editor.h"
#include "xge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t s_arrPalette[PALETTE_COUNT] = {
	XGE_COLOR_RGBA(0, 0, 0, 255),
	XGE_COLOR_RGBA(230, 57, 70, 255),
	XGE_COLOR_RGBA(255, 159, 28, 255),
	XGE_COLOR_RGBA(244, 208, 63, 255),
	XGE_COLOR_RGBA(42, 157, 143, 255),
	XGE_COLOR_RGBA(48, 133, 214, 255),
	XGE_COLOR_RGBA(131, 56, 236, 255),
	XGE_COLOR_RGBA(255, 255, 255, 255)
};

const uint32_t* editorPalette(void)
{
	return s_arrPalette;
}

void editorInit(editor_t* pEditor)
{
	if ( pEditor == NULL ) {
		return;
	}
	memset(pEditor, 0, sizeof(*pEditor));
	editorClear(pEditor);
	pEditor->iTool = TOOL_PENCIL;
	pEditor->iColorIndex = 0;
}

void editorSetPixel(editor_t* pEditor, int iX, int iY, uint32_t iColorRGBA)
{
	if ( (pEditor == NULL) || (iX < 0) || (iY < 0) || (iX >= CANVAS_SIZE) || (iY >= CANVAS_SIZE) ) {
		return;
	}
	/* Engine color packing: R<<24 | G<<16 | B<<8 | A. */
	pEditor->arrPixels[iY][iX][0] = (uint8_t)(iColorRGBA >> 24);
	pEditor->arrPixels[iY][iX][1] = (uint8_t)(iColorRGBA >> 16);
	pEditor->arrPixels[iY][iX][2] = (uint8_t)(iColorRGBA >> 8);
	pEditor->arrPixels[iY][iX][3] = (uint8_t)iColorRGBA;
}

uint32_t editorGetPixel(const editor_t* pEditor, int iX, int iY)
{
	const uint8_t* p;
	if ( (pEditor == NULL) || (iX < 0) || (iY < 0) || (iX >= CANVAS_SIZE) || (iY >= CANVAS_SIZE) ) {
		return 0;
	}
	p = pEditor->arrPixels[iY][iX];
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void editorPaint(editor_t* pEditor, int iX, int iY)
{
	if ( pEditor == NULL ) {
		return;
	}
	editorSetPixel(pEditor, iX, iY, s_arrPalette[pEditor->iColorIndex]);
}

void editorErase(editor_t* pEditor, int iX, int iY)
{
	if ( pEditor == NULL ) {
		return;
	}
	editorSetPixel(pEditor, iX, iY, XGE_COLOR_RGBA(255, 255, 255, 255));
}

/* Simple interpolation stroke so dragging paints a continuous line. */
int editorButtonErases(const editor_t* pEditor, int bRightButton)
{
	if ( pEditor == NULL ) {
		return 0;
	}
	return (bRightButton || (pEditor->iTool == TOOL_ERASER)) ? 1 : 0;
}

void editorStrokeEx(editor_t* pEditor, int iX0, int iY0, int iX1, int iY1, int bErase)
{
	int iDx, iDy, iSteps, i, iX, iY;
	if ( pEditor == NULL ) {
		return;
	}
	iDx = iX1 - iX0;
	iDy = iY1 - iY0;
	iSteps = (abs(iDx) > abs(iDy)) ? abs(iDx) : abs(iDy);
	if ( iSteps == 0 ) {
		iSteps = 1;
	}
	for ( i = 0; i <= iSteps; i++ ) {
		iX = iX0 + (iDx * i) / iSteps;
		iY = iY0 + (iDy * i) / iSteps;
		if ( bErase ) {
			editorErase(pEditor, iX, iY);
		} else {
			editorPaint(pEditor, iX, iY);
		}
	}
}

void editorStroke(editor_t* pEditor, int iX0, int iY0, int iX1, int iY1)
{
	if ( pEditor == NULL ) {
		return;
	}
	editorStrokeEx(pEditor, iX0, iY0, iX1, iY1, editorButtonErases(pEditor, 0));
}

void editorClear(editor_t* pEditor)
{
	int iX, iY;
	if ( pEditor == NULL ) {
		return;
	}
	for ( iY = 0; iY < CANVAS_SIZE; iY++ ) {
		for ( iX = 0; iX < CANVAS_SIZE; iX++ ) {
			editorSetPixel(pEditor, iX, iY, XGE_COLOR_RGBA(255, 255, 255, 255));
		}
	}
}

int editorIsClear(const editor_t* pEditor)
{
	int iX, iY;
	if ( pEditor == NULL ) {
		return 0;
	}
	for ( iY = 0; iY < CANVAS_SIZE; iY++ ) {
		for ( iX = 0; iX < CANVAS_SIZE; iX++ ) {
			if ( editorGetPixel(pEditor, iX, iY) != XGE_COLOR_RGBA(255, 255, 255, 255) ) {
				return 0;
			}
		}
	}
	return 1;
}

int editorSavePNG(const editor_t* pEditor, const char* sPath)
{
	if ( (pEditor == NULL) || (sPath == NULL) ) {
		return -1;
	}
	return xgeImageSavePNG(sPath, CANVAS_SIZE, CANVAS_SIZE, pEditor->arrPixels, CANVAS_SIZE * 4);
}
