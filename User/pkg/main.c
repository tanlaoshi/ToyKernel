/*
 * pkg/main.c — 课外用户程序模板示例（PR-L1）
 * 构建：在 User/pkg 下 make（或 make -C User/pkg）
 * 产物：MYAPP.ELF → 拷到 ToyImage/rootfs/ 后 exec MYAPP.ELF
 */
#include <stdio.h>
#include <toyos/version.h>

int main(void) {
    printf("ToyOS pkg template (CRT %s)\n", TOYOS_CRT_VERSION_STRING);
    return 0;
}
