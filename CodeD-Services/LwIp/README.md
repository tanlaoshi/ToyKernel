# LwIp/ — 网络服务簇（lwIP + 地址配置 + legacy UDP）

> **PR-MOD-svc-lwip**：从 `Services/` 根迁入。

| 文件 | 职责 |
| ---- | ---- |
| `LwIp*.c` | lwIP 主栈 / DHCP / socket / Config 绑定 |
| `NetConfig.c` | 可配 IPv4 / 网关 / DNS |
| `Udp.c` | legacy 教学对照 UDP（非新功能主路径） |
| `LwIpPrivate.h` | 仅本夹内部 |

公开门面仍在 `Include/{LwIp,NetConfig,Udp}.h`。

## 本刀不搬

- `Services/Install.c`：**Guest 装盘骨架**（非 Store 胶水）；**svc-misc** 迁入已有 `FileSystem/`（不新开第 4 模块）。
