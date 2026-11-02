"""Read-only native regression for damaged/oversized conversation projections.

Compiles the actual parser, cursor and view code in place. User journals, when
provided, are opened XFILE_READ and never copied, repaired, loaded as sessions,
or passed to a model. Only small synthetic fixtures live in the owned test dir.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def native_source(paths):
    events = (ROOT/'app/src/sessions/events.c').read_text(encoding='utf-8')
    header = (ROOT/'app/include/mdo/sessions.h').read_text(encoding='utf-8')
    def part(source, first, after):
        start = source.index(first)
        return source[start:source.index(after, start)]
    source = '''#include <xsbase.h>
#include <xwork.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MDO_SESSION_PATH_CAPACITY 2048u
#define MDO_PROJECT_ID_CAPACITY 65u
#define MDO_SESSION_ID_CAPACITY 33u
#define MDO_SESSION_IDENTITY_CAPACITY 129u
#define MDO_SESSION_EVENT_HISTORY_TRUNCATED ((xwork_event_kind)(XWORK_EVENT_RECOVERY_RESOLVED+1))
#define MDO_AGENT_EVENT_MODEL_RETRY ((xwork_event_kind)(XWORK_EVENT_RECOVERY_RESOLVED+2))
#define MDO_SESSION_EVENT_SCHEMA 9u
#define MDO_SESSION_EVENT_RECORD_LIMIT (96u*1024u)
#define MDO_SESSION_EVENT_TEXT_LIMIT (64u*1024u)
#define MDO_SESSION_EVENT_METADATA_LIMIT 4096u
typedef struct MdoSessionEventSnapshot MdoSessionEventSnapshot;
'''
    source += part(header, 'typedef struct MdoSessionEventSource {', 'typedef struct MdoSessionEventSink {')
    source += part(header, 'typedef struct MdoSessionEventInfo {', '/* Product bootstrap')
    source += part(header, '/* Read projection diagnostics only.', '/* One anchored journal view.')
    source += part(events, 'typedef struct MdoSessionEventOwned', 'struct MdoSessionEventBridge')
    source += part(events, 'static void MdoEventsError(', 'static void MdoEventsXrtError(')
    source += part(events, 'static bool MdoEventsIdValid(', 'static bool MdoEventsPath(')
    source += 'static const char* Names[]={' + ','.join(json.dumps(name) for name in paths) + '};\n'
    source += 'static const char* Paths[]={' + ','.join(json.dumps(str(path.resolve()).replace('\\', '/')) for path in paths.values()) + '};\n'
    source += '''static bool MdoEventsPath(char* Out,const char* Project,const char* Session){
 if(strcmp(Project,"numcheck"))return false;
 for(size_t I=0;I<sizeof(Names)/sizeof(*Names);I++)if(!strcmp(Names[I],Session)){snprintf(Out,MDO_SESSION_PATH_CAPACITY,"%s",Paths[I]);return true;}
 return false;
}
xfile MdoHomeOpenRead(cstr P){return xrtOpen(P,XFILE_READ);}
bool MdoHomeExternalStat(cstr P,bool* Exists,xfileinfo* Info){xfile F=xrtOpen(P,XFILE_READ);*Exists=F!=NULL;bool Ok=F&&xrtFileStat(F,Info);if(F)xrtClose(F);return Ok;}
'''
    source += part(events, 'static bool MdoEventsValueUInt(', 'bool MdoSessionsInternalEventVisit(')
    source += part(events, 'static bool MdoEventsSnapshotGrow(', '#include "conversation_page.inc.c"')
    source += part(events, 'void MdoSessionEventSnapshotRelease(', 'size_t MdoSessionEventSnapshotCount(')
    source += (ROOT/'app/src/sessions/event_reader.inc.c').read_text(encoding='utf-8')
    source += (ROOT/'app/src/sessions/conversation_page.inc.c').read_text(encoding='utf-8')
    source += r'''
static unsigned Failed;
static void Check(bool Ok,const char* Why){if(!Ok){Failed++;printf("FAIL %s\n",Why);}}
void ServiceInit(XS_HostInfo* Host){
 for(size_t N=0;N<sizeof(Names)/sizeof(*Names);N++){
  uint64 Before=0,Offset=0;bool HasOffset=false,Done=false;char Epoch[65]="";
  size_t Pages=0,Readable=0,Notices=0,Texts=0;bool FirstEmpty=false;
  for(;Pages<64;Pages++){
   MdoConversationPageInfo Page;xwork_error Error;
   MdoSessionEventSnapshot* S=MdoSessionConversationReplyPageAt("numcheck",Names[N],Before,0,Epoch,1,HasOffset,Offset,&Page,&Error);
   if(!S){printf("FAIL read %s %s\n",Names[N],Error.sMessage);Failed++;break;}
   if(!Pages)FirstEmpty=S->Count==0;
   Check(Page.NoticeCount<=MDO_CONVERSATION_NOTICE_LIMIT,"bounded_notices");
   uint64 Previous=0;
   for(size_t I=0;I<S->Count;I++){
    Check(S->Events[I].Info.EventId>Previous,"ordered_unique_events");Previous=S->Events[I].Info.EventId;
    if(S->Events[I].Info.Kind==XWORK_EVENT_MODEL_TEXT_DELTA){Texts++;Readable+=strlen(S->Events[I].Text);}
   }
   Notices+=Page.NoticeCount;
   printf("PAGE %s %u events=%u notices=%u older=%llu offset=%llu more=%d\n",Names[N],(unsigned)Pages,(unsigned)S->Count,(unsigned)Page.NoticeCount,(unsigned long long)Page.NextBefore,(unsigned long long)Page.NextScanBefore,Page.HasMore);fflush(stdout);
   if(HasOffset)Check(!Page.HasMore||Page.NextScanBefore<Offset,"byte_cursor_advances");
   HasOffset=Page.HasScanBefore;Offset=Page.NextScanBefore;Before=Page.NextBefore;memcpy(Epoch,Page.Epoch,sizeof(Epoch));
   Done=!Page.HasMore;MdoSessionEventSnapshotRelease(S);
   if(Done)break;
  }
  Check(Done,"reaches_beginning");Check(Notices>0,"visible_local_notices");
  if(strcmp(Names[N],"all-bad"))Check(Texts>0,"healthy_text_is_retained");
  if(!strcmp(Names[N],"bad-suffix"))Check(FirstEmpty,"empty_bad_page_can_continue");
  if(!strcmp(Names[N],"long-text"))Check(Readable==1200000u,"split_reply_text_preserved");
  printf("CASE_DONE %s pages=%u texts=%u notices=%u\n",Names[N],(unsigned)(Pages+1),(unsigned)Texts,(unsigned)Notices);fflush(stdout);
 }
 printf("faults_failed=%u\nprobe_done=1\n",Failed);fflush(stdout);
}
'''
    return source


def record(session, ident, kind, text=''):
    return dict(schema_version=6, event_id=ident, source_event_id=ident, occurred_at_us=ident*1000000,
        project_id='numcheck', session_id=session, kind=kind, agent_turn=1,
        user_message_sequence=1 if kind == 0 else 0, agent_depth=0, agent_id=1, run_id=1,
        queue_item_id='', task_id=0, artifact_id=0, parent_run_id=0, effects=0, task_state=0,
        task_revision=0, input_tokens=0, output_tokens=0, total_tokens=0, success=True,
        effect_applied=False, text_truncated=False, text=text, tool_name='', tool_call_id='',
        artifact_path='', model='fixture', model_id='fixture', context_window_tokens=128000)


def line(value):
    return json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode() + b'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT/'.build/host/xs.exe')
    parser.add_argument('--journals', type=Path, nargs='*', default=[])
    args = parser.parse_args()
    evidence = ROOT/'.build/acceptance/conversation-faults-20261102'
    evidence.mkdir(parents=True, exist_ok=True)
    hashes = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in args.journals}
    with tempfile.TemporaryDirectory(prefix='history-faults-', dir=ROOT/'.build') as directory:
        base = Path(directory); assert base.resolve().is_relative_to((ROOT/'.build').resolve())
        paths = {}
        for name in ['bad-middle', 'bad-suffix', 'all-bad', 'oversized-record', 'long-text']:
            path = base/(name+'.jsonl'); paths[name] = path
            if name == 'bad-middle':
                path.write_bytes(line(record(name, 1, 0, 'question')) + line(record(name, 2, 2, 'healthy before')) +
                    b'{broken record\n' + line(record(name, 3, 2, 'healthy after')) +
                    line(record(name, 999, 2, 'bad order')) + line(record(name, 4, 10)))
            elif name == 'bad-suffix':
                with path.open('wb') as out:
                    out.write(line(record(name, 1, 0, 'question')) + line(record(name, 2, 2, 'healthy prefix')))
                    for _ in range(18*1024):out.write(b'{' + b' ' * 1022 + b'\n')
            elif name == 'all-bad':path.write_bytes(b'{broken\nnot json\n')
            elif name == 'oversized-record':
                path.write_bytes(line(record(name, 1, 0, 'question')) + line(record(name, 2, 2, 'healthy before')) +
                    line(record(name, 3, 2, 'x'*200000)) + line(record(name, 4, 2, 'healthy after')) +
                    line(record(name, 5, 10)))
            else:
                with path.open('wb') as out:
                    out.write(line(record(name, 1, 0, 'question')))
                    for n in range(20):out.write(line(record(name, n+2, 2, 'x'*60000)))
                    out.write(line(record(name, 22, 4)) + line(record(name, 23, 10)))
        for path in args.journals:paths[path.parent.name] = path
        (base/'probe.c').write_text(native_source(paths), encoding='utf-8')
        with socket.socket() as sock:sock.bind(('127.0.0.1', 0));port=sock.getsockname()[1]
        (base/'xs.json').write_text(json.dumps({'engine': {'workers': 1}, 'services': [{'class': 'http', 'enabled': True,
            'name': 'history-fault-probe', 'ip': '127.0.0.1', 'port': port,
            'host_default': {'enabled': True, 'path': str(base), 'devlang': 'c', 'devfile': str(base/'probe.c')}}]}), encoding='utf-8')
        process = None
        try:
            with (evidence/'native.log').open('wb') as log:
                process = subprocess.Popen([str(args.host.resolve()), str(base/'xs.json')], cwd=base,
                    stdout=log, stderr=log, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                deadline = time.monotonic()+180
                while time.monotonic()<deadline and process.poll() is None:
                    if 'probe_done=1' in (evidence/'native.log').read_text(encoding='utf-8', errors='replace'):break
                    time.sleep(.1)
            text = (evidence/'native.log').read_text(encoding='utf-8', errors='replace')
            assert 'probe_done=1' in text and 'faults_failed=0' in text, text[-10000:]
            for path, digest in hashes.items():assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
            print(text[text.find('PAGE '):text.find('probe_done=1')+12])
            print('PASS native fault isolation, monotonic byte pagination, long replies and read-only original journals')
        finally:
            if process and process.poll() is None:process.terminate();process.wait(timeout=8)


if __name__ == '__main__':main()
