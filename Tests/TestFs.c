/*
 * TestFs.c — Vfs 契约 Host 单测（Synthetic stub；不链真实 FAT）。
 */
#include "Vfs.h"

#include <stdio.h>
#include <string.h>

void FsStubReset(void);
const FS_OPS *FsStubOps(void);

static int gFail;

static void Expect(int Cond, const char *Msg)
{
    if (!Cond) {
        printf("fail %s\n", Msg);
        gFail = 1;
    }
}

static void TestRejectIncomplete(void)
{
    FS_OPS Bad;

    memset(&Bad, 0, sizeof(Bad));
    Bad.Name = "bad";
    Expect(VfsRegister(&Bad) != 0, "reject missing Mount/Read/Write");
}

static void TestRegisterSelectRw(void)
{
    const FS_OPS *Ops = FsStubOps();
    char Buf[64];
    UINTN Sz = 0;

    FsStubReset();
    Expect(VfsRegister(Ops) == 0, "register stub");
    Expect(VfsOps() == Ops, "ops after register");
    Expect(VfsSelect(Ops) == 0, "select stub");
    Expect(VfsMount(0) == FAT_OK, "mount");
    Expect(VfsWriteFile("HI.TXT", "hi", 2) == FAT_OK, "write");
    Expect(VfsReadFile("HI.TXT", Buf, sizeof(Buf), &Sz) == FAT_OK, "read");
    Expect(Sz == 2 && Buf[0] == 'h' && Buf[1] == 'i', "read data");
}

static void TestNoentAndOptionalNull(void)
{
    char Buf[8];
    UINTN Sz = 0;
    FAT_FILE_STAT St;

    Expect(VfsReadFile("NO.TXT", Buf, sizeof(Buf), &Sz) == FAT_ERR_NOENT, "noent");
    Expect(VfsFileStat("HI.TXT", &St) == FAT_ERR_IO, "null FileStat → IO");
    Expect(VfsReadFileAt("HI.TXT", 0, Buf, 1, &Sz) == FAT_ERR_INVAL,
           "null ReadFileAt → INVAL");
}

int main(void)
{
    TestRejectIncomplete();
    TestRegisterSelectRw();
    TestNoentAndOptionalNull();
    if (gFail) {
        return 1;
    }
    printf("fs: ok\n");
    return 0;
}
