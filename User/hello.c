/*
 * hello.c — CRT：C + printf/malloc（HELLO.ELF）
 * 旧 int 0x80 汇编见 hello_int80.S
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *p = (char *)malloc(8);
    if (p) {
        memcpy(p, "ok", 3);
        printf("Hello Ring3! (%s)\n", p);
    } else {
        printf("Hello Ring3!\n");
    }
    return 0;
}
