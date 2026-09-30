# Store/ — 本地商店源（PR-LAN-store-src）

与 `Assets/` 同级。Guest 卷根同样是 **`Store/`**（prepare / `build.sh` 同步）。

| 路径 | 用途 |
| ---- | ---- |
| `Store/catalog.txt` | 本地可安装项 |
| `Store/packages/<id>/` | `PKG.TXT` + 载荷（ELF / 字体 / 资源） |
| `Store/remote.cat` | Guest 运行时：`store sync` 写入的**远程目录**（非 ELF 缓存） |
| `Apps/<id>/` | 已装运行副本（网装直达此处） |

**废止**：`Assets/Store/`、`StoreCache/`（勿再创建）。

## catalog 行格式

```
id|type|version|file|sha256|arch|title[|depends]
```

例：`hello|app|1|HELLO.ELF|-|x86_64|Hello`

## 载荷查找（install）

1. `Store/packages/<id>/<file>`
2. `Store/<file>`
3. 卷根 `<file>`
4. 皆无 → HTTP `GET /<file>` **直写**目标（`Apps/` / `Assets/Fonts/` / `Assets/Packs/`）

`store sync` **只**更新 `Store/remote.cat`；列表合并本地+远程（同 id 网络覆盖元数据），标 `[本]` / `[网]`。

## 三种来源

| 来源 | 表现 |
| ---- | ---- |
| 镜像预置（prepare 写入 packages + Apps） | `* [本]` 已装 |
| 手拷 packages（未 install） | `  [本]` 未装 |
| sync 后仅远端有 | `  [网]`；`store install` 直达 Apps |

## 打包

```bash
./Tools/Scripts/pack-app.sh <id> Build/User/<app>.elf FILE.ELF
./Tools/Scripts/export-store-lan.sh   # → Build/store-lan + http.server
```

详见 [`Documents/开发/局域网商店与聊天.md`](../Documents/开发/局域网商店与聊天.md) §2.8。
