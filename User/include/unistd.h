/*
 * unistd.h — read/write/close/execve/pipe/dup/fork/wait/brk/kill + 窗口（CRT2～G15）
 */
#ifndef UNISTD_H
#define UNISTD_H

#include <sys/types.h>
#include <toyos/syscall.h>

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int execve(const char *path, char *const argv[], char *const envp[]);
int pipe(int pipefd[2]);
int dup(int fd);
pid_t fork(void);
pid_t wait(int *status);
/* PR-P3：addr==0 查询；成功返回新/当前 break，失败 (void*)-1 */
void *brk(void *addr);
/* PR-P4：pid 与 fork 返回值一致；仅 SIGKILL/TERM/INT */
int kill(pid_t pid, int sig);
/* PR-G14/G15：用户态窗口协议（内核 Gui 后端） */
int create_window(const char *title, unsigned w, unsigned h);
int damage(int wid, const char *text);
int poll_input(int wid);
int ui_button(int wid, int button_id, const char *label);

#endif
