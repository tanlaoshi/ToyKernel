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

未检出源码时 `LWIP=1` 会编译失败；默认 `make`（无 lwIP）不受影响。

移植文件：`HAL/X86_64/LwIp/`（`lwipopts.h`、`toy_netif.c`、`toy_ping.c`、`toy_tcpecho.c`、`toy_udp.c`、`toy_tcpclient.c`）。

---

## 双栈策略（builtin vs lwIP）

ToyOS 同时保留两套 IP/传输实现，但**运行时不同时处理同一帧**。

| 模式 | 何时 | RX 路径 | TX / Shell 命令 | 用途 |
|------|------|---------|-----------------|------|
| **builtin（默认）** | `make`（无 `LWIP`），或 `LWIP=1` 但未 `lwip on` | `Net.c` → `HandleIpPacket` → ICMP / `Udp` / `Tcp` | 自研栈：`ping` / `udpsend` / `udplisten` / `tcplisten` / `tcpconnect` | 零第三方依赖、教学演示、快速联调 |
| **lwIP（生产路径）** | `make LWIP=1` 后 Shell `lwip on` | 仅 `ToyNetifInput` → lwIP | 同名命令自动改走 lwIP；builtin TX/RX **禁用** | 更完整协议栈；后续用户态 socket（PR-N8）以此为准 |

### 规则（必须遵守）

1. **一帧一栈**：`lwip on` 置 `HalNetSetLwIpRx(1)` 后，入站帧不再进 builtin `HandleIpPacket`。
2. **`lwip on` 不可逆（当前会话）**：会 `TcpInit`/`UdpInit` 清空自研连接；builtin `TcpPoll` 停转；`NetSendIp` / `NetPing` 在 lwIP 活跃时失败。要回 builtin：重启 QEMU（无 `lwip off` 热切回）。
3. **命令名共用**：Shell 不区分两套 API；`LwIpActive()` 决定路由。看当前栈：`net` / `lwip status` / `tcpstatus`。
4. **主栈方向**：新功能（多连接、用户态 socket）落在 **lwIP**。自研 `Tcp.c` / `Udp.c` 为 **legacy 教学栈**，仅维持现有单连接联调能力，不再做多连接槽 / 拥塞控制等深化（见 PR-N7 冻结）。

### 用户态 socket（PR-N8）

| 系统调用 | 编号 | 参数 | 说明 |
|----------|------|------|------|
| `socket` | 8 | domain, type, protocol | 仅 `AF_INET` + `SOCK_STREAM`；首次调用自动 `LwIpInit` |
| `connect` | 9 | fd, ip(host u32), port | 主动连接 |
| `bind` | 10 | fd, ip(`0`=ANY), port | 绑定本地端口 |
| `listen` | 11 | fd, backlog | 进入监听 |
| `accept` | 12 | listen_fd | 返回新连接 fd（默认一直等到有连接） |
| `write`/`read`/`close` | 1/3/4 | 同文件 FD | socket fd 上即 send/recv/close |

验证：`NETDEMO.ELF`（客户端→8888）、`NETSRV.ELF`（服务端:9000 echo）。

### 快速验证

```bash
# builtin
./build.sh
# toyos> ping 10.0.2.2
# toyos> tcplisten 9000

# lwIP
./build.sh LWIP=1
# toyos> lwip on
# toyos> ping 10.0.2.2
# toyos> tcpconnect 10.0.2.2 8888 hi
```

`run.sh` / `run-split.sh` 已配置 `hostfwd=tcp::9000-:9000`。
