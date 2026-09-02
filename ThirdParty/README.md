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

Shell：`lwip on` 切换到 lwIP 协议栈（会接管 RX，内置 ping/tcp/udp 不再收包）。

移植文件：`HAL/X86_64/LwIp/`（`lwipopts.h`、`toy_netif.c`）。
