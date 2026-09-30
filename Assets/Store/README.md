# Assets/Store — 离线商店目录（PR-S0～S3）

Guest 路径：`Assets/Store/`。与 `Assets/Icons` / `Fonts` / `Locale` / `Packs` 同级，**只读约定源**（课堂镜像预置）。

| 路径（FAT 上） | 用途 |
|----------------|------|
| `Assets/Store/catalog.txt` | 离线可安装项列表 |
| `Assets/Store/packages/<id>/` | 可选：包描述 `PKG.TXT` + 载荷 |
| `StoreCache/`（卷根） | **本地缓存**（运行时目录；`store sync`/`fetch` 或优盘拷入；prepare 只 `mkdir`） |
| `Apps/`（卷根） | **已安装**应用 = `Apps/<id>/` 目录包（勿再新增扁平 `Apps/*.ELF`） |
| `Assets/Fonts/` | **已安装**字库（`type=font`，TOYF `*.FNT`） |
| `Assets/Packs/` | **已安装**资源 blob（`type=asset`） |

- **S1 ✅**：`store install`（app → `Apps/<id>/`）  
- **S2 ✅**：`store sync` / `fetch` / `repo`  
- **S3 ✅**：`type=font` → `Assets/Fonts/`；`type=asset` → `Assets/Packs/`；安装后字库自动 `FontReloadAssets`  

总规划 [`Documents/路线图.md`](../../Documents/路线图.md)。
局域网台式机仓库 / NUC 下载：[`Documents/开发/局域网商店与聊天.md`](../../Documents/开发/局域网商店与聊天.md)。  
RootFs 根 ELF 处置表：[`ROOTFS-ELF.md`](ROOTFS-ELF.md)。  
真机/手测清单：[`HANDTEST-MOD.md`](HANDTEST-MOD.md)（`test-mod-verify.sh`）。

## catalog.txt

- `#` 行注释；空行忽略  
- 每条一项，字段用 `|` 分隔（8.3 友好、易手写）：

```text
id|type|version|file|sha256|arch|title[|depends]
```

| 字段 | 说明 |
|------|------|
| `id` | 短名（目录名 / DB 键） |
| `type` | `app` \| `font` \| `asset` |
| `version` | 十进制整数 |
| `file` | 安装后落盘文件名 |
| `sha256` | `-` 跳过；**8 位 hex** = 教学 FNV-1a-32（非真 SHA-256） |
| `arch` | `x86_64` / `arm64` / `riscv64` / `any` |
| `title` | 显示名（可 UTF-8） |
| `depends` | **可选（PR-M1）**：逗号分隔包 id；`store install` 缺依赖拒绝；**PR-M2** `store combo` 按序装齐 |

示例（M2 功能 = app + asset + font）：

```text
guidemo|app|1|GUIDEMO.ELF|-|x86_64|GUI Demo|demopack,sun8
demopack|asset|1|INFO.TXT|-|any|Demo asset pack
sun8|font|1|VGA8X16.FNT|-|any|Sun 8x16 (store)
```

```text
store combo guidemo      # demopack → sun8 → guidemo
store remove demopack    # 拒绝（仍被 guidemo 需要）
store uncombo guidemo    # -guidemo → -sun8 → -demopack（无引用才卸）
```
## PKG.TXT（可选，包目录内）

与规划稿一致的 `key=value`；`file=` 相对该包目录。  
**PR-M1**：若含 `depends=`，安装时**覆盖** catalog 第 8 段（`depends=-` 或空 = 无依赖）。

## 安装源顺序

`StoreCache/<file>` → `Assets/Store/packages/<id>/<file>` → 卷根 `<file>`（**目录名须等于 catalog `id`**，如 `demopack/`）。无网时课堂预置 packages 即可 `store install sun8`。

课堂跟做步骤见 [`Documents/开发/应用开发指南.md`](../../Documents/开发/应用开发指南.md) §十四。

**现状**：`StoreCache/catalog.txt` 非空时会**整表盖掉** `Assets/Store/catalog.txt`（本地预置从列表消失）。  
**[`PR-LAN-store-src`](../../Documents/路线图.md#pr-lan-store-src)**：改为**合并**两表；同 `id` 以网络为准；Store 列表行标「本地 / 网络」。细节见 [`局域网商店与聊天.md` §2.8](../../Documents/开发/局域网商店与聊天.md)。  
在 store-src 落地前，课堂更新 catalog 仍请一并刷新或删掉 `StoreCache/catalog.txt`。

## S2 联网

- **QEMU 课**：宿主 [`ToyImage/store-repo/`](../../../ToyImage/store-repo/)（或 `Fixtures/store-repo/`）+ `python3 -m http.server 8080`；Guest 默认 `store.repo=10.0.2.2:8080`。  
- **局域网（台式机 → NUC）**：见 [`Documents/开发/局域网商店与聊天.md`](../../Documents/开发/局域网商店与聊天.md)  
  - 导出：`./Tools/Scripts/export-store-lan.sh` → `Build/store-lan/`  
  - 一键服务：`./Tools/Scripts/serve-store-lan.sh`  
  - NUC：`store repo <台式机IP>:8080` → `sync` / `fetch` / `install`  
