/*
 * Examples/Hello — CRT 最小示例（PR-A-examples）
 * 产物：MYAPP.ELF
 */
#include <stdio.h>
#include <toyos/version.h>

int main(void) {
    printf("ToyOS pkg template (CRT %s)\n", TOYOS_CRT_VERSION_STRING);
    return 0;
}
