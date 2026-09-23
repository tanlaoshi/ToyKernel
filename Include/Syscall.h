#ifndef SYSCALL_H
#define SYSCALL_H

#include "BootTypes.h"
#include "Hal.h"
#include "SyscallABI.h"

/* SYS_* 号段定义见 SyscallABI.h（段内双轨 + 预留区） */

/* SYS_WAIT：rdi = options；WNOHANG 时无已退出子进程则返回 0（不阻塞） */

/* SYS_EXECVE：rdi=path rsi=argv rdx=envp（envp 可忽略）；成功不返回 */
/* SYS_PIPE：rdi=int[2] 用户指针 → [0]读 [1]写；成功返回 0 */
/* SYS_DUP：rdi=fd → 新 fd（P2 仅管道） */
/* SYS_BRK：rdi=new_brk（0=查询）；成功返回当前/新 break，失败 -1 */
/* SYS_KILL：rdi=pid（与 fork 返回值一致 = 槽位+1）rsi=sig；成功 0，失败 -1 */
/* SYS_CREATE_WINDOW：rdi=title rsi=w rdx=h → wid；失败 -1（PR-G14） */
/* SYS_DAMAGE：rdi=wid rsi=text → 0；失败 -1 */
/* SYS_POLL_INPUT：rdi=wid → 0 无事件 / 1 已关窗（或无 USER 窗）/ 100+id 按钮 / 300+HID 键 / 400+x+(y<<10) 客户区点击 */
/* SYS_UI_BUTTON：rdi=wid rsi=button_id(0..3) rdx=label → 0；失败 -1（PR-G15） */
/* SYS_FILE_STAT：rdi=path rsi=TOY_FILE_STAT* → 0；失败 -1（PR-F4） */
/* SYS_OPEN_DIRECTORY：rdi=path → dirfd；失败 -1（PR-F4） */
/* SYS_READ_DIRECTORY：rdi=dirfd rsi=TOY_DIR_ENT* → 1 有项 / 0 结束 / -1 失败 */
/* SYS_MMAP：rdi=len rsi=prot rdx=flags → VA；匿名或文件私有（PR-U-mmap2）；失败 -1
 * 非匿名时 flags 高 16 位 = fd；offset 教学固定 0 */
/* SYS_MUNMAP：rdi=addr rsi=len → 0；失败 -1 */
/* SYS_DAMAGE_RECT：rdi=wid rsi=TOY_GFX_DAMAGE_RECT* → 0；失败 -1（PR-G-desk-3） */
/* SYS_LSEEK：rdi=fd rsi=signed off rdx=whence(0=SET/1=CUR/2=END) → 新 Pos；管道/套接字 -ESPIPE */
/* SYS_SLEEP：rdi=ms（上限 60000）；课堂 1 tick≈1ms；成功返回 0 */
/* SYS_CLOCK_MS：无参；返回 BSP 定时器 tick（与 sleep 同尺） */
/* SYS_SOCKET：rdi=domain(AF_INET=2) rsi=type rdx=protocol
 *   type=SOCK_STREAM(1)：rdx 忽略 → fd
 *   type=TOY_NET_SOCK_RESOLVE(0x100)：rdx=TOY_NET_DNS_QUERY* → 0；失败 -errno
 * SYS_CONNECT：rdi=fd rsi=ip(host-order u32) rdx=port
 * SYS_BIND：rdi=fd rsi=ip(0=INADDR_ANY) rdx=port
 * SYS_LISTEN：rdi=fd rsi=backlog
 * SYS_ACCEPT：rdi=listen_fd → 新 fd（阻塞至连接或超时）
 * socket fd 上 SYS_WRITE/SYS_READ = send/recv（默认 LWIP=1）
 */

void SyscallInit(void);
UINT64 SyscallDispatch(HAL_INTERRUPT_FRAME *Frame); /* int 0x80 与 SYSCALL 共用 */

#endif
