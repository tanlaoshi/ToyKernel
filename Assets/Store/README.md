# Assets/Store — 离线商店目录（PR-S0）

Guest 路径：`Assets/Store/`。与 `Assets/Icons` / `Fonts` / `Locale` 同级，**只读约定源**（课堂镜像预置）。

| 路径（FAT 上） | 用途 |
|----------------|------|
| `Assets/Store/catalog.txt` | 离线可安装项列表 |
| `Assets/Store/packages/<id>/` | 可选：包描述 `PKG.TXT` + 载荷（S1 起） |
| `Store/`（卷根） | **本地缓存**（`store sync`/`fetch` 或优盘拷入） |
| `Apps/`（卷根） | **已安装**用户 ELF（`store install`） |

本刀起 **S1** 提供 `store install`；**S2** 提供 `store sync` / `store fetch` / `store repo`。总规划 [`Documents/应用商店规划.md`](../../Documents/应用商店规划.md)。

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
| `sha256` | `-` 跳过；**8 位 hex** = 教学 FNV-1a-32（非真 SHA-256） |
| `arch` | `x86_64` / `arm64` / `riscv64` / `any` |
| `title` | 显示名（可 UTF-8） |

## PKG.TXT（可选，包目录内）

与规划稿一致的 `key=value`；`file=` 相对该包目录。S1 优先读包内 `PKG.TXT`，否则用 catalog 行。

## S2 联网

宿主静态树见 [`ToyImage/store-repo/`](../../../ToyImage/store-repo/)；Guest 默认 `store.repo=10.0.2.2:8080`。