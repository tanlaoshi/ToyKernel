/*
 * Ps2Private.h — i8042 键盘 + Aux 触控板（PR-H-ps2-aux）
 */
#ifndef PS2_PRIVATE_H
#define PS2_PRIVATE_H

#include "HalDevices.h"
#include "SpinLock.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define STATUS_OBF   (1u << 0)
#define STATUS_IBF   (1u << 1)
#define STATUS_MOUSE (1u << 5)

#define PS2_KBD_Q    16
#define PS2_MOUSE_Q  16
#define PS2_SCALE    2 /* 与 EHCI HID 相对鼠 ×2 对齐；略快可再降 */

extern int gPs2Ready;
extern int gPs2AuxReady;
extern SPIN_LOCK gPs2Lock;

int Ps2StatusLooksDead(UINT8 St);
void Ps2WaitIbFree(void);
void Ps2WaitOb(void);
void Ps2CtrlCmd(UINT8 Cmd);
void Ps2DataWrite(UINT8 Data);
int Ps2DataRead(UINT8 *Out);
void Ps2DrainOb(int Max);
int Ps2ExpectAck(void);
int Ps2AuxWrite(UINT8 Data);
int Ps2AuxReadByte(UINT8 *Out);
int Ps2InitHw(void);
void Ps2KbdPortEnable(int On);

void Ps2KbdResetState(void);
void Ps2KbdFeed(UINT8 B);
int Ps2KbdDequeue(HAL_KEYBOARD_REPORT *Report);

void Ps2AuxResetState(void);
void Ps2AuxFeed(UINT8 B);
int Ps2AuxPresent(void);
int Ps2AuxDequeue(HAL_MOUSE_REPORT *Report);
void Ps2AuxHandoffDesktop(UINT32 CursorX, UINT32 CursorY);
int Ps2AuxInitDevice(void);
void Ps2DiagFormat(char *Buf, int Max);
int Ps2AuxRetry(void);
UINT32 Ps2AuxPktCount(void);
UINT32 Ps2AuxByteCount(void);
extern const char *gPs2Fail;
extern UINT8 gPs2LastRx;
extern UINT8 gPs2DevId;

#endif
