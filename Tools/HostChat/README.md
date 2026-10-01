# HostChat · `chatd`（PR-CHAT-1）

Ubuntu 侧 TCP **行聊天服务端**。协议见 [`开发/局域网商店与聊天.md`](../../Documents/开发/局域网商店与聊天.md) **§3.3**。

| 项 | 值 |
| -- | -- |
| 端口 | **9090**（可 `./chatd <port>`） |
| 帧 | 一行一条，`\n` 结束；单行 ≤200 字节 |
| 角色 | 本进程 = 服务端；ToyOS `CHAT.ELF` / `nc` = 客户端 |

## 构建

```bash
cd Tools/HostChat
make          # → ./chatd
```

## 本机互测（验收）

终端 A：

```bash
./chatd
```

终端 B：

```bash
nc 127.0.0.1 9090
```

两边各打几行回车，应互见。Ctrl+C 停服务端；`nc` 断开后 `chatd` 继续等下一客户端。

## 与 NUC（chat-2+）

1. 台式机跑 `./chatd`（防火墙放行 TCP 9090）。  
2. Guest：`dbset chat.peer <台式机LAN-IP>` → 装/跑 `CHAT.ELF`。  
