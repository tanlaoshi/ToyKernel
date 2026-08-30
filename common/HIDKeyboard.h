/*
 * HIDKeyboard.h — USB HID 键盘键码定义与转换接口
 *
 * 键码遵循 USB HID Usage Tables。提供键码到 ASCII 及特殊键名称的映射。
 */
#ifndef HID_KEYBOARD_H
#define HID_KEYBOARD_H

#include "BootConfig.h"

#define HID_KEY_A           0x04
#define HID_KEY_Z           0x1D
#define HID_KEY_0           0x27
#define HID_KEY_9           0x26
#define HID_KEY_ENTER       0x28
#define HID_KEY_ESCAPE      0x29
#define HID_KEY_BACKSPACE   0x2A
#define HID_KEY_TAB         0x2B
#define HID_KEY_SPACE       0x2C
#define HID_KEY_MINUS       0x2D
#define HID_KEY_EQUAL       0x2E
#define HID_KEY_LBRACKET    0x2F
#define HID_KEY_RBRACKET    0x30
#define HID_KEY_BACKSLASH   0x31
#define HID_KEY_SEMICOLON   0x33
#define HID_KEY_APOSTROPHE  0x34
#define HID_KEY_GRAVE       0x35
#define HID_KEY_COMMA       0x36
#define HID_KEY_DOT         0x37
#define HID_KEY_SLASH       0x38

#define HID_KEY_LEFT        0x50
#define HID_KEY_RIGHT       0x4F
#define HID_KEY_UP          0x52
#define HID_KEY_DOWN        0x51
#define HID_KEY_DELETE      0x4C

#define HID_MOD_LCTRL   0x01
#define HID_MOD_LSHIFT  0x02
#define HID_MOD_LALT    0x04
#define HID_MOD_LGUI    0x08
#define HID_MOD_RCTRL   0x10
#define HID_MOD_RSHIFT  0x20
#define HID_MOD_RALT    0x40
#define HID_MOD_RGUI    0x80

char HIDKeyCodeToASCII(UINT8 KeyCode, UINT8 ModifierKeys);
const char* HIDKeyCodeToName(UINT8 KeyCode);

#endif
