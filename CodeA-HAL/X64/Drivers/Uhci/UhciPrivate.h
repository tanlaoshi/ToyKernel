/*
 * UhciPrivate.h — UHCI 1.1 I/O 寄存器（PR-H-uhci-1）
 */
#ifndef UHCI_PRIVATE_H
#define UHCI_PRIVATE_H

#include "Uhci.h"
#include "PCIe.h"
#include "Hal.h"

#define UHCI_PROG_IF           0x00u
#define UHCI_MAX_CTRL          4
#define UHCI_NPORTS            2u

#define UHCI_USBCMD            0x00u
#define UHCI_USBSTS            0x02u
#define UHCI_USBINTR           0x04u
#define UHCI_FRNUM             0x06u
#define UHCI_FLBASEADD         0x08u
#define UHCI_SOF               0x0Cu
#define UHCI_PORTSC(N)         (0x10u + 2u * ((N) - 1u))

#define UHCI_CMD_RS            (1u << 0)
#define UHCI_CMD_HCRESET       (1u << 1)
#define UHCI_CMD_GRESET        (1u << 2)
#define UHCI_CMD_MAXP          (1u << 7)

#define UHCI_PORT_CCS          (1u << 0)
#define UHCI_PORT_CSC          (1u << 1)
#define UHCI_PORT_PE           (1u << 2)
#define UHCI_PORT_PEDC         (1u << 3)
#define UHCI_PORT_RESET        (1u << 9)

#define UHCI_LINK_TERMINATE    1u
#define UHCI_FRAME_ENTRIES     1024u

typedef struct {
    USB_CONTROLLER Pci;
    UINT16 IoBase;
    UINT8 NPorts;
    int Up;
    UINT32 CcsMask;
    UINT32 *FrameList;
} UHCI_CTRL;

extern UHCI_CTRL gUhci[UHCI_MAX_CTRL];
extern int gUhciCount;
extern int gUhciReady;
extern UINT32 gUhciCcsOr;

static inline UINT16 UhciR16(UINT16 Base, UINT16 Off) {
    return HalIoRead16((UINT16)(Base + Off));
}

static inline void UhciW16(UINT16 Base, UINT16 Off, UINT16 V) {
    HalIoWrite16((UINT16)(Base + Off), V);
}

static inline UINT32 UhciR32(UINT16 Base, UINT16 Off) {
    return HalIoRead32((UINT16)(Base + Off));
}

static inline void UhciW32(UINT16 Base, UINT16 Off, UINT32 V) {
    HalIoWrite32((UINT16)(Base + Off), V);
}

void UhciDelay(int Loops);
void UhciSurveyCcs(UHCI_CTRL *C);
int UhciInitOne(UHCI_CTRL *C, int Index);

#endif
