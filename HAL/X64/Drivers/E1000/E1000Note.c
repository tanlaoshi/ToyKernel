/*
 * E1000Note.c — PR-N-i219-note：只读现场 dump（不加 DID、不改 TX/PHY）
 *
 * 扫 PCI Intel 网卡 class；对照现网 DID 表；若已 Bind 再打 LU/MAC/环指针。
 */
#include "E1000.h"
#include "E1000Private.h"
#include "PCIe.h"
#include "Hal.h"

typedef void (*NOTE_WRITE)(const char *Text);

static void WHex8(NOTE_WRITE Write, UINT8 V) {
    char Buf[3];
    static const char Dig[] = "0123456789ABCDEF";

    Buf[0] = Dig[(V >> 4) & 0xF];
    Buf[1] = Dig[V & 0xF];
    Buf[2] = 0;
    Write(Buf);
}

static void WHex16(NOTE_WRITE Write, UINT16 V) {
    char Buf[5];
    static const char Dig[] = "0123456789ABCDEF";

    Buf[0] = Dig[(V >> 12) & 0xF];
    Buf[1] = Dig[(V >> 8) & 0xF];
    Buf[2] = Dig[(V >> 4) & 0xF];
    Buf[3] = Dig[V & 0xF];
    Buf[4] = 0;
    Write(Buf);
}

static void WHex32(NOTE_WRITE Write, UINT32 V) {
    char Buf[9];
    static const char Dig[] = "0123456789ABCDEF";
    int i;

    for (i = 0; i < 8; i++) {
        Buf[i] = Dig[(V >> (28 - i * 4)) & 0xF];
    }
    Buf[8] = 0;
    Write(Buf);
}

static void WDec(NOTE_WRITE Write, UINT32 V) {
    char Buf[12];
    int N = 0;
    UINT32 T = V;
    int i;

    if (V == 0) {
        Write("0");
        return;
    }
    while (T > 0 && N < 11) {
        Buf[N++] = (char)('0' + (T % 10u));
        T /= 10u;
    }
    for (i = N - 1; i >= 0; i--) {
        char One[2];

        One[0] = Buf[i];
        One[1] = 0;
        Write(One);
    }
}

/* 与 E1000Probe.c gE1000Ids 同步；本刀不加新 DID */
static int DidInDriverTable(UINT16 Did) {
    static const UINT16 Ids[] = {
        E1000_DID_82540EM,
        0x100F,
        E1000_DID_82574L,
        0x10F5,
        E1000_DID_I219_LM,
        0
    };
    int i;

    for (i = 0; Ids[i] != 0; i++) {
        if (Ids[i] == Did) {
            return 1;
        }
    }
    return 0;
}

static int IsIntelNetwork(UINT16 Vid, UINT32 ClassDw) {
    UINT8 Base = (UINT8)((ClassDw >> 24) & 0xFF);

    return (Vid == E1000_VENDOR && Base == 0x02) ? 1 : 0;
}

static void DumpBound(NOTE_WRITE Write) {
    int Up = 0;
    UINT32 Mbps = 0;
    int Fd = 0;
    int i;

    Write("e1000: bound=yes did=0x");
    WHex16(Write, gPciDid);
    Write(" bdf=");
    WDec(Write, gPciBus);
    Write(":");
    WDec(Write, gPciDev);
    Write(".");
    WDec(Write, gPciFn);
    Write(" chip=");
    Write(E1000ChipName());
    Write(gE1000UseIrq ? " irq=msi\n" : " irq=poll\n");

    Write("e1000: mac=");
    for (i = 0; i < 6; i++) {
        WHex8(Write, gE1000Mac[i]);
        if (i < 5) {
            Write(":");
        }
    }
    Write("\n");

    if (E1000GetLink(&Up, &Mbps, &Fd) == 0) {
        Write("e1000: LU=");
        Write(Up ? "1" : "0");
        Write(" Mbps=");
        WDec(Write, Mbps);
        Write(Fd ? " FD\n" : " HD\n");
    }

    if (gBar) {
        UINT32 Ral = MmioR32(E1000_REG_RAL);
        UINT32 Rah = MmioR32(E1000_REG_RAH);

        Write("e1000: STATUS=0x");
        WHex32(Write, MmioR32(E1000_REG_STATUS));
        Write(" TCTL=0x");
        WHex32(Write, MmioR32(E1000_REG_TCTL));
        Write("\n");
        Write("e1000: TDH=0x");
        WHex32(Write, MmioR32(E1000_REG_TDH));
        Write(" TDT=0x");
        WHex32(Write, MmioR32(E1000_REG_TDT));
        Write(" RDH=0x");
        WHex32(Write, MmioR32(E1000_REG_RDH));
        Write(" RDT=0x");
        WHex32(Write, MmioR32(E1000_REG_RDT));
        Write("\n");
        /* PR-N-i219-mac：RAL 对照软件 MAC */
        Write("e1000: RAL=0x");
        WHex32(Write, Ral);
        Write(" RAH=0x");
        WHex32(Write, Rah);
        Write("\n");
        /* PR-N-i219-txdiag */
        Write("e1000: tx_ok=0x");
        WHex32(Write, gE1000TxOk);
        Write(" tx_fail=0x");
        WHex32(Write, gE1000TxFail);
        Write(" last_rc=");
        if (gE1000TxLastRc < 0) {
            Write("-");
            WDec(Write, (UINT32)(-gE1000TxLastRc));
        } else {
            WDec(Write, (UINT32)gE1000TxLastRc);
        }
        Write(" (0=ok -1=arg -2=desc -3=DD)\n");
    }
}

void E1000DumpNote(void (*Write)(const char *Text)) {
    int B;
    int D;
    int F;
    int Found = 0;
    int InTable = 0;
    int Bound = E1000Ready();

    if (!Write) {
        return;
    }

    Write("=== e1000 note (PR-N-i219-note; no DID change) ===\n");
    Write("driver table: 100E 100F 10D3 10F5 156F (I219-LM)\n");

    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT16 Vid = (UINT16)(VidDid & 0xFFFF);
                UINT16 Did = (UINT16)(VidDid >> 16);
                UINT32 ClassDw;
                int Known;

                if (Vid == 0xFFFF) {
                    continue;
                }
                ClassDw = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x08);
                if (!IsIntelNetwork(Vid, ClassDw)) {
                    continue;
                }
                Found++;
                Known = DidInDriverTable(Did);
                if (Known) {
                    InTable++;
                }
                Write("pci 8086:");
                WHex16(Write, Did);
                Write(" @");
                WDec(Write, (UINT32)B);
                Write(":");
                WDec(Write, (UINT32)D);
                Write(".");
                WDec(Write, (UINT32)F);
                Write(" class=0x");
                WHex32(Write, ClassDw);
                Write(Known ? " in_table=yes\n" : " in_table=no\n");
            }
        }
    }

    if (Found == 0) {
        Write("pci: no Intel network class (02xxxx)\n");
    }

    if (Bound) {
        DumpBound(Write);
    } else {
        Write("e1000: bound=no (Setup soft-fail or DID not in table)\n");
    }

    Write("hint: ");
    if (Found == 0) {
        Write("no Intel NIC → stop / wrong machine\n");
    } else if (!Bound && Found > InTable) {
        Write("saw Intel NIC outside table → next PR-N-i219-did (exact DID)\n");
    } else if (Bound) {
        Write("bound → ping/ARP 后看 tx_ok/last_rc；TDH=TDT=0 且 tx_ok=0 → 未进 Send\n");
    } else {
        Write("in table but unbound → link timeout or Setup fail; note STATUS\n");
    }
}
