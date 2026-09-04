#ifndef SYSCALL_H
#define SYSCALL_H

#include "BootTypes.h"
#include "Hal.h"

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_READ    3
#define SYS_CLOSE   4
#define SYS_FORK    5
#define SYS_WAIT    6
#define SYS_YIELD   7
#define SYS_SOCKET  8
#define SYS_CONNECT 9
#define SYS_BIND    10
#define SYS_LISTEN  11
#define SYS_ACCEPT  12
#define SYS_EXECVE  13
#define SYS_PIPE    14
#define SYS_DUP     15

/* SYS_WAIT：rdi = options；WNOHANG 时无已退出子进程则返回 0（不阻塞） */
#define WNOHANG 1

/* SYS_EXECVE：rdi=path rsi=argv rdx=envp（envp 可忽略）；成功不返回 */
/* SYS_PIPE：rdi=int[2] 用户指针 → [0]读 [1]写；成功返回 0 */
/* SYS_DUP：rdi=fd → 新 fd（P2 仅管道） */
/* SYS_SOCKET：rdi=domain(AF_INET=2) rsi=type(SOCK_STREAM=1) rdx=protocol
 * SYS_CONNECT：rdi=fd rsi=ip(host-order u32) rdx=port
 * SYS_BIND：rdi=fd rsi=ip(0=INADDR_ANY) rdx=port
 * SYS_LISTEN：rdi=fd rsi=backlog
 * SYS_ACCEPT：rdi=listen_fd → 新 fd（阻塞至连接或超时）
 * socket fd 上 SYS_WRITE/SYS_READ = send/recv（需 LWIP=1）
 */

void SyscallInit(void);
UINT64 SyscallDispatch(HAL_FRAME *Frame); /* int 0x80 与 SYSCALL 共用 */

#endif
