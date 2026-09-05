/*
 * dirdemo.c — PR-F4：OpenDirectory / ReadDirectory / FileStat 冒烟
 */
#include <stdio.h>
#include <dirent.h>
#include <string.h>

int main(void) {
    TOY_DIR *Dir;
    TOY_DIR_ENT Ent;
    TOY_FILE_STAT St;
    int Count = 0;
    int SawHello = 0;
    int Rc;

    if (FileStat("HELLO.ELF", &St) != 0) {
        printf("dirdemo: FAIL FileStat HELLO.ELF\n");
        return 1;
    }
    if (St.Attr & TOY_ATTR_DIR) {
        printf("dirdemo: FAIL HELLO.ELF marked dir\n");
        return 1;
    }
    if (St.Size == 0) {
        printf("dirdemo: FAIL HELLO.ELF size 0\n");
        return 1;
    }
    printf("filestat HELLO size=%u\n", St.Size);

    if (FileStat("", &St) != 0) {
        printf("dirdemo: FAIL FileStat root\n");
        return 1;
    }
    if ((St.Attr & TOY_ATTR_DIR) == 0) {
        printf("dirdemo: FAIL root not dir\n");
        return 1;
    }

    Dir = OpenDirectory("");
    if (!Dir) {
        printf("dirdemo: FAIL OpenDirectory root\n");
        return 1;
    }
    while ((Rc = ReadDirectory(Dir, &Ent)) > 0) {
        Count++;
        if (strcmp(Ent.Name, "HELLO.ELF") == 0) {
            SawHello = 1;
        }
        if (Count <= 4) {
            printf("ent %s%s\n", Ent.Name,
                   (Ent.Attr & TOY_ATTR_DIR) ? "/" : "");
        }
    }
    if (Rc < 0) {
        printf("dirdemo: FAIL ReadDirectory\n");
        CloseDirectory(Dir);
        return 1;
    }
    CloseDirectory(Dir);
    if (Count < 1 || !SawHello) {
        printf("dirdemo: FAIL root list count=%d hello=%d\n", Count, SawHello);
        return 1;
    }

    Dir = OpenDirectory("RES:");
    if (!Dir) {
        printf("dirdemo: FAIL OpenDirectory RES:\n");
        return 1;
    }
    Count = 0;
    while ((Rc = ReadDirectory(Dir, &Ent)) > 0) {
        Count++;
    }
    CloseDirectory(Dir);
    if (Rc < 0 || Count < 1) {
        printf("dirdemo: FAIL RES: list\n");
        return 1;
    }

    Dir = OpenDirectory("HELLO.ELF");
    if (Dir) {
        CloseDirectory(Dir);
        printf("dirdemo: FAIL OpenDirectory on file\n");
        return 1;
    }

    printf("dirdemo: ok\n");
    return 0;
}
