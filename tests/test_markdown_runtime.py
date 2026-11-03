"""Bounded native md4c ABI, structure, Unicode and budget regressions."""
import argparse,json
from pathlib import Path
from markdown_runtime_support import ROOT,markdown_host,walk

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--packed',type=Path,required=True);args=parser.parse_args()
    source='''##### 标题

[**链接**](https://example.invalid/a_(b) "标题") 与 [引用][ref] https://example.invalid/

![中文图片](./中文%20图片.png "说明")

- 一级
  - [x] 二级

a | b
--- | :---:
1 | 2

~~~javascript
const a = "<script>";
~~~

first  
second &amp; third

<script>alert(1)</script>

[ref]: https://example.invalid/ref
'''
    with markdown_host(args.packed) as (port,call,home):
        status,body=call('markdown.parse',{'source':source});assert status==200,body
        data=body['data'];assert data['engine']=='md4c' and data['schema_version']==1
        nodes=list(walk(data['nodes']));types={n['t'] for n in nodes}
        assert {'heading','a','img','ul','li','table','th','td','codeblock','br','entity'}<=types,types
        assert any(n.get('url')=='https://example.invalid/a_(b)' for n in nodes)
        assert any(n.get('url')=='https://example.invalid/ref' for n in nodes)
        assert any(n.get('task')=='checked' for n in nodes)
        assert not any(n['t'] in ('html','script') for n in nodes)
        assert any('<script>' in n.get('s','') for n in nodes)
        for text,expected in (('x'*(512*1024+1),413),('x\0y',400),('> '*110+'too deep',413),('*x* '*17000,413)):
            status,body=call('markdown.parse',{'source':text});assert status==expected,(status,str(body)[:400])
        status,body=call('markdown.parse',{'source':''});assert status==200 and body['data']['nodes']==[]
        proof=ROOT/'.build/md4c-20261104';proof.mkdir(exist_ok=True)
        (proof/'native-ast.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
        print('PASS native md4c ABI, links/images/lists/tables/code/entities/Unicode and bounded failures')

if __name__=='__main__':main()
