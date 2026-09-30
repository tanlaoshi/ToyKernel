# RootFs ELF 清单（PR-MOD-app-inventory）

> **状态**：✅ inventory TG；sample/repack 已执行部分入店（2026-09-30）。  
> **规格**：[`Documents/待做/模块化与App课堂闭环.md`](../../Documents/待做/模块化与App课堂闭环.md) §6.4–6.5。  
> **范围**：`ToyImage/RootFs/X64/*.ELF`（不含 `Kernel.elf`）；另记 `LIBTOY.SO` 与扁平 `Apps/*.ELF`。  
> **处置写死后**由 `app-sample` / `app-repack` / `rootfs-trim` 执行；本文件**不**改 `build.sh`。

## 图例

| 处置 | 含义 | 下一刀 |
| ---- | ---- | ------ |
| **入店** | 标准 `packages/<id>/` + `catalog.txt`；装后 `Apps/<id>/` | sample / repack |
| **根白名单** | `build.sh` 可继续拷到卷根，供 `exec NAME.ELF` 课用捷径 | rootfs-trim 保留 |
| **废弃** | 停同步并从 RootFs 删除（本表暂无） | rootfs-trim |

菜单：`taskbar=yes` / `desktop=yes` 仅标「产品」类；教学入店默认 **不**进开始菜单。

---

## 全表（按卷根名）

| 卷根 ELF | User 源 | 现状 | 处置 | 建议 id | 备注 |
| -------- | ------- | ---- | ---- | ------- | ---- |
| `HELLO.ELF` | `Apps/Hello.c` | catalog；`Apps/hello/`+StoreCache+根 | **入店** + **根白名单** | `hello` | sample：补 `packages/hello/`；根留捷径 |
| `GUIDEMO.ELF` | `Apps/GuiDemo.c` | catalog；包缺 ELF；`Apps/guidemo/` | **入店** | `guidemo` | sample：包内带 ELF；可产品菜单 |
| `CAT.ELF` | `Apps/Cat.c` | catalog；扁平 `Apps/CAT.ELF` | **入店** | `cat` | sample；去掉扁平 |
| `TASKMGR.ELF` | `Apps/TaskMgr.c` | ✅ packages+catalog；`Apps/taskmgr/` | **入店**（产品） | `taskmgr` | 扁平已废 |
| `SNAKE.ELF` | `Apps/Snake.c` | ✅ packages+catalog | **入店**（产品） | `snake` | |
| `WINDEMO.ELF` | `Apps/WinDemo.c` | ✅ packages+catalog | **入店**（教学 GUI） | `windemo` | |
| `BLITDEMO.ELF` | `Apps/BlitDemo.c` | ✅ packages+catalog | **入店**（教学 GUI） | `blitdemo` | |
| `FORK.ELF` | `Apps/Fork.S` | 仅根 | **根白名单** | — | 课用 `exec FORK.ELF` |
| `SYSFORK.ELF` | `Apps/SysFork.S` | 仅根 | **根白名单** | — | int80/系统调用演示 |
| `SYSHELLO.ELF` | `Apps/SysHello.S` | 仅根 | **根白名单** | — | |
| `EXECDEMO.ELF` | `Apps/ExecDemo.c` | 仅根 | **根白名单** | — | |
| `PIPEDEMO.ELF` | `Apps/PipeDemo.c` | 仅根 | **根白名单** | — | |
| `WAITNH.ELF` | `Apps/WaitNoHang.S` | 仅根 | **根白名单** | — | |
| `THREADSMOKE.ELF` | `Apps/ThreadSmoke.c` | 仅根 | **根白名单** | — | 线程课烟雾 |
| `PTHREADSMOKE.ELF` | `Apps/PthreadSmoke.c` | 仅根 | **根白名单** | — | |
| `THREADDEMO.ELF` | `Apps/ThreadDemo.c` | 仅根 | **根白名单** | — | |
| `ENOSYS.ELF` | `Apps/EnosysDemo.c` | 仅根 | **根白名单** | — | ABI/`-ENOSYS` 课 |
| `BRKDEMO.ELF` | `Apps/BrkDemo.c` | 仅根 | **入店**（教学） | `brkdemo` | 可不进菜单 |
| `MMAPDEMO.ELF` | `Apps/MmapDemo.c` | 仅根 | **入店**（教学） | `mmapdemo` | |
| `KILLDEMO.ELF` | `Apps/KillDemo.c` | 仅根 | **入店**（教学） | `killdemo` | |
| `SIGDEMO.ELF` | `Apps/SigDemo.c` | 仅根 | **入店**（教学） | `sigdemo` | |
| `SLEEPDEMO.ELF` | `Apps/SleepDemo.c` | 仅根 | **入店**（教学） | `sleepdemo` | |
| `LIBCDEMO.ELF` | `Apps/LibcDemo.c` | 仅根 | **入店**（教学） | `libcdemo` | |
| `COUNT.ELF` | `Apps/Count.S` | 仅根 | **根白名单** | — | 极简计数 |
| `DYNDEMO.ELF` | `Apps/DynDemo.S` | 仅根；需 `LIBTOY.SO` | **根白名单** | — | 与 `LIBTOY.SO` 同留根 |
| `DIRDEMO.ELF` | `Apps/DirDemo.c` | 仅根 | **入店**（教学） | `dirdemo` | |
| `CWDDEMO.ELF` | `Apps/CwdDemo.c` | 仅根 | **入店**（教学） | `cwddemo` | |
| `WRITE.ELF` | `Apps/WriteFile.c` | 仅根 | **入店**（教学） | `write` | file=`WRITE.ELF` |
| `NETDEMO.ELF` | `Apps/NetDemo.S` | 仅根 | **入店**（教学网） | `netdemo` | |
| `NETSRV.ELF` | `Apps/NetServer.S` | 仅根 | **入店**（教学网） | `netsrv` | |
| `NETLIB.ELF` | `Apps/NetLibDemo.c` | 仅根 | **入店**（教学网） | `netlib` | |
| `SOCKDEMO.ELF` | `Apps/SockDemo.c` | 仅根 | **入店**（教学网） | `sockdemo` | |

### 非 ELF / 旁路

| 路径 | 处置 | 备注 |
| ---- | ---- | ---- |
| `LIBTOY.SO` | **根白名单** | `DYNDEMO` 依赖；trim 与 DYNDEMO 同留 |
| `Apps/hello/` `Apps/guidemo/` | 保留为正统预装或改由 store 安装生成 | sample 后以包为准 |
| `Apps/TASKMGR.ELF` `Apps/CAT.ELF` | **已废除扁平**（repack） | 现为 `Apps/taskmgr/`；cat 仅包/根 |
| `StoreCache/HELLO.ELF` | 可保留作缓存 | 与入店不冲突 |
| `Kernel.elf` | 非本表 | 构建产物 |

---

## 汇总（供 trim / repack）

| 处置 | 数量（约） | 成员摘要 |
| ---- | ---------- | -------- |
| 入店 + 根白名单 | 1 | `HELLO` |
| 入店（已 catalog / sample） | 2 | `GUIDEMO` `CAT` |
| 入店（产品 repack） | 2 | `TASKMGR` `SNAKE` |
| 入店（教学 GUI/系统/网） | 16 | windemo blitdemo brk… sockdemo 等 |
| 仅根白名单 | 11 + `LIBTOY.SO` | fork/sys*/thread*/enosys/count/dyndemo… |
| 废弃 | 0 | — |

**根白名单（trim 后允许的卷根用户 ELF）**

`HELLO.ELF` `FORK.ELF` `SYSFORK.ELF` `SYSHELLO.ELF` `EXECDEMO.ELF` `PIPEDEMO.ELF` `WAITNH.ELF` `THREADSMOKE.ELF` `PTHREADSMOKE.ELF` `THREADDEMO.ELF` `ENOSYS.ELF` `COUNT.ELF` `DYNDEMO.ELF`（+ `LIBTOY.SO`）

其余由商店安装，不再由 `build.sh` 刷根。

---

## 修订

| 日期 | 说明 |
| ---- | ---- |
| 2026-09-30 | 初稿：对照 RootFs 与 `User/Apps`；处置供后续刀执行 |
| 2026-09-30 | repack：taskmgr/snake/windemo/blitdemo；去扁平 Apps |
