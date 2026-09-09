# 真机 USB 键盘：小 PR 切分（Real-PC USB PR split）

> 源计划：公司机 Cursor `~/.cursor/plans/real-pc_usb_pr_split_3ddff5d5.plan.md`  
> 入库目的：家里 Cursor 可 `@Documents/real-pc-usb-pr-split.md` 对齐同一把刀序列。  
> **协作**：JX = 实现不 commit；TG = commit+push。勿改公司机上的原 plan 文件当唯一真相——以本文件 + [`home-xhci-handoff.md`](home-xhci-handoff.md) 为准。

## 起点与原则

- **工作分支（当前）**：`home/xhci-pr1-5-bundle`（PR1–5 合包 + 真机迭代 WIP）
- **曾计划起点**：`home/xhci-retry-from-scratch` @ `ea3c143`（干净重做基线；现以 bundle 为准）
- **每刀验收**：公司 `./smoke-boot.sh` PASS + 家里真机约定日志；过线再下一刀
- **不进本序列**：分辨率 / ToyBoot SetMode

```mermaid
flowchart LR
  obs[PR1_obs日志] --> rs[PR2_控制器RS]
  rs --> port[PR3_端口复位]
  port --> enum[PR4_枚举HID]
  enum --> base[PR5_poll打字]
  base --> hub[PR6_hub可选]
```

## PR 序列

| PR | 目标 | 状态 |
|----|------|------|
| PR-H-xhci-obs | DiagChk + BootMark / PHOTO | ✅ 真机可见 |
| PR-H-xhci-rs | firmware-first；RS running | ✅ |
| PR-H-xhci-port | leavePRC / already-skip；PED | ✅ |
| PR-H-xhci-enum | EnableSlot + priv rings；HID 枚举 | ✅ |
| PR-H-xhci-base | 真机 `irq=poll` **能打字** | 🔧 **当前刀** |
| PR-H-hub | 根口 hub 后键盘 | ⬜ |
| PR-H-xhci-dual/stat | MSI-X + fallback | ⬜ base 通后 |

### PR5 过线标准

- PHOTO：按键/鼠标时 `i=` / `k=` / `m=` 上涨
- 桌面或 Shell 可输入
- 公司 smoke 仍 PASS（`xhci-hid keyboard` + `irq=msi`）

### PR5 已尝试（摘要）

详见 [`home-xhci-handoff.md`](home-xhci-handoff.md)。

## 第一刀落地说明（历史）

原计划批准后先做 PR1 obs；现 PR1–4 已在真机过线，卡在 PR5 中断 IN 完成 / 推送。
