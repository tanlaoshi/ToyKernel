/*
 * InputPs2.h — i8042 PS/2 键盘 + Aux 触控板（PR-H2 / PR-H-ps2-aux）
 */
#ifndef INPUT_PS2_H
#define INPUT_PS2_H

#include "BootTypes.h"

void InputPs2Register(void);
void Ps2MouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY);
void Ps2DiagFormat(char *Buf, int Max);
int Ps2AuxRetry(void);

#endif
