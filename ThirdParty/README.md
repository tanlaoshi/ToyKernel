# ThirdParty

## lwIP

ToyOS 可选嵌入 [lwIP](https://savannah.nongnu.org/projects/lwip/) 2.2.x：

```bash
git clone --depth 1 --branch STABLE-2_2_0_RELEASE \
  https://github.com/lwip-tcpip/lwip.git ThirdParty/lwip
```

构建：

```bash
make LWIP=1
# 或
./build.sh LWIP=1
```

### 双栈策略

| 阶段 | RX | Shell 网络命令 |
|------|-----|----------------|
| 默认 `make` | builtin（`Net.c` HandleIpPacket） | ping / tcp / udp 自研栈 |
| `make LWIP=1` 且未 `lwip on` | builtin | 同上（fallback） |
| `lwip on` | **仅 lwIP**（`ToyNetifInput`） | 自动走 lwIP；builtin TX/RX 禁用 |

`lwip on` 后：`NetSendIp` / `NetPing` 返回失败，builtin `TcpPoll` 不再运行；请先 `lwip on` 再测 ping / tcplisten / tcpconnect / udpsend。

移植文件：`HAL/X86_64/LwIp/`（`lwipopts.h`、`toy_netif.c`、`toy_ping.c`、`toy_tcpecho.c`、`toy_udp.c`、`toy_tcpclient.c`）。
