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

Shell：`lwip on` 后 `ping` / `tcplisten` / `udpsend` / `udplisten` 自动走 lwIP。

移植文件：`HAL/X86_64/LwIp/`（`lwipopts.h`、`toy_netif.c`）。
