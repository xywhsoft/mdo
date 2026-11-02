/* Browser fallback is armed by window failure and dispatched only after Ready.
 * Both the UI thread and Ready may observe the failure first; claim it once. */
#ifndef XS_BROWSER_FALLBACK_WIN_H
#define XS_BROWSER_FALLBACK_WIN_H

static void app_browser_open(XS_AppWindow* pWin)
{
	WCHAR sUrl[1024], sMessage[1280];
	HRESULT iCom;
	INT_PTR iResult = 0;

	if (g_XS_Stop || InterlockedCompareExchange(&pWin->iNavState, 0, 0) != XS_APP_NAV_URL)
		return;
	if (InterlockedCompareExchange(&pWin->iBrowserFallback, 2, 1) != 1)
		return;
	printf("[xs] app '%s': browser/service mode -> %s\n", pWin->pServer->Name, pWin->sTargetUrl);
	fflush(stdout);
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pWin->sTargetUrl,
		-1, sUrl, sizeof sUrl / sizeof sUrl[0]) > 0) {
		iCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		iResult = (INT_PTR)ShellExecuteW(NULL, L"open", sUrl, NULL, NULL, SW_SHOWNORMAL);
		if (SUCCEEDED(iCom)) CoUninitialize();
	}
	else {
		wcscpy(sUrl, L"(invalid page URL)");
	}
	if (iResult <= 32) {
		printf("[xs] app '%s': cannot open browser (%lld); open %s manually\n",
			pWin->pServer->Name, (long long)iResult, pWin->sTargetUrl);
		fflush(stdout);
		int iChinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
		_snwprintf(sMessage, sizeof sMessage / sizeof sMessage[0] - 1,
			iChinese ? L"本地服务已启动，但无法自动打开浏览器，请手动访问：\n\n%ls\n\n关闭此提示不会停止服务。" :
			L"The local service is running. Open this address in a browser:\n\n%ls\n\n"
			L"Closing this message will keep the service running.", sUrl);
		sMessage[sizeof sMessage / sizeof sMessage[0] - 1] = 0;
		MessageBoxW(NULL, sMessage, iChinese ? L"xs - 浏览器访问" : L"xs - Browser fallback", MB_OK | MB_ICONINFORMATION);
	}
}

static void app_browser_fallback(XS_AppWindow* pWin, const char* sReason)
{
	if (InterlockedCompareExchange(&pWin->iBrowserFallback, 1, 0) != 0)
		return; /* Explicit headless, or already scheduled/opened. */
	InterlockedExchange(&g_iBrowserService, 1);
	printf("[xs] app '%s': %s; using browser/service mode when ready\n",
		pWin->pServer->Name, sReason);
	app_browser_open(pWin);
}

#endif
