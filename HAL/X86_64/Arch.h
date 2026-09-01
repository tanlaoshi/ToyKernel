/*
 * Arch.h — x86-64 架构相关接口
 *
 * 中断向量：VEC_XHCI=0x40（USB MSI-X），VEC_TIMER=0x41（LAPIC 定时器）。
 * 中断桩在 Isr.S 中实现；C 侧逻辑在 Arch.c。
 */
#ifndef ARCH_H
#define ARCH_H

#include "HalPort.h"

#endif
