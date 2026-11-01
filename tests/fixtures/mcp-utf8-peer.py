"""MCP strings deliberately cross compact 512/4096-byte UTF-8 boundaries."""
import json,sys
for line in sys.stdin:
 request=json.loads(line);method=request.get('method');ident=request.get('id')
 if ident is None:continue
 if method=='initialize':result={'protocolVersion':'2025-11-25','capabilities':{'tools':{}},'serverInfo':{'name':'utf8-peer','version':'1'},'instructions':'a'*4095+'中文'}
 elif method=='tools/list':result={'tools':[{'name':name,'description':'a'*offset+tail,'inputSchema':{'type':'object','properties':{},'additionalProperties':False}} for name,offset,tail in [('two',511,'é'),('three',511,'中'),('four',510,'😀')]]}
 else:result={}
 print(json.dumps({'jsonrpc':'2.0','id':ident,'result':result},ensure_ascii=True),flush=True)

