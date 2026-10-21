"""Exercise Skill parameters in the real catalog/renderer using the native host."""
import argparse
import json
from pathlib import Path
import tempfile
import test_skill_runtime as fixture

ROOT = Path(__file__).resolve().parents[1]

# Names refer to positions, not KEY=value parsing. The input is never evaluated.
CASES = [
    ('$ARGUMENTS', '  "a b" $1 ${MDO_SKILL_DIR}  ', '  "a b" $1 ${MDO_SKILL_DIR}  '),
    ('$0|$1|$ARGUMENTS[2]', 'first "second value" third', 'first|second value|third'),
    ('$file|$focus', '"C:\\new folder\\file.txt" "memory leaks"', 'C:\\new folder\\file.txt|memory leaks'),
    ('$file|$focus|$2', 'one', 'one||$2'),
    ('$0|$1|$2', '\'\' "" third', '||third'),
    ('$0|$1', 'pre"two words"post tail', 'pretwo wordspost|tail'),
    ('$0|$1', '"unfinished words', 'unfinished words|$1'),
    ('$0|$1', 'a\\ b\tsecond', 'a b|second'),
    ('$0|$1', "'a\\b' C:\\new\\file", 'a\\b|C:\\new\\file'),
    ('$0', "'\\\\server\\share\\file'", '\\\\server\\share\\file'),
    ('$0', '"a\\"b"', 'a"b'),
    ('$0', '"$ARGUMENTS $(do-not-run) `literal`"', '$ARGUMENTS $(do-not-run) `literal`'),
    ('$0|$ARGUMENTS[1]|$file', '"中文 路径" 第二项', '中文 路径|第二项|中文 路径'),
    ('$10|$ARGUMENTS[10]', '0 1 2 3 4 5 6 7 8 9 ten', 'ten|ten'),
    ('$9999999999999999999999999999', 'one', '$9999999999999999999999999999\n\nARGUMENTS: one'),
    ('$ARGUMENTS[x] $ARGUMENTS[0', 'one', '$ARGUMENTS[x] $ARGUMENTS[0\n\nARGUMENTS: one'),
    ('$ARGUMENTSsuffix $filename $unknown', 'one', '$ARGUMENTSsuffix $filename $unknown\n\nARGUMENTS: one'),
    ('\\$0 \\$file \\$ARGUMENTS $1', 'first second', '$0 $file $ARGUMENTS second'),
    ('\\\\$0', 'first', '\\\\first'),
    ('plain instructions', 'keep "all raw"', 'plain instructions\n\nARGUMENTS: keep "all raw"'),
    ('plain instructions', '', 'plain instructions'),
    ('$file|$focus|$0', '', '||$0'),
    ('${MDO_SKILL_DIR}|${CLAUDE_SKILL_DIR}|${ZCODE_SKILL_DIR}|$0', '"$focus"', '/skill dir|/skill dir|/skill dir|$focus'),
    ('$0|$1|$2', 'first\r\nsecond\nthird', 'first|second|third'),
]

VALID_DECLARATIONS = [
    'arguments: [file, focus]', 'arguments: ["file", "focus"]',
    "arguments: ['file', 'focus']", 'arguments: file focus',
    'arguments:\n  - file\n  - focus',
]
INVALID_DECLARATIONS = [
    'arguments: [file, file]', 'arguments: [ARGUMENTS]', 'arguments: [0file]',
    'arguments: [file-name]', 'arguments: [""]', 'arguments: [file,]',
    'arguments: {file: required}', 'arguments: [1]', 'arguments: ["file\\u0000x"]',
    'arguments: [' + 'x' * 65 + ']',
    'arguments: [' + ', '.join(f'a{i}' for i in range(129)) + ']',
]


def literal(value):
    return json.dumps(value, ensure_ascii=False)


def checks():
    code = ['{ const char* names[]={"file","focus"}; int failures=0;']
    for i, (text, raw, expected) in enumerate(CASES):
        code.append('''{char* out=MdoSkillRenderArguments(%s,"/skill dir",%s,names,2);
            if(!out||strcmp(out,%s)){printf("argument_case_%d_failed=%%s\\n",out?out:"NULL");failures++;}xrtFree(out);}'''
                    % (literal(text), literal(raw), literal(expected), i))
    for i, declaration in enumerate(VALID_DECLARATIONS + INVALID_DECLARATIONS):
        document = '---\nname: parameters\ndescription: Parameter fixture\n' + declaration + '\n---\n$file $focus'
        valid = i < len(VALID_DECLARATIONS)
        code.append('''{char error[1024]={0};bool ok=MdoSkillValidateText(%s,error,sizeof(error));
            if(ok!=%s%s){printf("declaration_case_%d_failed=%%s\\n",error);failures++;}}'''
                    % (literal(document), 'true' if valid else 'false', '' if valid else '||!strstr(error,"arguments")', i))
    code.append(r'''
        char* raw=xrtMalloc(16386);memset(raw,'x',16385);raw[16385]=0;
        char* out=MdoSkillRender("$ARGUMENTS","/dir",raw);
        if(out){failures++;xrtFree(out);}xrtClearError();raw[16384]=0;
        out=MdoSkillRender("$ARGUMENTS","/dir",raw);
        if(!out||strlen(out)!=16384)failures++;xrtFree(out);xrtFree(raw);
        char* text=xrtMalloc(2u*1024u*1024u+1);memset(text,'x',2u*1024u*1024u);text[2u*1024u*1024u]=0;
        out=MdoSkillRender(text,"/dir",NULL);if(out){failures++;xrtFree(out);}xrtClearError();
        text[2u*1024u*1024u-1]=0;out=MdoSkillRender(text,"/dir",NULL);
        if(!out||strlen(out)!=2u*1024u*1024u-1)failures++;xrtFree(out);xrtFree(text);
        raw=xrtMalloc(16385);memset(raw,' ',16384);raw[16384]=0;
        for(size_t i=0;i<16384;i+=2)raw[i]='x';
        out=MdoSkillRender("$8191","/dir",raw);if(!out||strcmp(out,"x"))failures++;
        xrtFree(out);xrtFree(raw);
        const char* doc="---\nname: parameters\ndescription: Fixture\narguments: [file, focus]\n---\n$file|$focus";
        MdoHomeAtomicWrite("skills/parameters/SKILL.md",doc,strlen(doc),false);MdoSkillManagerReload();
        MdoSkillCatalog* old=MdoSkillCatalogSnapshot();MdoSkillInfo info={0};info.Size=sizeof(info);
        if(!MdoSkillCatalogFind(old,"parameters",&info)||info.ArgumentCount!=2||strcmp(info.ArgumentNames[1],"focus"))failures++;
        MdoSkillContent body={0};body.Size=sizeof(body);
        bool loaded=MdoSkillCatalogLoadBody(old,"parameters",&body);
        MdoHomeRemove("skills/parameters",true);MdoSkillManagerReload();
        if(loaded) {
            out=MdoSkillRenderArguments(body.Text,"/dir","a b",info.ArgumentNames,info.ArgumentCount);
            if(!out||!strstr(out,"a|b"))failures++;xrtFree(out);MdoSkillContentUnit(&body);
        } else {failures++;xrtClearError();}
        if(info.ArgumentCount!=2||strcmp(info.ArgumentNames[0],"file"))failures++;
        MdoSkillCatalogRelease(old);
        printf("argument_failures=%d\n",failures);
    }
    ''')
    return '\n'.join(code)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    args = parser.parse_args()
    source = fixture.PROBE_SOURCE
    fixture.PROBE_SOURCE = source.replace('PrintFirst(builtin, "initial_skill");',
        'PrintFirst(builtin, "initial_skill");\n' + checks(), 1)
    try:
        with tempfile.TemporaryDirectory(prefix='skill-arguments-', dir=ROOT / '.build') as raw:
            base = Path(raw)
            fixture.write_site(base / 'site')
            output = fixture.run_probe(args.host.resolve(), base / 'site', base / 'home')
            assert 'argument_failures=0' in output and 'probe_done=1' in output, output
    finally:
        fixture.PROBE_SOURCE = source
    print(f'Skill argument runtime: PASS ({len(CASES)} expansion cases, {len(VALID_DECLARATIONS) + len(INVALID_DECLARATIONS)} declaration cases, bounds and catalog lifetime)')


if __name__ == '__main__':
    main()
