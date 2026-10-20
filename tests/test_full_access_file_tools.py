"""Native file scope and deterministic read recovery using disposable files."""
import argparse
import json
import os
import sys
from pathlib import Path
import tempfile

from test_memory_runtime import run_probe

ROOT = Path(__file__).resolve().parent.parent

SOURCE = r'''
#include <stdio.h>
#include <string.h>
#include <xsbase.h>
#include <xwork.h>
#include <xllm-time.h>
static unsigned failed;
static char resource[2048];
static xwork_result Fault(void *data, const xwork_tool_context *context,
    const char *arguments, xwork_tool_output *output, xwork_error *error) {
    char large[512];
    (void)data; (void)context;
    if (strstr(arguments, "invalid-output")) {
        xworkToolOutputSet(output, true, "\xff"); return XWORK_RESULT_OK;
    }
    if (strstr(arguments, "oversize")) {
        memset(large, 'x', sizeof(large)-1); large[sizeof(large)-1]=0;
        xworkToolOutputSet(output, true, large); return XWORK_RESULT_OK;
    }
    xworkErrorInit(error);
    error->eCode = strstr(arguments, "oom") ? XWORK_ERROR_OUT_OF_MEMORY :
        strstr(arguments, "context") ? XWORK_ERROR_CONTEXT :
        strstr(arguments, "cancelled") ? XWORK_ERROR_CANCELLED :
        strstr(arguments, "uncertain") ? XWORK_ERROR_RECOVERY_REQUIRED :
        strstr(arguments, "timeout") ? XWORK_ERROR_TIMEOUT :
        strstr(arguments, "invalid") ? XWORK_ERROR_INVALID_ARGUMENT : XWORK_ERROR_IO;
    snprintf(error->sMessage, sizeof(error->sMessage), "injected recoverable tool failure");
    return XWORK_RESULT_ERROR;
}
static xwork_permission_decision Policy(void *data, const xwork_permission_request *request) {
    size_t i;
    (void)data;
    resource[0] = 0;
    for (i = 0; i < request->iResourceCount; ++i) {
        snprintf(resource, sizeof(resource), "%s", request->pResources[i].sResource);
        if (strstr(resource, "deny-topic.md")) return XWORK_PERMISSION_DENY;
    }
    return XWORK_PERMISSION_DEFAULT;
}
static void Check(xwork_agent *agent, const char *label, const char *tool,
    const char *json, bool success, const char *text, const char *effective) {
    xllm_executor executor = {0};
    xllm_executor_ctx context = {0};
    xllm_executor_result result = {0};
    xllm_tool_call call = {0};
    xwork_error error;
    bool ok;
    context.uRound = 1;
    context.uDeadline = xllmDeadlineAfterUs(5000000u);
    call.sId = (char*)label; call.sName = (char*)tool;
    call.sArgumentsJson = (char*)json;
    ok = xworkExecutorBind(&executor, agent, &error) &&
        executor.pExecute(executor.pUserData, &call, &context, &result);
    ok = ok && result.bSuccess == success &&
        (!text || (result.sContent && strstr(result.sContent, text))) &&
        (!effective || !strcmp(resource, effective));
    printf("case=%s %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) {
        ++failed;
        printf("failure=%s infra_result:%d reason:%s resource:%s\n", label,
            result.bSuccess ? 1 : 0, result.sContent ? result.sContent : error.sMessage, resource);
    }
    xworkExecutorUnbind(&executor);
}
static void CheckFatal(xwork_agent *agent, const char *mode) {
    xllm_executor executor = {0}; xllm_executor_ctx context = {0};
    xllm_executor_result result = {0}; xllm_tool_call call = {0};
    xwork_error error; char json[80]; bool ok;
    snprintf(json, sizeof(json), "{\"mode\":\"%s\"}", mode);
    call.sId=(char*)"fatal-probe"; call.sName=(char*)"fault"; call.sArgumentsJson=json;
    context.uRound=1u; context.uDeadline=xllmDeadlineAfterUs(5000000u);
    ok=xworkExecutorBind(&executor,agent,&error) &&
        !executor.pExecute(executor.pUserData,&call,&context,&result);
    printf("fatal=%s %s\n",mode,ok?"PASS":"FAIL");
    if (!ok) ++failed;
    xworkExecutorUnbind(&executor);
}
void ServiceInit(XS_HostInfo *host) {
    xllm_session_config llm;
    xwork_agent_config config;
    xwork_error error;
    xwork_agent *full = NULL, *confined = NULL, *readonly = NULL;
    xllm_session *sessions[3] = {0};
    size_t i;
    (void)host;
    xllmSessionConfigInit(&llm);
    for (i = 0; i < 3; ++i) sessions[i] = xllmSessionCreate(&llm, NULL);
    xworkAgentConfigInit(&config);
    config.sWorkspaceRoot = ".";
    config.eApprovalMode = XWORK_APPROVAL_AUTO;
    config.bRegisterBuiltinTools = true;
    config.OnPermission = Policy;
    config.pSession = sessions[0]; full = xworkAgentCreate(&config, &error);
    config.pSession = sessions[1]; confined = xworkAgentCreate(&config, &error);
    config.pSession = sessions[2]; config.eApprovalMode = XWORK_APPROVAL_READ_ONLY;
    readonly = xworkAgentCreate(&config, &error);
    if (!full || !confined || !readonly ||
        !xworkAgentAllowExternalPaths(full, true, &error) ||
        !xworkAgentMountDirectory(full, "memory-global", "memory-global", &error) ||
        !xworkAgentMountDirectory(full, "memory-project", "memory-project", &error) ||
        !xworkAgentMountDirectory(confined, "memory-global", "memory-global", &error)) {
        printf("init_error=%s\n", error.sMessage); goto done;
    }
    {
        xwork_tool_definition fault = {0};
        fault.sName = "fault"; fault.sDescription = "Disposable fault injection";
        fault.sParametersJson = "{\"type\":\"object\"}";
        fault.uEffects = XWORK_TOOL_EFFECT_READ;
        fault.OnExecute = Fault; fault.iMaxResultBytes = 64u;
        if (!xworkAgentRegisterTool(full, &fault, &error)) {
            printf("init_error=%s\n", error.sMessage); goto done;
        }
    }
    CASES
    CheckFatal(full, "oom"); CheckFatal(full, "context");
    CheckFatal(full, "cancelled"); CheckFatal(full, "uncertain");
    if (xworkAgentAllowExternalPaths(full, false, &error) || error.eCode != XWORK_ERROR_CONTEXT) {
        printf("sealed_scope=FAIL\n"); ++failed;
    } else printf("sealed_scope=PASS\n");
done:
    xworkAgentDestroy(readonly); xworkAgentDestroy(confined); xworkAgentDestroy(full);
    for (i = 0; i < 3; ++i) xllmSessionDestroy(sessions[i]);
    printf("cases_failed=%u\nprobe_done=1\n", failed);
}
void ServiceUnit(XS_HostInfo *host) { (void)host; }
'''


def run(host):
    with tempfile.TemporaryDirectory(prefix='file-scope-', dir=ROOT / '.build') as raw, \
         tempfile.TemporaryDirectory(prefix='mdo-external-read-') as external_raw:
        base, external = Path(raw), Path(external_raw)
        site = base / 'site'
        (site / 'web').mkdir(parents=True)
        (site / 'web/index.html').write_text('probe')
        for scope in ('memory-global', 'memory-project'):
            (site / scope).mkdir()
            (site / scope / 'MEMORY.md').write_text('# ' + scope)
            (site / scope / 'shared.md').write_text(scope)
        for name in ('topic.md', 'deny-topic.md'):
            (site / 'memory-global' / name).write_text('MEMORY_TOPIC_CONTENT\n')
        (site / 'note.txt').write_text('NOTE_CONTENT\n', encoding='utf-8')
        (site / 'utf8.txt').write_text('慕晴🙂文本\nEND\n', encoding='utf-8')
        (site / 'invalid-encoding.txt').write_bytes(b'\xffBAD')
        (site / 'binary.dat').write_bytes(b'\0BINARY')
        (base / 'sibling.txt').write_text('RELATIVE_EXTERNAL_CONTENT\n', encoding='utf-8')
        (site / "'literal'").write_text('EXACT_FILENAME_CONTENT\n', encoding='utf-8')
        novel = external / '慕晴.txt'
        novel.write_text('UNICODE_EXTERNAL_CONTENT 慕晴\n', encoding='utf-8')
        with (external / 'large.txt').open('wb') as stream:
            stream.write(b'LARGE_PAGE_CONTENT\n' * 1000)
            stream.truncate(65 * 1024 * 1024)
        checks = []

        def check(label, tool, args, success=True, text=None, agent='full', effective=None):
            values = [agent, json.dumps(label), json.dumps(tool),
                      json.dumps(json.dumps(args)), 'true' if success else 'false',
                      json.dumps(text) if text else 'NULL',
                      json.dumps(effective) if effective else 'NULL']
            checks.append('    Check(' + ', '.join(values) + ');')

        check('cross-drive-native-read', 'read', {'path': str(novel)}, text='UNICODE_EXTERNAL_CONTENT')
        check('relative-external-read', 'read', {'path': '../sibling.txt'}, text='RELATIVE_EXTERNAL_CONTENT')
        check('confined-relative-read', 'read', {'path': '../sibling.txt'}, False, 'permission_denied', 'confined')
        actual_novel = Path('G:/wen/慕晴.txt')
        if actual_novel.is_file():
            check('reported-novel-read', 'read', {'path': str(actual_novel)}, text='file:')
            for offset, effective in ((745,744),(218000,217998)):
                check('reported-novel-cursor-' + str(offset), 'read',
                      {'path': str(actual_novel), 'offset': offset}, text='effective_offset=' + str(effective))
        source_session = Path('E:/software/mdo/mdo-home/sessions/default/fd429b2783cee59e05418afe2225c74d')
        for relative, offset, effective in [
            ('artifacts/run-00000000000000000001/00000000000000000001-exec.txt',2500,2499),
            ('artifacts/run-00000000000000000010/00000000000000000026-exec.txt',3620,3619)]:
            original = source_session / relative
            if original.is_file():
                check('reported-artifact-cursor-' + str(offset), 'read',
                      {'path': str(original), 'offset': offset}, text='effective_offset=' + str(effective))
        check('confined-external-read', 'read', {'path': str(novel)}, False, 'permission_denied', 'confined')
        check('readonly-external-read', 'read', {'path': str(novel)}, False, 'permission_denied', 'readonly')
        for tool, args in [('ls', {}), ('glob', {'pattern': '*.txt'}),
                           ('grep', {'pattern': 'UNICODE_EXTERNAL_CONTENT'})]:
            args['path'] = str(external)
            check('external-' + tool, tool, args, text='慕晴.txt')
            check('confined-' + tool, tool, args, False, agent='confined')
        target = str(external / 'write.txt')
        check('external-write', 'write', {'path': target, 'content': 'BEFORE_CONTENT'})
        check('external-edit', 'edit', {'path': target, 'edits': [{'old_text': 'BEFORE', 'new_text': 'AFTER'}]})
        check('external-write-verified', 'read', {'path': target}, text='AFTER_CONTENT')
        check('external-process-cwd', 'exec', {'cwd': str(external),
            'argv': [sys.executable, '-c', 'from pathlib import Path; assert Path("write.txt").read_text() == "AFTER_CONTENT"; print("EXTERNAL_CWD_OK")'],
            'timeout_ms': 5000}, text='EXTERNAL_CWD_OK')
        check('confined-process-cwd', 'exec', {'cwd': str(external),
            'argv': [sys.executable, '-c', 'raise AssertionError("must not start")'],
            'timeout_ms': 5000}, False, agent='confined')
        check('readonly-write-denied', 'write', {'path': 'readonly.txt', 'content': 'Denied'}, False, agent='readonly')
        for label, path in [('quote', ' "note.txt" '), ('backtick', '`note.txt`'),
                            ('xml', 'note.txt\n</parameter atlas="#">\n Atlas #>\n'),
                            ('xml-mounted', '@memory-global/topic.md\n</parameter>')]:
            check('recover-' + label, 'read', {'path': path}, text='read_recovery:',
                  effective='@memory-global/topic.md' if label == 'xml-mounted' else 'note.txt')
        check('exact-quoted-name', 'read', {'path': "'literal'"}, text='EXACT_FILENAME_CONTENT', effective="'literal'")
        check('unique-memory-topic', 'read', {'path': 'topic.md'}, text='MEMORY_TOPIC_CONTENT', effective='@memory-global/topic.md')
        check('legacy-memory-topic', 'read', {'path': str(base / 'mdo-home/data/memory/topic.md')},
              text='MEMORY_TOPIC_CONTENT', effective='@memory-global/topic.md')
        check('unique-memory-confined', 'read', {'path': 'topic.md'}, text='read_recovery:', agent='confined')
        check('unique-index-confined', 'read', {'path': 'MEMORY.md'}, text='read_recovery:', agent='confined')
        check('ambiguous-memory-topic', 'read', {'path': 'shared.md'}, False, 'not_found')
        check('ambiguous-index', 'read', {'path': 'MEMORY.md'}, False, 'not_found')
        check('corrected-resource-policy-denial', 'read', {'path': 'deny-topic.md'}, False,
              agent='full', effective='@memory-global/deny-topic.md')
        check('missing-remains-missing', 'read', {'path': 'missing.txt'}, False, 'not_found')
        check('nul-rejected', 'read', {'path': 'note.txt\0suffix'}, False, 'embedded NUL')
        check('mount-traversal-denied', 'read', {'path': '@memory-global/../note.txt'}, False, 'permission_denied')
        check('unknown-alias', 'read', {'path': '@unknown/note.txt'}, False, 'invalid_argument')
        check('invalid-numeric-type', 'read', {'path': 'note.txt', 'max_bytes': '8192'}, False, 'read needs')
        check('clamp-page-options', 'read', {'path': 'note.txt', 'start_line': 0, 'max_lines': 999999, 'max_bytes': 999999}, text='NOTE_CONTENT')
        check('small-page-budget', 'read', {'path': 'note.txt', 'max_bytes': 0}, text='NOTE_CONTENT')
        check('offset-precedence', 'read', {'path': 'note.txt', 'start_line': 10, 'offset': 0}, text='NOTE_CONTENT')
        check('stale-offset-eof', 'read', {'path': 'note.txt', 'offset': 999999}, text='eof=true')
        check('large-text-paged', 'read', {'path': str(external / 'large.txt')}, text='LARGE_PAGE_CONTENT')
        for offset, effective in [(1,0),(2,0),(4,3),(5,3),(7,6),(8,6),(9,6)]:
            check('align-utf8-' + str(offset), 'read', {'path': 'utf8.txt', 'offset': offset},
                  text='effective_offset=' + str(effective))
        check('binary-is-distinct', 'read', {'path': 'binary.dat'}, False, 'reason=binary_data')
        check('bad-encoding-is-distinct', 'read', {'path': 'invalid-encoding.txt'}, False, 'reason=invalid_encoding')
        for fault in ('io', 'timeout', 'invalid', 'invalid-output', 'oversize'):
            check('recoverable-executor-' + fault, 'fault', {'mode': fault}, False, 'tool_failure:')
        (site / 'probe.c').write_text(SOURCE.replace('    CASES', '\n'.join(checks)), encoding='utf-8')
        import socket
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        (site / 'xs.json').write_text(json.dumps({'services': [{'enabled': True,
            'class': 'http', 'name': 'probe', 'ip': '127.0.0.1', 'port': port,
            'host_default': {'enabled': True, 'name': 'probe', 'path': 'web',
                             'devlang': 'c', 'devfile': 'probe.c'}}]}))
        output = run_probe(host, site, base / 'home')
        assert 'init_error=' not in output and 'cases_failed=0' in output, output
        assert output.count(' PASS') == len(checks) + 4 and 'sealed_scope=PASS' in output, output
        assert (external / 'write.txt').read_text() == 'AFTER_CONTENT'
        assert novel.read_text(encoding='utf-8') == 'UNICODE_EXTERNAL_CONTENT 慕晴\n'
        print(output[output.find('case='):].strip())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, required=True)
    run(parser.parse_args().host.resolve())
