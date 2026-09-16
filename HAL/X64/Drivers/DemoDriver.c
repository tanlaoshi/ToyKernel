/*
 * DemoDriver.c — 课堂 Demo（PR-D-tpl-2）
 *
 * 证明 Register → Probe → Bind → lsdev 路径；不摸硬件、不抢 Input Backend。
 * 关闭：编译加 -DTOY_DEMO_DRIVER=0（见 HalDevices.c）。
 */
#include "Driver.h"
#include "Hal.h"

static int DemoProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;
    if (OutPriv) {
        *OutPriv = 0;
    }
    HalSerialWrite("demo: probe called\n");
    return 0; /* 恒匹配，课堂证明路径 */
}

static int DemoBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    HalSerialWrite("demo: bind called\n");
    /*
     * 故意不调用 ToyDriverInputAttach。
     * 只占 Driver 实例槽，使 lsdev 可见；真 Input 仍由 xhci-hid / ps2-kbd 管理。
     */
    return 0;
}

static void DemoRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gDemoDriver = {
    .Name = "demo-driver",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = 0,
    .Probe = DemoProbe,
    .Bind = DemoBind,
    .Remove = DemoRemove,
};

void DemoDriverRegister(void) {
    (void)ToyDriverRegister(&gDemoDriver);
}
