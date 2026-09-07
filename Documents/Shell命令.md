# Shell 命令清单（一级 / 二级 + 别名）

> 命名目标：**自然语序、见字知意**。正统写法不用中横线。  
> 实现见路线图 **PR-C1～C3**；与源码命名整改的合并排期见 [`命名与Shell整改计划.md`](命名与Shell整改计划.md)。

---

## 0. 命名原则

1. **正统 = 一级词 + 可选二级词**（空格分隔）。例：`tcp listen`，不是 `tcplisten` / `tcp-listen`。
2. **语序**：尽量接近口语/书面祈使句——能「动词在前」就动词在前；网络等成族命令用「对象在前、动作在后」（`tcp listen` = 对 TCP 做 listen），与 `store install` 一致。
3. **用完整单词**：`directory` / `memory` / `database` / `devices`，不用 `dir`/`mem`/`db`/`dev` 当正统（那些只做别名）。
4. **别名**：保留旧粘连名与 Unix 习惯（`ls`、`mkdir`、`tcplisten`），方便熟手；`help` 以正统为主、括号注别名。
5. **无三级**：参数不再充当第三级命令名。
6. **不做**：用户自定义 alias 配置文件。

---

## 1. 机制（PR-C1）

| 项 | 约定 |
|----|------|
| 分发 | `Argv[0]`=一级；若该一级登记了二级，则 `Argv[1]`=二级，其余为参数 |
| Handler | 二级 Handler 收到的 `Argv[0]`=二级名，参数从 `Argv[1]` 起（已去掉一级） |
| 缺二级 | 打印 `usage: <一级> <子命令…>`（如只打 `tcp`） |
| 别名 | 整词别名（`halt`←`exit`）或粘连别名（`tcp listen`←`tcplisten`） |
| `help` | 按一级分组列出二级；括号列别名 |

```c
ConsoleRegister2("tcp", "listen", "start TCP echo server", Handler);
ConsoleRegisterAliasLine("tcplisten", "tcp", "listen");  /* 旧名 → 正统 */

ConsoleRegister("halt", "stop CPU", Handler);
ConsoleRegisterAlias("halt", "exit");
```

---

## 2. 正统命令总表

表头：**正统**（怎么打）· **别名** · **意思** · **现今名**（迁移前）。

### 2.1 内置

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `help` | `?` | 列出命令 | `help` |
| `clear` | `cls` | 清屏 | `clear` |
| `echo` | — | 打印参数 | `echo` |

### 2.2 任务 / 内存 / 用户程序

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `list tasks` | `ps`, `tasks` | 列出任务 | `ps` |
| `show memory` | `mem`, `memory` | 显示内存统计 | `mem` |
| `test memory` | `memtest` | 测一页内存 | `memtest` |
| `run user` | `runuser` | 跑内嵌 hello | `runuser` |
| `execute` | `exec` | 加载并运行 ELF | `exec` |
| `kill` | — | 向用户任务发信号 | `kill` |

> `list tasks`：动词 `list` + 对象 `tasks`，见字即「列任务」。  
> `show memory` / `test memory`：同一对象 `memory`，动作用二级区分。

### 2.3 窗口与设置

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `shell` | — | 打开 Shell 窗 | `shell` |
| `settings` | — | 打开设置 | `settings` |
| `files` | — | 打开文件浏览器 | `files` |
| `edit` | — | 打开文本编辑器 | `edit` |
| `show info` | `info` | 显示帧缓冲等信息 | `info` |
| `test glyph` | `zh` | 测中文点阵字形 | `zh` |
| `set language` | `lang`, `language` | 设置界面语言 | `lang` |
| `font` | — | 字体列表/切换/reload | `font` |

> `set language en`：语序接近「设置语言」。参数仍为 `en` / `zh` / `reload`。

### 2.4 电源

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `reboot` | — | 复位 CPU | `reboot` |
| `halt` | `exit`, `quit` | 停机 | `halt` |

### 2.5 设备 / 网络摘要

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `list devices` | `lsdev` | 列出已绑定驱动 | `lsdev` |
| `show network` | `net`, `network` | 显示网络摘要 | `net` |
| `ping` | — | ICMP 探测 | `ping` |

> `list devices` 与 `list tasks`、`list`（目录）同属「list …」族，见下节。

### 2.6 网络：`udp` / `tcp` / `lwip`（对象在前）

族内语序：**协议 + 动作**，与口头「TCP listen」「UDP send」一致。

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `udp listen` | `udplisten` | UDP 监听 | `udplisten` |
| `udp send` | `udpsend` | UDP 发送 | `udpsend` |
| `tcp listen` | `tcplisten` | TCP 回显服务 | `tcplisten` |
| `tcp connect` | `tcpconnect` | TCP 连接并发送 | `tcpconnect` |
| `tcp status` | `tcpstatus` | TCP 连接状态 | `tcpstatus` |
| `lwip on` | — | 打开 lwIP（若编入） | `lwip on` |
| `lwip status` | — | lwIP 状态 | `lwip status` |

只输入 `tcp` / `udp` / `lwip` → 打印该族 usage。

### 2.7 文件与目录（动词在前）

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `list` | `ls`, `dir` | 列出当前目录（或路径） | `ls` |
| `print` | `cat`, `type` | 打印文件内容 | `cat` |
| `write` | — | 写入文本文件 | `write` |
| `write big` | `wrbig` | 大文件写并校验 | `wrbig` |
| `make directory` | `mkdir`, `md` | 创建目录 | `mkdir` |
| `remove directory` | `rmdir`, `rd` | 删除空目录 | `rmdir` |
| `remove` | `rm`, `del` | 删除文件（或空目录） | `rm` |
| `move` | `mv`, `rename` | 重命名/移动 | `mv` |
| `list volumes` | `vols`, `volumes` | 列出挂载卷 | `vols` |
| `show file` | `filestat`, `stat` | 显示文件/目录状态 | `filestat` |
| `sync file` | `filesync` | 刷新卷到磁盘 | `filesync` |
| `stress directory` | `dirstress` | 目录簇增长压测 | `dirstress` |

> 语序对照：`make directory`、`remove directory`、`list volumes`、`show file`、`sync file`、`stress directory` —— 均按「做什么 + 对什么」阅读。

### 2.8 数据库（对象在前，与 store 同族感）

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `database get` | `dbget` | 读键 | `dbget` |
| `database set` | `dbset` | 写键 | `dbset` |
| `database list` | `dblist` | 列出键值 | `dblist` |

### 2.9 商店 `store`（已是一级+二级；去掉中横线）

| 正统 | 别名 | 意思 | 现今 |
|------|------|------|------|
| `store list` | `store`, `store status` | 列出目录与安装状态 | `store` / `list` / `status` |
| `store install` | — | 安装包（缺依赖则拒绝） | `store install` |
| `store remove` | `store rm` | 卸载包 | `store remove` |
| `store combo` | — | 按依赖顺序装齐 | `store combo` |
| `store uncombo` | — | 按依赖逆序拆卸 | `store uncombo` |
| `store installed` | `store list-installed` | 仅列已安装 | `list-installed` / `installed` |
| `store sync` | — | 同步仓库目录 | `store sync` |
| `store fetch` | — | 拉取单个包 | `store fetch` |
| `store repo` | — | 查看/设置仓库 | `store repo` |

---

## 3. `list` / `show` / `make` / `remove` 族一览

方便 `help` 与课堂板书：

```text
list              列出目录
list tasks        列出任务
list devices      列出驱动
list volumes      列出卷

show memory       内存统计
show network      网络摘要
show info         显示启动/帧缓冲信息
show file         文件状态

make directory    创建目录
remove            删除文件
remove directory  删除空目录

test memory       内存页测试
test glyph        字形测试

run user          内嵌用户程序
set language      界面语言

sync file         刷盘
stress directory  目录压测
write big         大文件写测
```

网络 / 库 / 商店仍按对象族：

```text
udp listen | udp send
tcp listen | tcp connect | tcp status
lwip on | lwip status
database get | set | list
store list | install | remove | combo | …
```

---

## 4. PR 切分

| PR | 内容 |
|----|------|
| **C1** | 一级/二级分发 + 别名表；`help` 分组；缺二级打 usage |
| **C2** | 按本表挂 builtins / 任务内存 / 窗体 / 文件 / 数据库 / 网络 |
| **C3** | `store` 子词对齐（`installed` 等）；旧中横线子词变别名 |

---

## 5. 验收（见字知意）

```text
help
list
list tasks
list devices
make directory Apps/T
mkdir Apps/T
tcp listen 9000
tcplisten 9000
udp send 10.0.2.2 7 hello
show memory
test memory
database list
store list
store installed
execute HELLO.ELF
exec HELLO.ELF
```

课堂口述应对：  
「list tasks」= 列任务；「make directory」= 建目录；「tcp listen」= TCP 监听 —— 无需先背缩写。
