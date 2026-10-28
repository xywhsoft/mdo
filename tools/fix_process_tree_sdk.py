"""Repair owned pipe/process-tree cleanup in the one locked SDK export."""
import argparse
from pathlib import Path
import build_mdo
from add_process_ownership_sdk import refresh_patch
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--base-repository',type=Path,required=True);a=p.parse_args();sdk=a.sdk.resolve();build_mdo.verify_dependencies(sdk,build_mdo.load_object(ROOT/'deps.lock'))
 header=sdk/'lib/xrt.h';text=header.read_text(encoding='utf-8');before=text
 def replace(old,new):
  nonlocal text
  if text.count(old)!=1:raise ValueError('SDK source changed: expected exactly one tree-cleanup anchor')
  text=text.replace(old,new,1)
 replace('\t\tpid_t Id;\n\t\tint Stdin;', '\t\tpid_t Id;\n\t\tbool ExitObserved; /* WNOWAIT pins this PID until final tree cleanup. */\n\t\tint Stdin;')
 replace('if ( pProcess->State == XPROCESS_EXITED ) {\n\t\t(void)xrtMutexUnlock(&pProcess->Lock);\n\t\treturn true;\n\t}', 'if ( pProcess->State == XPROCESS_EXITED && Stop != XPROCESS_STOP_KILL_TREE ) {\n\t\t(void)xrtMutexUnlock(&pProcess->Lock);\n\t\treturn true;\n\t}')
 old='''\tint iStatus;
\tpid_t iResult;

\tdo {
\t\tiResult = waitpid(pProcess->Id, &iStatus, 0);
\t} while ( (iResult < 0) && (errno == EINTR) );'''
 new='''\tint iStatus = 0;
\tint iResult;
\tsiginfo_t Info;
\tmemset(&Info, 0, sizeof(Info));
\t/* Observe without reaping. A completed owned handle must keep its PID
\t * reserved, otherwise a later killpg could hit an unrelated reused ID. */
\tdo {
\t\tiResult = waitid(P_PID, (id_t)pProcess->Id, &Info, WEXITED | WNOWAIT);
\t} while ( (iResult < 0) && (errno == EINTR) );
\tif (iResult == 0) {
\t\tpProcess->ExitObserved = true;
\t\tiStatus = Info.si_code == CLD_EXITED ? Info.si_status << 8 :
\t\t\t(Info.si_status & 0x7f) | (Info.si_code == CLD_DUMPED ? 0x80 : 0);
\t}'''
 replace(old,new)
 replace('''void __xrtProcessPlatformUnit(xprocess* pProcess)
{
\t__xrtProcessFdClose(&pProcess->Stdin);''','''void __xrtProcessPlatformUnit(xprocess* pProcess)
{
\tif (pProcess->ExitObserved && pProcess->Id > 0) {
\t\t/* No waiter or user remains; the zombie still pins the process group. */
\t\tif (pProcess->NewGroup) (void)kill(-pProcess->Id, SIGKILL);
\t\twhile (waitpid(pProcess->Id, NULL, 0) < 0 && errno == EINTR) {}
\t\tpProcess->ExitObserved = false;
\t}
\t__xrtProcessFdClose(&pProcess->Stdin);''')
 replace('''\tif ( !__xrtProcessRunThreadsJoin(&State) ) {
\t\t__xrtProcessRunFail(&State, xrtTakeError());
\t}
\tif ( !xrtProcessStatus(pProcess, &pResult->Status) )''','''\t/* A leader may exit with descendants still holding its output pipes.
\t * This run owns a group: stop it before joining the pipe pumps. */
\tif (Config.NewGroup && !xrtProcessKillTree(pProcess))
\t\t__xrtProcessRunFail(&State, xrtTakeError());
\tif ( !__xrtProcessRunThreadsJoin(&State) ) {
\t\t__xrtProcessRunFail(&State, xrtTakeError());
\t}
\tif ( !xrtProcessStatus(pProcess, &pResult->Status) )''')
 work=sdk/'lib/xwork/src/xwork_tools.c';source=work.read_text(encoding='utf-8');old='''    } else if ( pEntry->pProcess ) {
        if ( xwork__process_running(pEntry->pProcess) ) {
            (void)xrtProcessKillTree(pEntry->pProcess);''';new='''    } else if ( pEntry->pProcess ) {
        /* Completed leaders can leave descendants holding capture pipes. */
        (void)xrtProcessKillTree(pEntry->pProcess);
        if ( xwork__process_running(pEntry->pProcess) ) {'''
 if source.count(old)!=1:raise ValueError('SDK managed-process cleanup anchor changed')
 source=source.replace(old,new,1).replace('''fail:
    if ( pCapture->tStdout.pThread ) {''','''fail:
    (void)xrtProcessKillTree(pProcess);
    if ( pCapture->tStdout.pThread ) {''',1)
 backup=ROOT/'.build/process-owner-sdk-before/lib/xrt.h';backup.parent.mkdir(parents=True,exist_ok=True)
 if backup.exists():raise ValueError('Keep the recorded XRT source backup; do not apply this transform twice')
 backup.write_text(before,encoding='utf-8',newline='\n');header.write_text(text,encoding='utf-8',newline='\n');work.write_text(source,encoding='utf-8',newline='\n');refresh_patch(sdk,a.base_repository)
if __name__=='__main__':main()
