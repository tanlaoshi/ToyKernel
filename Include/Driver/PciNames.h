/*
 * PciNames.h — PCI Vendor/Device/Class 可读名（PR-DEV-names）
 * 未命中返回 NULL，调用方自行回退。
 */
#ifndef PCI_NAMES_H
#define PCI_NAMES_H

#include "BootTypes.h"

const char *PciGetVendorName(UINT16 Vendor);
const char *PciGetDeviceName(UINT16 Vendor, UINT16 Device);
const char *PciGetClassName(UINT8 Class, UINT8 Subclass, UINT8 ProgIf);

#endif
