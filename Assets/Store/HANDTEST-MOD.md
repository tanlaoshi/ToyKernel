# 模块化轨 C · 真机/手测清单（PR-MOD-app-verify）

> 对应规格 [`Documents/待做/模块化与App课堂闭环.md`](../../Documents/待做/模块化与App课堂闭环.md) §6；路线图 ★ `PR-MOD-app-verify`。  
> QEMU 自动化：`ToyImage/Scripts/test-mod-verify.sh`（需 `expect`）。  
> store-disk 双盘真机挂账：**不阻塞**本刀 TG。

## 刷盘

| # | 步骤 | 期望 |
| - | ---- | ---- |
| 0 | `sync-nuc.sh`（NUC）或 `sync-usb.sh`（U 盘备选） | 有 `TOYOS.ID`；ToyOS 无残留 `EFI/` |
| 1 | 开机进桌面 | 默认卷 ToyOS |

## 根白名单（trim）

| # | 步骤 | 期望 |
| - | ---- | ---- |
| 2 | `ls` 卷根 | 有 `HELLO.ELF` 等白名单；**无** `SNAKE`/`TASKMGR`/`CAT`/`GUIDEMO` 等入店类 |
| 3 | `exec HELLO.ELF` | 成功 |
| 4 | `exec SNAKE.ELF` | 失败/找不到（须商店） |

## 预装 / 无扁平

| # | 步骤 | 期望 |
| - | ---- | ---- |
| 5 | 看 `Apps/` | 有 `hello/` `guidemo/` `taskmgr/`；**无** `Apps/*.ELF` 扁平 |

## Store combo（repack）

| # | 步骤 | 期望 |
| - | ---- | ---- |
| 6 | `store combo snake` | `store job: installed`（非 fail） |
| 7 | 有 `Apps/snake/`；能开 Snake | 桌面/开始菜单或 `exec Apps/snake/SNAKE.ELF` |
| 8 | `store uncombo snake` | 目录与菜单消失 |
| 9 | `store combo taskmgr` → 开窗 → `uncombo` | 同 6–8 |
| 10 | `store combo guidemo` | 依赖 demopack/sun8 装齐可跑 |
| 11 | `store combo cat` / `windemo` / `blitdemo` | 装后 `Apps/<id>/` 可 exec |

## 串口壳（顺带）

| # | 步骤 | 期望 |
| - | ---- | ---- |
| 12 | 看开机串口 | `toyos>` 不与 `AP entered idle` 抢同一行 |
| 13 | 空 Enter | 只多一行 `toyos>` |

## 自动化（QEMU）

```bash
cd ToyImage && ./Scripts/test-mod-verify.sh
```

覆盖：`test-bundle-install` / `remove` / `combo`（含根白名单 + snake/taskmgr）。
