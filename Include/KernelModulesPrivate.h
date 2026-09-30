/*
 * KernelModulesPrivate.h — KernelModules / Init 内部
 */
#ifndef KERNEL_MODULES_PRIVATE_H
#define KERNEL_MODULES_PRIVATE_H

int InitializeSerial(void);
int InitializePhysicalMemory(void);
int InitializeVirtualMemory(void);
int InitializeVideo(void);
int InitializeCpu(void);
int InitializeSerialEarly(void);
int InitializeSmp(void);
int InitializeUsb(void);
int InitializeFileSystem(void);
int InitializeGui(void);
int InitializeNetwork(void);
int InitializeDriver(void);
int InitializeScheduler(void);
int InitializeConsole(void);

#endif
