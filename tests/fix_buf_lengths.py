# fix_buf_lengths.py — 校验/修复 main.c 中 MdoBufAppend 字面量硬编码长度
import re

PATH = r'D:\GIT\xserver\release\mdo\app\main.c'
src = open(PATH, encoding='utf-8').read()

BS = chr(92)  # backslash

def c_unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == BS and i + 1 < len(s):
            n = s[i + 1]
            out.append({'n': chr(10), 't': chr(9), 'r': chr(13), '"': '"', BS: BS}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)

pat = re.compile(r'MdoBufAppend\(&(tOut|tBuf), ("(?:[^"\\]|\\.)*"),\s*(\d+)\)')
bad = 0

def rep(m):
    global bad
    bufvar, lit, n = m.group(1), m.group(2), int(m.group(3))
    real = len(c_unescape(lit[1:-1]).encode('utf-8'))
    if n != real:
        bad += 1
        print(f'{bufvar} {lit}: {n} -> {real}')
        return f'MdoBufAppend(&{bufvar}, {lit}, {real})'
    return m.group(0)

src = pat.sub(rep, src)

# done 尾巴三目：],"done":true} = 14 / ],"done":false} = 15
old_tail = 'bDone ? 15 : 16);'
new_tail = 'bDone ? 14 : 15);'
if old_tail in src:
    src = src.replace(old_tail, new_tail)
    print('tail fixed')
else:
    t = re.search(r'bDone \? \d+ : \d+\);', src)
    print('tail now:', t.group(0) if t else 'NOT FOUND')

open(PATH, 'w', encoding='utf-8').write(src)
print('bad literals fixed:', bad)
