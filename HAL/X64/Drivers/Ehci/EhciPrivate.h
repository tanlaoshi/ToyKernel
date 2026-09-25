/*
 * EhciPrivate.h — EHCI 寄存器（PR-H-ehci-1）
 *
 * 对照 EHCI 1.0 §2；不搬 Linux ehci-hcd。
 */
#ifndef EHCI_PRIVATE_H
#define EHCI_PRIVATE_H

#include "Ehci.h"
#include "PCIe.h"

#define EHCI_PROG_IF           0x20u

/* Capability */
#define EHCI_CAPLENGTH         0x00u
#define EHCI_HCSPARAMS         0x04u
#define EHCI_HCCPARAMS         0x08u

#define EHCI_HCS_N_PORTS(V)    ((V) & 0x0Fu)
#define EHCI_HCC_EECP(V)       (((V) >> 8) & 0xFFu)

/* Operational (相对 OpBase) */
#define EHCI_USBCMD            0x00u
#define EHCI_USBSTS            0x04u
#define EHCI_CONFIGFLAG        0x40u
#define EHCI_PORTSC(N)         (0x44u + 4u * ((N) - 1u))

#define EHCI_CMD_RS            (1u << 0)
#define EHCI_CMD_HCRESET       (1u << 1)

#define EHCI_STS_HCHALTED      (1u << 12)

#define EHCI_PORT_CCS          (1u << 0)
#define EHCI_PORT_CSC          (1u << 1)  /* R/WC */
#define EHCI_PORT_PED          (1u << 2)
#define EHCI_PORT_PEDC         (1u << 3)  /* R/WC */
#define EHCI_PORT_OCC          (1u << 5)  /* R/WC over-current change */
#define EHCI_PORT_PP           (1u << 12)

/* PCI USBLEGSUP（EECP）：bit16=BIOS owned，bit24=OS owned */
#define EHCI_LEGSUP_BIOS       (1u << 16)
#define EHCI_LEGSUP_OS         (1u << 24)

#define EHCI_MAX_CTRL          4
#define EHCI_BAR_MAP           0x1000u

typedef struct {
    USB_CONTROLLER Pci;
    volatile UINT8 *Cap;
    volatile UINT8 *Op;
    UINT8 CapLen;
    UINT8 NPorts;
    UINT32 CcsMask;
    int Up;
} EHCI_CTRL;

extern EHCI_CTRL gEhci[EHCI_MAX_CTRL];
extern int gEhciCount;
extern int gEhciReady;
extern UINT32 gEhciCcsOr;

static inline UINT32 EhciR32(volatile UINT8 *Base, UINT32 Off) {
    return *(volatile UINT32 *)(Base + Off);
}

static inline void EhciW32(volatile UINT8 *Base, UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(Base + Off) = Val;
}

int EhciInitOne(EHCI_CTRL *C, int Index);
void EhciSurveyCcs(EHCI_CTRL *C);

#endif
