/*
 * _template/Template.c — 拷贝源（PR-D-tpl-1）
 *
 * 本文件在 HAL/X64/Drivers/_template/ 下，Makefile 只通配 Drivers/*.c，
 * **不会**编进 Kernel.elf。请复制到上一级再改名接入。
 *
 * 复制后：
 *   1) cp Template.c ../MyDriver.c
 *  2) 改 Name / Template* / gTemplate* / TemplateDriverRegister
 *  3) HalDriverRegister() 加 MyDriverRegister();
 *  见同目录 README.md
 */
#include "Driver.h"
#include "DriverInput.h"

static int gTemplateReady;

static void TemplatePoll(void) {
}

static int TemplateKbdDequeue(HAL_KEYBOARD_REPORT *R) {
    (void)R;
    return 0;
}

static int TemplateMousePresent(void) {
    return 0;
}

static int TemplateMouseDequeue(HAL_MOUSE_REPORT *R) {
    (void)R;
    return 0;
}

static const INPUT_BACKEND gTemplateBackend = {
    .Poll = TemplatePoll,
    .KeyboardDequeue = TemplateKbdDequeue,
    .KeyboardSetLeds = 0,
    .MousePresent = TemplateMousePresent,
    .MouseDequeue = TemplateMouseDequeue,
};

static int TemplateProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;
    /* TODO: 扫 PCI / 口 / MMIO；有设备则 gTemplateReady=1; *OutPriv=…; return 0; */
    if (OutPriv) {
        *OutPriv = 0;
    }
    return -1; /* 模板默认不上线 */
}

static int TemplateBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (ToyDriverInputReady()) {
        return -1; /* 单槽：已有 Input 则失败 */
    }
    return ToyDriverInputAttach(&gTemplateBackend);
}

static void TemplateRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gTemplateReady = 0;
}

static const TOY_DRIVER gTemplateDriver = {
    .Name = "template-input",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = 0,
    .Probe = TemplateProbe,
    .Bind = TemplateBind,
    .Remove = TemplateRemove,
};

void TemplateDriverRegister(void) {
    (void)ToyDriverRegister(&gTemplateDriver);
}
