# sync_proxy_src.py — 同步 xrt/xllm 代理改动到 xserver vendored 副本
import shutil, os

# 1. xs 侧 xllm-xrt.h 加代理模块
p = r'D:\GIT\xserver\lib\xllm\xllm-xrt.h'
src = open(p, encoding='utf-8').read()
old = '#define XRT_MODULE_NET_TCP_DIAL_FUTURE'
if 'XRT_MODULE_NET_PROXY_DIAL' not in src:
    src = src.replace(old, old + '\n#define XRT_MODULE_NET_PROXY_DIAL', 1)
    open(p, 'w', encoding='utf-8', newline='').write(src)
    print('xs xllm-xrt + proxy')

# 2. 同步源文件
pairs = [
    (r'D:\GIT\xrt\single\xrt.h', r'D:\GIT\xserver\lib\xrt.h'),
    (r'D:\GIT\xrt\extlibs\xllm\xllm.h', r'D:\GIT\xserver\lib\xllm\xllm.h'),
    (r'D:\GIT\xrt\extlibs\xllm\src\xllm_internal.h', r'D:\GIT\xserver\lib\xllm\src\xllm_internal.h'),
    (r'D:\GIT\xrt\extlibs\xllm\src\xllm_client.c', r'D:\GIT\xserver\lib\xllm\src\xllm_client.c'),
    (r'D:\GIT\xrt\extlibs\xllm\src\xllm_transport.c', r'D:\GIT\xserver\lib\xllm\src\xllm_transport.c'),
]
for s, d in pairs:
    shutil.copyfile(s, d)
    print('synced', os.path.basename(d))
