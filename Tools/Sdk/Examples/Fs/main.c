/*
 * Examples/Fs — libFsUtil 路径拼接 / 列目录 / TOYOS: 前缀（PR-A-fsutil）
 * 产物：FSUTIL.ELF。底层仍 OpenDirectory / FileStat。
 */
#include <stdio.h>
#include <FsUtil.h>

int main(void) {
    char Path[TOY_FS_PATH_MAX];
    TOY_DIR_ENT Ents[8];
    int N;
    int I;

    printf("Fs: FsUtil %s\n", FS_UTIL_ABI_VERSION_STRING);

    if (FsUtilToyosPath(Path, sizeof(Path), "") != 0) {
        printf("Fs: toyos path fail\n");
        return 1;
    }
    N = FsUtilListDir(Path, Ents, 8);
    if (N < 0) {
        printf("Fs: list %s fail\n", Path);
        return 1;
    }
    printf("Fs: %s count=%d\n", Path, N);
    for (I = 0; I < N && I < 4; I++) {
        printf("  %s%s\n", Ents[I].Name,
               (Ents[I].Attr & TOY_ATTR_DIR) ? "/" : "");
    }

    if (FsUtilJoin(Path, sizeof(Path), "TOYOS:", "NOTE.TXT") != 0) {
        printf("Fs: join fail\n");
        return 1;
    }
    printf("Fs: join %s\n", Path);
    return 0;
}
