# Linux 挂载盘的 Home 导入原子操作记录

2026-10-01 更新：缓存 Home 导入现已在写入 payload 前检查真实的空目录
不覆盖移动。当前挂载盘被明确拒绝，HTTP 返回 `409` 与稳定错误码
`migration_storage_unsupported`；页面有中、英、俄三语解释。失败后不留下
事务目录，原来源及缓存保持，Home 不需要重启且没有被导入冻结。检查前后
进程退出由现有空目录恢复处理，未知内容仍保留并阻止启动。只读检测和
普通启动没有新增试探写入，xrt 底层及其原子保证没有修改。

这修复了缓存 Home 导入的残留/冻结与错误提示；它没有使当前挂载盘支持
原子不覆盖重命名，也不代表所有 Home 事务在此路径可用。以下保留首次复现
证据。验证与实现详情见 [迁移记录](frontend-migration.md#2026-10-01旧数据导入在写入前检查文件系统能力)。

2026-10-01 在当前锁定依赖和同一份 mdo 源码上重新验证 Linux。WSL 的临时
文件实际写入、GCC/Node/Python 和宿主构建均恢复正常，之前的只读/I/O error
不再是本次阻碍。

`/mnt/d/GIT/mdo` 下的完整发布验证在 `test_api_runtime.py` 的
`run_cache_migration_probe` 失败：故障注入的导入返回 500 后，断言
`not (home / ".mdo-import").exists()` 未满足。没有放宽断言、手动清理
用户 Home 或把该次门禁计为通过。

复制同一源码至 Linux 原生文件系统后，全门禁通过：114 项 Python、164 项
Node、85 个模块解析、严格 C11、31 个运行探针及两个包的哈希比较。
两次验证使用同一个从锁定 xs `5f1e31a20f236adfeb9b4f5a554aa1df41771558`
源码新构建的 Linux 宿主。差异在工作目录及测试 Home 所在的文件系统。

进一步使用自有空目录调用 `renameat2`，结果为：

| 目录所在位置 | `RENAME_NOREPLACE` 结果 |
| --- | --- |
| `/mnt/d/GIT/mdo/.build` | `-1`，errno `22`，`Invalid argument` |
| `/tmp` | `0`，成功 |

这说明当前挂载盘不接受所测试的不覆盖目标重命名。Home 导入的事务退休由
`MdoHomeImportRetire` 调用 `xrtRootRenameNoReplace`；锁定 xrt 的 Linux 实现
使用同一系统调用及标志。因此它是本次回滚失败的重要原因。完整调用现场
的 xrt 错误链尚未单独捕获，不能据此推广到所有 NTFS、WSL 或 Linux 系统。

门禁日志保存在工作区：

- `.build/qa-recovery-context-linux-release.log`：挂载盘失败。
- `.build/qa-recovery-context-linux-native-release.log`：原生文件系统全量通过。
- 原生快照：`/home/ubuntu/.cache/mdo-linux-qa-recovery-context`。

下面是独立、无网络、每种文件系统只创建一个临时空目录的复现。只在 Linux
运行；测试目录在正常退出时由 `TemporaryDirectory` 清理，不访问用户 Home。

```python
import ctypes
import os
from pathlib import Path
import tempfile

libc = ctypes.CDLL(None, use_errno=True)
libc.renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p,
                         ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
for parent in ["/mnt/d/GIT/mdo/.build", "/tmp"]:
    with tempfile.TemporaryDirectory(prefix="mdo-rename-probe-", dir=parent) as raw:
        source, target = Path(raw) / "source", Path(raw) / "target"
        source.mkdir()
        ctypes.set_errno(0)
        result = libc.renameat2(-100, os.fsencode(source),
                               -100, os.fsencode(target), 1)
        error = ctypes.get_errno()
        print(parent, result, error, os.strerror(error))
```

仍需后续处理：缺失原子能力的文件系统能支持哪些 Home 写入及事务，以及
如何在对应业务入口统一报告限制。本次只为缓存 Home 导入提前检查并明确
拒绝，没有增加不等价的兼容重命名实现。若增加兼容实现，必须保留不覆盖
目标、目录身份校验和跨进程事务约束，并在此挂载盘重新通过原探针。不能用
“先判断不存在再普通 rename”替代这些保证，也不能把事务残留当成可随意
删除的缓存。
