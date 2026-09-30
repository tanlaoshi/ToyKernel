/*
 * ToyOsVersion.h — 内核 OS 版本字符串（PR-DEV-ui-summary-data）
 *
 * 仅供内核 / DevicesUi 系统摘要使用；与用户态 CRT 版本解耦。
 * 改版本号改这一处即可。
 */
#ifndef TOY_OS_VERSION_H
#define TOY_OS_VERSION_H

/*
 * 主版本号语义：0.x = 早期教学版。
 * 摘要页展示为「ToyOS 0.1」；可叠加 HalArchName() 显示架构。
 */
#define TOY_OS_VERSION_STRING "ToyOS 0.1"

#endif
