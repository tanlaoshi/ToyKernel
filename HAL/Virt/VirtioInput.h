/*
 * VirtioInput.h — virtio-keyboard / tablet（PR-V3 / PR-D3）
 */
#ifndef HAL_VIRTIO_INPUT_H
#define HAL_VIRTIO_INPUT_H

/* PR-D3：向 Drv 注册描述符（不 Probe） */
void VirtioInputRegister(void);
/* Probe Input 类；有键鼠返回 0，否则 -1 */
int VirtioInputInit(void);

#endif
