# Assets/Store — 离线商店目录（PR-S0～S3）

Guest 路径：`Assets/Store/`。与 `Assets/Icons` / `Fonts` / `Locale` / `Packs` 同级，**只读约定源**（课堂镜像预置）。

| 路径（FAT 上） | 用途 |
|----------------|------|
| `Assets/Store/catalog.txt` | 离线可安装项列表 |
| `Assets/Store/packages/<id>/` | 可选：包描述 `PKG.TXT` + 载荷 |
| `Store/`（卷根） | **本地缓存**（`store sync`/`fetch` 或优盘拷入） |
| `Apps/`（卷根） | **已安装**用户 ELF（`type=app`） |
| `Assets/Fonts/` | **已安装**字库（`type=font`，TOYF `*.FNT`） |
| `Assets/Packs/` | **已安装**资源 blob（`type=asset`） |

- **S1 ✅**：`store install`（app → `Apps/`）  
- **S2 ✅**：`store sync` / `fetch` / `repo`  
- **S3 ✅**：`type=font` → `Assets/Fonts/`；`type=asset` → `Assets/Packs/`；安装后字库自动 `FontReloadAssets`  

总规划 [`Documents/应用商店规划.md`](../../Documents/应用商店规划.md)。

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
| `depends` | **可选（PR-M1）**：逗号分隔包 id；缺依赖时 `store install` 提示并拒绝 |

示例（M1）：

```text
guidemo|app|1|GUIDEMO.ELF|-|x86_64|GUI Demo|demopack
demopack|asset|1|INFO.TXT|-|any|Demo asset pack
```

## PKG.TXT（可选，包目录内）

与规划稿一致的 `key=value`；`file=` 相对该包目录。  
**PR-M1**：若含 `depends=`，安装时**覆盖** catalog 第 8 段（`depends=-` 或空 = 无依赖）。

## 安装源顺序

`Store/<file>` → 卷根 `<file>` → `Assets/Store/packages/<id>/<file>`（**目录名须等于 catalog `id`**，如 `demopack/`）。无网时课堂预置 packages 即可 `store install sun8`。

若卷上已有 `Store/catalog.txt`（`store sync` 缓存），它会**优先于** `Assets/Store/catalog.txt`；课堂更新 catalog 时请一并刷新 `Store/catalog.txt`，或删掉该缓存文件。

## S2 联网

宿主静态树见 [`ToyImage/store-repo/`](../../../ToyImage/store-repo/)；Guest 默认 `store.repo=10.0.2.2:8080`。
