/*
 * LibWrite.h — PR-R4：Library 文本输出槽（不 #include Console）
 *
 * ConsoleInit 注册 ConsoleWrite，供 Fat/Res ListDir 等用户可见列举。
 * 未注册时回退 HalDebugWrite（仅串口）。
 */
#ifndef LIB_WRITE_H
#define LIB_WRITE_H

void LibWriteRegister(void (*Write)(const char *Text));
void LibWrite(const char *Text);

#endif
