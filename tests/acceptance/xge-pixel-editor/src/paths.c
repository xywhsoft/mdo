#include "paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

int pathNextToExe(char* sOut, int iSize, const char* sName)
{
	char sExe[MAX_PATH];
	int iLen, i;
	DWORD dw = GetModuleFileNameA(NULL, sExe, sizeof(sExe));
	if ( (dw == 0) || (dw >= sizeof(sExe)) ) {
		return -1;
	}
	/* strip file name, keep trailing separator */
	iLen = (int)dw;
	for ( i = iLen - 1; i > 0; i-- ) {
		if ( (sExe[i] == '\\') || (sExe[i] == '/') ) {
			break;
		}
	}
	sExe[i + 1] = '\0';
	if ( (int)strlen(sExe) + (int)strlen(sName) + 1 > iSize ) {
		return -1;
	}
	strcpy(sOut, sExe);
	strcat(sOut, sName);
	return 0;
}
