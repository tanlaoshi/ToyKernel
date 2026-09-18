/*
 * _template/TemplateNetL2.c — 网卡 L2 注释骨架（PR-N-nic-doc）
 *
 * 本文件在 HAL/X64/Drivers/_template/ 下，**不会**编进 Kernel.elf。
 * 复制到上一级再改名：cp TemplateNetL2.c ../MyNic.c
 *
 * 活范例：../NetE1000.c
 * 契约：Include/DriverNic.h → NetAttachNic
 * 指南：Documents/驱动开发指南.md §5.1
 * 过程：Documents/驱动开发范例-网卡L2.md
 *
 * —— 下面为注释草稿（勿在本目录当真驱动改）——
 *
 * #include "Driver.h"
 * #include "DriverNic.h"
 * #include "Net.h"
 *
 * static int TemplateNetSendFrame(const UINT8 *Frame, UINTN FrameLen);
 * static void TemplateNetPoll(void);
 * static void TemplateNetGetMac(UINT8 Mac[6]);
 * // optional: TemplateNetGetLink(...)
 *
 * static const NIC_L2 gTemplateNicL2 = {
 *     .SendFrame = TemplateNetSendFrame,
 *     .Poll      = TemplateNetPoll,
 *     .GetMac    = TemplateNetGetMac,
 *     .GetLink   = 0,
 * };
 *
 * static int TemplateNetProbe(...) {
 *     // PCI 扫 VID/DID 或 class；无卡 return -1；有卡 Setup 后 return 0
 *     return -1;
 * }
 *
 * static int TemplateNetBind(TOY_DRIVER_INSTANCE *Inst) {
 *     (void)Inst;
 *     return NetAttachNic(&gTemplateNicL2);
 * }
 *
 * // RX 路径（Poll 或 IRQ）：NetInputFrame(Pkt, Len);
 *
 * static const TOY_DRIVER gTemplateNetDriver = {
 *     .Name  = "template-net",
 *     .Class = TOY_DRIVER_CLASS_NET,
 *     .Probe = TemplateNetProbe,
 *     .Bind  = TemplateNetBind,
 *     .Remove = ...,
 * };
 *
 * void TemplateNetDriverRegister(void) {
 *     (void)ToyDriverRegister(&gTemplateNetDriver);
 * }
 * // HalDriverRegister() 加一行 TemplateNetDriverRegister();
 */
