/*
 * Examples/Dir — OpenDirectory / ReadDirectory（PR-A-examples）
 * 产物：DIR.ELF。返回 TOY_DIR *，不是 int fd。
 */
#include <stdio.h>
#include <dirent.h>

int main(void) {
    TOY_DIR *Dir;
    TOY_DIR_ENT Ent;
    int Rc;

    Dir = OpenDirectory("");
    if (!Dir) {
        printf("dir: OpenDirectory failed\n");
        return 1;
    }
    while ((Rc = ReadDirectory(Dir, &Ent)) > 0) {
        printf("  %s%s\n", Ent.Name, (Ent.Attr & TOY_ATTR_DIR) ? "/" : "");
    }
    CloseDirectory(Dir);
    return 0;
}
