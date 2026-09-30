/*
 * Install.c — PR-FS-inst-1：Guest 安装器骨架
 *
 * 布局对齐 ToyImage/make-usb-stick.sh：GPT | ESP 256MiB | TOYOS 剩余。
 * 最小文件：ESP:EFI/BOOT/BOOTX64.EFI；TOYOS:Kernel.elf、TOYOS.ID、THEME.CFG。
 */
#include "Install.h"
#include "Block.h"
#include "Gpt.h"
#include "Fat.h"
#include "FileSystem.h"
#include "PhysicalMemory.h"
#include "Debug.h"
#include "Console.h"

#define INSTALL_COPY_MAX (2u * 1024u * 1024u)

static int LoadFile(const char *Path, UINT8 **OutBuf, UINTN *OutSize, UINT32 PagesHint) {
    UINT8 *Buf;
    UINTN Size = 0;
    UINT32 Pages;
    int Err;

    Pages = PagesHint ? PagesHint : ((INSTALL_COPY_MAX + 4095u) / 4096u);
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    Err = FileSystemReadFile(Path, Buf, (UINTN)Pages * 4096u, &Size);
    if (Err != FAT_OK || Size == 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    *OutBuf = Buf;
    *OutSize = Size;
    return (int)Pages;
}

static int WritePartFile(UINT32 PartLba, const char *Path, const void *Data, UINTN Size) {
    int Err;

    if (!BlockFlush()) {
        return -1;
    }
    Err = FatInit(PartLba);
    if (Err != FAT_OK) {
        return Err;
    }
    return FatWriteFile(Path, Data, Size);
}

static int EnsureDir(UINT32 PartLba, const char *Path) {
    int Err = FatInit(PartLba);
    if (Err != FAT_OK) {
        return Err;
    }
    Err = FatMkdir(Path);
    if (Err == FAT_OK || Err == FAT_ERR_EXIST) {
        return FAT_OK;
    }
    return Err;
}

int InstallListDisks(void (*PrintLine)(UINT32 Drive, int Ready)) {
    UINT32 d;
    int N = 0;

    if (!PrintLine) {
        return 0;
    }
    for (d = 0; d < BLOCK_MAX_DRIVES; d++) {
        int Ready = BlockDriveReady(d);
        PrintLine(d, Ready);
        if (Ready) {
            N++;
        }
    }
    return N;
}

int InstallToDrive(UINT32 Drive, UINT64 TotalSectors, UINT32 EspMib, int Yes) {
    UINT32 EspLba = 0;
    UINT32 EspSec = 0;
    UINT32 ToyLba = 0;
    UINT32 ToySec = 0;
    UINT8 *Boot = 0;
    UINT8 *Kern = 0;
    UINT8 *Theme = 0;
    UINT8 *Id = 0;
    UINTN BootSz = 0;
    UINTN KernSz = 0;
    UINTN ThemeSz = 0;
    UINTN IdSz = 0;
    int BootPages = 0;
    int KernPages = 0;
    int ThemePages = 0;
    int IdPages = 0;
    int Err;
    UINT32 SavedDrive;
    static const char DefaultId[] = "ToyOS root volume\n";

    if (Drive >= BLOCK_MAX_DRIVES || !BlockDriveReady(Drive)) {
        ConsoleWrite("install: drive not ready\n");
        return -1;
    }
    if (EspMib == 0) {
        EspMib = 256;
    }
    if (TotalSectors < 512ull * 2048ull) {
        ConsoleWrite("install: need >=512MiB (--mib)\n");
        return -1;
    }
    if (!Yes) {
        ConsoleWrite("install: dry-run drive=");
        ConsoleWriteHex32(Drive);
        ConsoleWrite(" sectors=");
        ConsoleWriteHex32((UINT32)TotalSectors);
        ConsoleWrite(" esp=");
        ConsoleWriteHex32(EspMib);
        ConsoleWrite("MiB (pass --yes to wipe)\n");
        return 0;
    }

    /* 先从当前卷读入载荷，再改目标盘 */
    BootPages = LoadFile("ESP:EFI/BOOT/BOOTX64.EFI", &Boot, &BootSz, 8);
    if (BootPages < 0) {
        BootPages = LoadFile("EFI/BOOT/BOOTX64.EFI", &Boot, &BootSz, 8);
    }
    if (BootPages < 0) {
        ConsoleWrite("install: missing BOOTX64.EFI\n");
        return -1;
    }
    KernPages = LoadFile("Kernel.elf", &Kern, &KernSz, (INSTALL_COPY_MAX / 4096u));
    if (KernPages < 0) {
        KernPages = LoadFile("TOYOS:Kernel.elf", &Kern, &KernSz, (INSTALL_COPY_MAX / 4096u));
    }
    if (KernPages < 0) {
        ConsoleWrite("install: missing Kernel.elf\n");
        goto fail;
    }
    ThemePages = LoadFile("THEME.CFG", &Theme, &ThemeSz, 1);
    if (ThemePages < 0) {
        ThemePages = LoadFile("TOYOS:THEME.CFG", &Theme, &ThemeSz, 1);
    }
    IdPages = LoadFile("TOYOS.ID", &Id, &IdSz, 1);
    if (IdPages < 0) {
        IdPages = LoadFile("TOYOS:TOYOS.ID", &Id, &IdSz, 1);
    }
    if (IdPages < 0) {
        Id = (UINT8 *)(UINTN)DefaultId;
        IdSz = sizeof(DefaultId) - 1;
        IdPages = 0;
    }

    SavedDrive = BlockCurrentDrive();
    if (!BlockSelect(Drive)) {
        ConsoleWrite("install: BlockSelect fail\n");
        goto fail;
    }

    ConsoleWrite("install: writing GPT...\n");
    if (!GptWriteToyLayout(TotalSectors, EspMib, &EspLba, &EspSec, &ToyLba, &ToySec)) {
        ConsoleWrite("install: GPT fail\n");
        goto fail_sel;
    }

    ConsoleWrite("install: format ESP...\n");
    Err = FatFormatFat32(EspLba, EspSec, "ESP");
    if (Err != FAT_OK) {
        ConsoleWrite("install: ESP format fail\n");
        goto fail_sel;
    }
    ConsoleWrite("install: format TOYOS...\n");
    Err = FatFormatFat32(ToyLba, ToySec, "TOYOS");
    if (Err != FAT_OK) {
        ConsoleWrite("install: TOYOS format fail\n");
        goto fail_sel;
    }

    if (EnsureDir(EspLba, "EFI") != FAT_OK || EnsureDir(EspLba, "EFI/BOOT") != FAT_OK) {
        ConsoleWrite("install: mkdir EFI/BOOT fail\n");
        goto fail_sel;
    }
    Err = WritePartFile(EspLba, "EFI/BOOT/BOOTX64.EFI", Boot, BootSz);
    if (Err != FAT_OK) {
        ConsoleWrite("install: write BOOTX64 fail\n");
        goto fail_sel;
    }
    Err = WritePartFile(ToyLba, "TOYOS.ID", Id, IdSz);
    if (Err != FAT_OK) {
        ConsoleWrite("install: write TOYOS.ID fail\n");
        goto fail_sel;
    }
    Err = WritePartFile(ToyLba, "Kernel.elf", Kern, KernSz);
    if (Err != FAT_OK) {
        ConsoleWrite("install: write Kernel.elf fail\n");
        goto fail_sel;
    }
    if (Theme && ThemeSz > 0) {
        (void)WritePartFile(ToyLba, "THEME.CFG", Theme, ThemeSz);
    }

    (void)BlockFlush();
    (void)BlockSelect(SavedDrive);
    (void)FileSystemRemountVolumes();

    ConsoleWrite("install: ok drive=");
    ConsoleWriteHex32(Drive);
    ConsoleWrite(" ESP@");
    ConsoleWriteHex32(EspLba);
    ConsoleWrite(" TOYOS@");
    ConsoleWriteHex32(ToyLba);
    ConsoleWrite("\n");

    if (BootPages > 0) {
        PhysicalMemoryFreePages(Boot, (UINT32)BootPages);
    }
    if (KernPages > 0) {
        PhysicalMemoryFreePages(Kern, (UINT32)KernPages);
    }
    if (ThemePages > 0) {
        PhysicalMemoryFreePages(Theme, (UINT32)ThemePages);
    }
    if (IdPages > 0) {
        PhysicalMemoryFreePages(Id, (UINT32)IdPages);
    }
    return 0;

fail_sel:
    (void)BlockSelect(SavedDrive);
fail:
    if (BootPages > 0) {
        PhysicalMemoryFreePages(Boot, (UINT32)BootPages);
    }
    if (KernPages > 0) {
        PhysicalMemoryFreePages(Kern, (UINT32)KernPages);
    }
    if (ThemePages > 0) {
        PhysicalMemoryFreePages(Theme, (UINT32)ThemePages);
    }
    if (IdPages > 0) {
        PhysicalMemoryFreePages(Id, (UINT32)IdPages);
    }
    return -1;
}
