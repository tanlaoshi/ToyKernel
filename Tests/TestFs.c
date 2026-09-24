/*
 * TestFs.c — Vfs 契约 Host 单测。
 * 默认链 FsStub；FS=ram 时链 Student/FileSystemRam。
 */
#include "Vfs.h"

#include <stdio.h>
#include <string.h>

#ifdef TOY_FS_RAM
void RamFsReset(void);
const FS_OPS *RamFsOps(void);
#else
void FsStubReset(void);
const FS_OPS *FsStubOps(void);
#endif

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
#ifdef TOY_FS_RAM
    const FS_OPS *Ops = RamFsOps();
#else
    const FS_OPS *Ops = FsStubOps();
#endif
    char Buf[64];
    UINTN Sz = 0;
    FAT_FILE_STAT St;

#ifdef TOY_FS_RAM
    RamFsReset();
#else
    FsStubReset();
#endif
    Expect(VfsRegister(Ops) == 0, "register");
    Expect(VfsOps() == Ops, "ops after register");
    Expect(VfsSelect(Ops) == 0, "select");
    Expect(VfsMount(0) == FAT_OK, "mount");
    Expect(VfsWriteFile("HI.TXT", "hi", 2) == FAT_OK, "write");
    Expect(VfsReadFile("HI.TXT", Buf, sizeof(Buf), &Sz) == FAT_OK, "read");
    Expect(Sz == 2 && Buf[0] == 'h' && Buf[1] == 'i', "read data");
    Expect(VfsReadFile("NO.TXT", Buf, sizeof(Buf), &Sz) == FAT_ERR_NOENT, "noent");
#ifdef TOY_FS_RAM
    Expect(VfsFileStat("HI.TXT", &St) == FAT_OK && St.Size == 2, "stat");
    Expect(VfsDeleteFile("HI.TXT") == FAT_OK, "delete");
    Expect(VfsReadFile("HI.TXT", Buf, sizeof(Buf), &Sz) == FAT_ERR_NOENT, "gone");
#else
    (void)St;
    Expect(VfsFileStat("HI.TXT", &St) == FAT_ERR_IO, "null FileStat → IO");
#endif
    Expect(VfsReadFileAt("HI.TXT", 0, Buf, 1, &Sz) == FAT_ERR_INVAL,
           "null ReadFileAt → INVAL");
}

int main(void)
{
    TestRejectIncomplete();
    TestRegisterSelectRw();
    if (gFail) {
        return 1;
    }
#ifdef TOY_FS_RAM
    printf("fs ram: ok\n");
#else
    printf("fs: ok\n");
#endif
    return 0;
}
