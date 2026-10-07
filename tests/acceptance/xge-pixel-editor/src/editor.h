#ifndef PIXEL_EDITOR_H
#define PIXEL_EDITOR_H

#include <stdint.h>

#define CANVAS_SIZE 32

/* Tool identifiers shared by interactive and self-test modes. */
#define TOOL_PENCIL 0
#define TOOL_ERASER 1

/* Palette selectable by mouse click or keys 1-8 (XGE_COLOR packing). */
#define PALETTE_COUNT 8

typedef struct editor_t {
	uint8_t arrPixels[CANVAS_SIZE][CANVAS_SIZE][4]; /* straight RGBA bytes */
	int iTool;
	int iColorIndex;
} editor_t;

/* Shared editor logic; no engine or window dependency except PNG export. */
void editorInit(editor_t* pEditor);
void editorSetPixel(editor_t* pEditor, int iX, int iY, uint32_t iColorRGBA);
uint32_t editorGetPixel(const editor_t* pEditor, int iX, int iY);
void editorPaint(editor_t* pEditor, int iX, int iY);
void editorErase(editor_t* pEditor, int iX, int iY);
void editorStroke(editor_t* pEditor, int iX0, int iY0, int iX1, int iY1);
void editorStrokeEx(editor_t* pEditor, int iX0, int iY0, int iX1, int iY1, int bErase);
/* Shared pointer-button semantics: right button erases temporarily (does not
 * change the selected tool); left button uses the selected tool. */
int editorButtonErases(const editor_t* pEditor, int bRightButton);
void editorClear(editor_t* pEditor);
int editorIsClear(const editor_t* pEditor);
int editorSavePNG(const editor_t* pEditor, const char* sPath);
const uint32_t* editorPalette(void);

#endif
