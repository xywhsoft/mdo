# Linux 图片队列 HEAD 连接重置（待排查）

2026-10-02 的完整有界门禁出现一次 `ConnectionResetError: [Errno 104]`。
捕获的是客户端连接重置，没有记录当时的进程退出码，不能判断是否伴随
进程退出，也尚无证据证明根因在新增图片解码器或 xrt。
状态保持待排查，后续重跑通过不等于根因已修复。

环境：Ubuntu WSL、xs `0c8c533b7704a6f9ca614ad22f39eff761d92ddd`、
xrt `6040abda647bd947bd561cd185f37a3751f122f4`，当前 mdo 图片检查增量。
源与宿主在 `/home/ubuntu/.cache/mdo-linux-images-uuzgn5_e`；首次相关失败
日志保存为 `.build/qa-image-linux-second-failure.log`。

失败在 `tests/test_image_download_runtime.py` 的 queued Unit 子用例：

1. 暂停图片 worker 于读取前，建立五个客户端连接。
2. 前四个发送 GET，已观察到四个 worker 进入暂停。
3. 第五个连接发送 HEAD 时，其 `sendall` 收到 reset，尚未进入第五个
   job 已排队的断言。

服务启动日志正常；没有崩溃栈或主动退出信息。此次已确认的范围仅为
第五个连接的 reset，不能据此宣布竞态、idle timeout 或 use-after-free。
产品下载实现、下载探针及暂停夹具本轮均未改动，LF 标准化 SHA-256 为：

| 文件 | SHA-256 |
| --- | --- |
| app/src/api/image_downloads.c | e45a1715fcf234a7ff987c456061ac56db8c5cc873a0544374d493f18e87beb1 |
| tests/test_image_download_runtime.py | 8162b835d0a7d688e1a483764f5b1fa42f342e9fca3a903ac571823e9ef21318 |
| tests/fixtures/image-download.c | d4d79f0bf9627f975e4750f5eef3e60831ee9c21af602e7a8b462d7b9abb79a2 |

未放宽断言或更改连接时序。相同最终源/宿主的两次独立普通流程均通过
HTTP/TLS，日志分别是 `.build/qa-image-download-linux-focused.log` 和
`.build/qa-image-download-linux-isolated-second.log`；随后完整有界门禁
通过，保存为 `.build/qa-image-linux-final.log`。没有压力/高负载测试。
这些证明失败目前未稳定复现，仍不能关闭问题。

下一步保留该用例，在失败清理前记录进程退出状态，若再次出现，补充
第五个连接的 accept/read/close 身份与原因、任务回收和 peer 侧信息，
建立短小确定性复现后再判断归属。
在没有该证据前不改动 xrt 核心，也不让网络 reset 的结论覆盖独立离线
PNG/JPEG/WebP 像素校验结果。
