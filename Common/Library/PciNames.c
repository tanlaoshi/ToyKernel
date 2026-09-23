/*
 * PciNames.c — 课用 PCI 名称表（PR-DEV-names）
 * 覆盖 QEMU virtio / e1000 / xHCI / AHCI / NVMe；未命中返回 NULL。
 */
#include "PciNames.h"

typedef struct {
    UINT16      Id;
    const char *Name;
} PCI_VENDOR_ENTRY;

typedef struct {
    UINT16      Vendor;
    UINT16      Device;
    const char *Name;
} PCI_DEVICE_ENTRY;

typedef struct {
    UINT8       Class;
    UINT8       Subclass; /* 0xFF = 任意 */
    UINT8       ProgIf;   /* 0xFF = 任意 */
    const char *Name;
} PCI_CLASS_ENTRY;

static const PCI_VENDOR_ENTRY gVendors[] = {
    { 0x8086, "Intel" },
    { 0x1AF4, "Red Hat (VirtIO)" },
    { 0x1B36, "Red Hat (QEMU)" },
    { 0x1234, "QEMU" },
    { 0x10EC, "Realtek" },
    { 0x1022, "AMD" },
};

static const PCI_DEVICE_ENTRY gDevices[] = {
    /* VirtIO（legacy + modern） */
    { 0x1AF4, 0x1000, "VirtIO network" },
    { 0x1AF4, 0x1001, "VirtIO block" },
    { 0x1AF4, 0x1003, "VirtIO console" },
    { 0x1AF4, 0x1041, "VirtIO network (1.0)" },
    { 0x1AF4, 0x1042, "VirtIO block (1.0)" },
    { 0x1AF4, 0x1050, "VirtIO GPU" },
    /* Intel 网卡（课堂 + NUC） */
    { 0x8086, 0x100E, "Intel 82540EM (e1000)" },
    { 0x8086, 0x10D3, "Intel 82574L (e1000e)" },
    { 0x8086, 0x10F5, "Intel 82567LM" },
    { 0x8086, 0x156F, "Intel I219-LM" },
    /* 存储 / USB / 桥 */
    { 0x8086, 0x2922, "Intel ICH9 AHCI" },
    { 0x8086, 0x1E31, "Intel Panther Point xHCI" },
    { 0x8086, 0x1237, "Intel 440FX host bridge" },
    { 0x8086, 0x7000, "Intel PIIX3 ISA" },
    { 0x8086, 0x7010, "Intel PIIX3 IDE" },
    { 0x8086, 0x7113, "Intel PIIX4 ACPI" },
    /* QEMU 虚拟设备 */
    { 0x1B36, 0x000D, "QEMU xHCI" },
    { 0x1B36, 0x0010, "QEMU NVMe" },
    { 0x1234, 0x1111, "QEMU VGA (Bochs)" },
};

static const PCI_CLASS_ENTRY gClasses[] = {
    { 0x01, 0x06, 0xFF, "SATA AHCI" },
    { 0x01, 0x08, 0x02, "NVMe" },
    { 0x01, 0x08, 0xFF, "Non-Volatile memory" },
    { 0x01, 0xFF, 0xFF, "Mass storage" },
    { 0x02, 0xFF, 0xFF, "Network controller" },
    { 0x03, 0xFF, 0xFF, "Display controller" },
    { 0x06, 0x00, 0xFF, "Host bridge" },
    { 0x06, 0x01, 0xFF, "ISA bridge" },
    { 0x06, 0x04, 0xFF, "PCI-to-PCI bridge" },
    { 0x06, 0xFF, 0xFF, "Bridge" },
    { 0x0C, 0x03, 0x30, "USB xHCI" },
    { 0x0C, 0x03, 0x20, "USB EHCI" },
    { 0x0C, 0x03, 0xFF, "USB controller" },
    { 0x0C, 0xFF, 0xFF, "Serial bus" },
};

const char *PciGetVendorName(UINT16 Vendor) {
    UINTN i;

    for (i = 0; i < sizeof(gVendors) / sizeof(gVendors[0]); i++) {
        if (gVendors[i].Id == Vendor) {
            return gVendors[i].Name;
        }
    }
    return NULL;
}

const char *PciGetDeviceName(UINT16 Vendor, UINT16 Device) {
    UINTN i;

    for (i = 0; i < sizeof(gDevices) / sizeof(gDevices[0]); i++) {
        if (gDevices[i].Vendor == Vendor && gDevices[i].Device == Device) {
            return gDevices[i].Name;
        }
    }
    return NULL;
}

const char *PciGetClassName(UINT8 Class, UINT8 Subclass, UINT8 ProgIf) {
    UINTN i;

    for (i = 0; i < sizeof(gClasses) / sizeof(gClasses[0]); i++) {
        const PCI_CLASS_ENTRY *E = &gClasses[i];

        if (E->Class != Class) {
            continue;
        }
        if (E->Subclass != 0xFF && E->Subclass != Subclass) {
            continue;
        }
        if (E->ProgIf != 0xFF && E->ProgIf != ProgIf) {
            continue;
        }
        return E->Name;
    }
    return NULL;
}
