/* Test-only bridge to the Python protocol fixture. Never a shipped engine. */
#define wmain unused_supervisor_entry
#include "../tools/model_supervisor.c"
#undef wmain
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
 wchar_t *python=_wgetenv(L"MDO_QA_MODEL_PYTHON"),*script=_wgetenv(L"MDO_QA_MODEL_SCRIPT");if(!python||!script)return 125;
 wchar_t* line=calloc(32768,sizeof(wchar_t)),*out=line;if(!line)return 125;out=quote(out,python);*out++=' ';out=quote(out,script);
 for(int k=1;k<argc;k++){*out++=' ';out=quote(out,argv[k]);}STARTUPINFOW start={0};PROCESS_INFORMATION process={0};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;start.hStdInput=GetStdHandle(STD_INPUT_HANDLE);start.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);start.hStdError=GetStdHandle(STD_ERROR_HANDLE);
 BOOL ok=CreateProcessW(python,line,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&start,&process);free(line);if(!ok)return 125;WaitForSingleObject(process.hProcess,INFINITE);DWORD code=125;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);CloseHandle(process.hThread);return (int)code;
}
#endif
