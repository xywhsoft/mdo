#define XRT_MODULE_PROCESS_RUN
#define XRT_MODULE_PATH_SYSTEM
#define XRT_IMPLEMENTATION
#include <xrt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

int main(int argc,char** argv){
    if(argc>1&&!strcmp(argv[1],"grandchild")){xrtSleep(15000);return 0;}
    if(argc>1&&!strcmp(argv[1],"child")){
#if defined(_WIN32)
        char Cmd[4096];snprintf(Cmd,sizeof(Cmd),"\"%s\" grandchild",argv[0]);STARTUPINFOA S={0};S.cb=sizeof(S);S.dwFlags=STARTF_USESTDHANDLES;S.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);S.hStdError=GetStdHandle(STD_ERROR_HANDLE);S.hStdInput=GetStdHandle(STD_INPUT_HANDLE);PROCESS_INFORMATION P={0};if(!CreateProcessA(argv[0],Cmd,NULL,NULL,TRUE,0,NULL,NULL,&S,&P))return 3;CloseHandle(P.hProcess);CloseHandle(P.hThread);
#else
        pid_t Child=fork();if(Child<0)return 3;if(!Child){execl(argv[0],argv[0],"grandchild",NULL);_exit(4);}
#endif
        puts("leader done");fflush(stdout);return 0;
    }
    char* Self=xrtPathExecutable();const cstr Args[]={"child"};xprocessconfig C;xrtProcessConfigInit(&C);C.Program=Self;C.Args=Args;C.ArgCount=1;C.NewGroup=true;C.Stdout.Mode=XPROCESS_IO_PIPE;C.Stderr.Mode=XPROCESS_IO_MERGE;
    if(argc>1&&!strcmp(argv[1],"run")){
        xprocessrunoptions O;xrtProcessRunOptionsInit(&O);O.Timeout=2500;xprocessresult R={0};bool Ok=xrtProcessRun(&C,&O,&R);bool Passed=Ok&&R.Wait==XWAIT_OK&&R.Status.Code==0&&R.Duration<2500;printf("run=%d duration=%llu\n",Passed,(unsigned long long)R.Duration);xrtProcessResultUnit(&R);xrtFree(Self);return Passed?0:5;
    }
    xprocess* P=xrtProcessSpawn(&C);xrtFree(Self);if(!P)return 6;printf("root=%llu\n",(unsigned long long)xrtProcessId(P));fflush(stdout);
    bool Ok=xrtProcessWaitFor(P,2000)==XWAIT_OK;bool Pinned=true;
#if !defined(_WIN32)
    siginfo_t Info={0};Pinned=waitid(P_PID,(id_t)xrtProcessId(P),&Info,WEXITED|WNOWAIT|WNOHANG)==0&&Info.si_pid==(pid_t)xrtProcessId(P);
#endif
    Ok=Ok&&Pinned&&xrtProcessKillTree(P);char B[128];int64 N;while((N=xrtProcessRead(P,XPROCESS_STDOUT,B,sizeof(B)))>0){}Ok=Ok&&N==0;uint64 Id=xrtProcessId(P);xrtProcessDestroy(P);
#if !defined(_WIN32)
    int Status;errno=0;bool Reaped=waitpid((pid_t)Id,&Status,WNOHANG)<0&&errno==ECHILD;Ok=Ok&&Reaped;
#endif
    printf("tree=%d pinned=%d\n",Ok,Pinned);return Ok?0:7;
}
