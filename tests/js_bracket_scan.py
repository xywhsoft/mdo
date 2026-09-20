# js_bracket_scan.py — 跳过字符串/模板/注释的括号栈扫描，定位 JS 语法不平衡点
import sys

BS = chr(92)

def scan(path):
    src = open(path, encoding='utf-8').read()
    stack = []
    line = 1
    i = 0
    n = len(src)
    state = 'code'
    pairs = {')': '(', ']': '[', '}': '{'}
    while i < n:
        c = src[i]
        if c == chr(10):
            line += 1
            if state == 'line_comment':
                state = 'code'
            i += 1
            continue
        if state == 'code':
            two = src[i:i+2]
            if two == '//':
                state = 'line_comment'; i += 2; continue
            if two == '/*':
                state = 'block_comment'; i += 2; continue
            if c in ("'", '"'):
                state = 'str'; i += 1; continue
            if c == '`':
                state = 'tpl'; i += 1; continue
            if c in '([{':
                stack.append((c, line))
            elif c in ')]}':
                if not stack:
                    print(f'{path}: line {line}: 关闭 {c} 无对应开括号')
                    return
                op, oline = stack.pop()
                if op != pairs[c]:
                    print(f'{path}: line {line}: {c} 与 line {oline} 的 {op} 不匹配')
                    return
        elif state in ('str', 'tpl'):
            if c == BS:
                i += 2
                continue
            if state == 'str' and c in ("'", '"'):
                state = 'code'
            elif state == 'tpl' and c == '`':
                state = 'code'
        elif state == 'block_comment':
            if src[i:i+2] == '*/':
                state = 'code'; i += 2; continue
        i += 1
    if stack:
        print(f'{path}: 未闭合（就近 3 个）:', stack[-3:])
    else:
        print(f'{path}: 括号平衡')

for p in sys.argv[1:]:
    scan(p)
