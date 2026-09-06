# Assets/Store — 离线商店目录（PR-S0）

Guest 路径：`Assets/Store/`。与 `Assets/Icons` / `Fonts` / `Locale` 同级，**只读约定源**（课堂镜像预置）。

| 路径（FAT 上） | 用途 |
|----------------|------|
| `Assets/Store/catalog.txt` | 离线可安装项列表 |
| `Assets/Store/packages/<id>/` | 可选：包描述 `PKG.TXT` + 载荷（S1 起） |
| `Store/`（卷根） | **本地缓存**（下载/优盘拷入暂存）；S0 仅占位 |
| `Apps/`（卷根） | **已安装**用户 ELF；S0 仅占位，S1 写入 |

本刀（S0）**不写安装逻辑**。实现见路线图 **1.3s**；总规划 [`Documents/应用商店规划.md`](../../Documents/应用商店规划.md)。

## catalog.txt

- `#` 行注释；空行忽略  
- 每条一项，字段用 `|` 分隔（8.3 友好、易手写）：

```text
id|type|version|file|sha256|arch|title
```

| 字段 | 说明 |
|------|------|
| `id` | 短名（目录名 / DB 键） |
| `type` | `app` \| `font` \| `asset`（资源型「驱动包」→ S3） |
| `version` | 十进制整数 |
| `file` | 安装后落在 `Apps/` 或 `Assets/` 的文件名 |
| `sha256` | 小写 hex；未知可写 `-`（S1 起强制校验可选） |
| `arch` | `x86_64` / `arm64` / `riscv64` / `any` |
| `title` | 显示名（可 UTF-8） |

## PKG.TXT（可选，包目录内）

与规划稿一致的 `key=value`；`file=` 相对该包目录。S1 优先读包内 `PKG.TXT`，否则用 catalog 行。
