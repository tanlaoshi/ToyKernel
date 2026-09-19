# 任务：拆分 Net.c（PR-H-net-split-1）

> **规格**：只搬家；`Net.h` / 对外网络 API 语义不变；每刀 build + smoke。  
> **★ 下一刀**：默认排队已空（`Net.c` ~771 ≤800 → **不**开 net-split-2）；后置见路线图。  
> **统计**：2026-09-16；split-1 TG `b833ef9`。

### ★ 进度

| 状态 | PR | 内容 |
| --- | --- | --- |
| ✅ `b833ef9` | **PR-H-net-split-1** | `NetPrivate.h` + `NetVirtio.c`（队列/PCI/`VirtioNetStart`）；`Net.c` 留 ARP/ICMP/API |

**行数**：`Net.c` ~771；`NetVirtio.c` ~351；`NetPrivate.h` ~154。  
**验收**：`./build.sh` + headless smoke `ToyOS ready` PASS（2026-09-16；`TOY_NO_HOSTFWD`）。
