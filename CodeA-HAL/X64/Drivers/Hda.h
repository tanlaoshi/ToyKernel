/*
 * Hda.h — Intel HD Audio（PR-G-audio）
 */
#ifndef HDA_H
#define HDA_H

#include "BootTypes.h"

void HdaDriverRegister(void);

/* 1=已认到 HDA PCI；尚未表示可播放 */
int HdaProbed(void);
UINT16 HdaPciDid(void);
UINT8 HdaPciBus(void);
UINT8 HdaPciDev(void);
UINT8 HdaPciFn(void);

/* PR-G-audio-1：VMM 后映 BAR0；只读指纹 */
int HdaMmioInit(void);
int HdaMmioOk(void);
UINT64 HdaMmioBarPhys(void);
volatile UINT8 *HdaMmioBase(void);
UINTN HdaMmioMapBytes(void);
UINT32 HdaMmioRead32(UINT32 Off);
UINT16 HdaMmioRead16(UINT32 Off);
UINT8 HdaMmioRead8(UINT32 Off);
void HdaMmioWrite32(UINT32 Off, UINT32 Val);
void HdaMmioWrite16(UINT32 Off, UINT16 Val);
void HdaMmioWrite8(UINT32 Off, UINT8 Val);

/* PR-G-audio-2：CORB/RIRB + codec 枚举（poll，无 IRQ） */
int HdaCorbInit(void);
int HdaCorbOk(void);
int HdaCorbVerb(UINT8 Cad, UINT8 Nid, UINT32 VerbPayload, UINT32 *RespOut);
int HdaCodecInit(void);
int HdaCodecOk(void);
UINT8 HdaCodecAddr(void);     /* 选用的 codec CAD；无则 0xFF */
UINT8 HdaCodecAfg(void);      /* AFG nid */
UINT8 HdaCodecOutPins(void);  /* 输出 pin 计数 */
UINT8 HdaCodecFirstOutNid(void);

/* PR-G-audio-3：输出 Stream + BDL；开机短 PCM（HDMI） */
int HdaStreamInit(void);
int HdaStreamOk(void);
/* Intel HDMI/DP：开全 pin、选路、DIP InfoFrame、显示侧 AUD */
int HdaHdmiEnableAllPins(UINT8 Cad, UINT8 Afg);
int HdaHdmiPickPath(UINT8 Cad, UINT8 *PinOut, UINT8 *CvtOut);
int HdaHdmiInfoframe(UINT8 Cad, UINT8 Pin);
int HdaHdmiDisplayAudioEnable(void);
/* PR-G-audio-4：HalAudio 底层；Samples=0 则内置方波蜂鸣 */
int HdaAudioProbe(void);
int HdaAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits);
void HdaAudioStop(void);

#endif
