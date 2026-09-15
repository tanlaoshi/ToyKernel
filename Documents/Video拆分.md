# 任务：拆分 Video.c（PR-H-video-split-1 / -2）

> **规格同 [`大文件拆分.md`](大文件拆分.md)**：只搬家、不改逻辑；`Video.h` / `HalVideo.h` 不动；每刀 build + smoke。  
> **本文件 = 轨 E · Video 细则**；总盘点见 [`大文件拆分3.md`](大文件拆分3.md)。  
> **★ 柱完成（轨 E 两刀）**：split-2 本地待 TG。  
> **统计时点**：2026-09-16；split-1 本地落地。

### ★ 拆分进度

| 状态 | PR | 内容 | 说明 |
| --- | --- | --- | --- |
| ✅ 本地 | **PR-H-video-split-1** | `VideoPriv.h` + `VideoBochs.c` + `VideoScale.c` | DISPI；UiScale；**含 `VideoSet`** |
| ✅ 本地 | **PR-H-video-split-2** | `VideoPresent.c` | Dirty/Cursor/Present |

**行数（约）**：`Video.c` ~800；Present ~292；Bochs ~89；Scale ~103。  
**验收**：split-2 `./build.sh` OK；串口 `ToyOS ready` PASS（2026-09-16）。

### 边界

- **不动** `HalVideo.c`（薄封装）；Present 只在 `Video*.c` 一族。
- 刀 1 后宿主仍 >900：≤600 靠刀 2 抽 Present（~200+）及后续 Draw。

### 目标树

```
HAL/X64/Drivers/
├── Video.c           # 宿主全局 + Draw/Present（刀 2 再瘦）
├── VideoPriv.h       # 内部共享
├── VideoBochs.c      # DISPI
├── VideoScale.c      # UiScale / 逻辑分辨率
└── VideoPresent.c    # Dirty / Present
```
