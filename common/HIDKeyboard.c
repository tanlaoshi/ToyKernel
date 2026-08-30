/*
 * HIDKeyboard.c — HID 键码到 ASCII 的映射
 *
 * 美式 QWERTY 布局，支持 Shift 组合键。仅处理按下事件（由上层去重）。
 */
#include "HIDKeyboard.h"

typedef struct {
    UINT8 KeyCode;
    UINT8 NormalChar;
    UINT8 ShiftChar;
} HID_KEYCODE_MAP;

static const HID_KEYCODE_MAP US_ASCII_MAP[] = {
    {0x04, 'a', 'A'},
    {0x05, 'b', 'B'},
    {0x06, 'c', 'C'},
    {0x07, 'd', 'D'},
    {0x08, 'e', 'E'},
    {0x09, 'f', 'F'},
    {0x0A, 'g', 'G'},
    {0x0B, 'h', 'H'},
    {0x0C, 'i', 'I'},
    {0x0D, 'j', 'J'},
    {0x0E, 'k', 'K'},
    {0x0F, 'l', 'L'},
    {0x10, 'm', 'M'},
    {0x11, 'n', 'N'},
    {0x12, 'o', 'O'},
    {0x13, 'p', 'P'},
    {0x14, 'q', 'Q'},
    {0x15, 'r', 'R'},
    {0x16, 's', 'S'},
    {0x17, 't', 'T'},
    {0x18, 'u', 'U'},
    {0x19, 'v', 'V'},
    {0x1A, 'w', 'W'},
    {0x1B, 'x', 'X'},
    {0x1C, 'y', 'Y'},
    {0x1D, 'z', 'Z'},
    {0x1E, '1', '!'},
    {0x1F, '2', '@'},
    {0x20, '3', '#'},
    {0x21, '4', '$'},
    {0x22, '5', '%'},
    {0x23, '6', '^'},
    {0x24, '7', '&'},
    {0x25, '8', '*'},
    {0x26, '9', '('},
    {0x27, '0', ')'},
    {0x2C, ' ', ' '},
    {0x2D, '-', '_'},
    {0x2E, '=', '+'},
    {0x2F, '[', '{'},
    {0x30, ']', '}'},
    {0x31, '\\', '|'},
    {0x33, ';', ':'},
    {0x34, '\'', '"'},
    {0x35, '`', '~'},
    {0x36, ',', '<'},
    {0x37, '.', '>'},
    {0x38, '/', '?'},
    {0x00, 0, 0}
};

/* 将 HID 键码转为 ASCII，考虑 Shift；无法映射返回 0 */
char HIDKeyCodeToASCII(UINT8 KeyCode, UINT8 ModifierKeys) {
    UINT8 IsShift = (ModifierKeys & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) != 0;
    
    for (int i = 0; US_ASCII_MAP[i].KeyCode != 0; i++) {
        if (US_ASCII_MAP[i].KeyCode == KeyCode) {
            if (IsShift) {
                return US_ASCII_MAP[i].ShiftChar;
            } else {
                return US_ASCII_MAP[i].NormalChar;
            }
        }
    }
    
    return 0;
}

/* 返回特殊键的英文名称（方向键、Enter 等），普通键返回空串 */
const char* HIDKeyCodeToName(UINT8 KeyCode) {
    switch (KeyCode) {
        case 0x28: return "ENTER";
        case 0x29: return "ESC";
        case 0x2A: return "BACK";
        case 0x2B: return "TAB";
        case 0x2C: return "SPACE";
        case 0x4C: return "DEL";
        case 0x50: return "LEFT";
        case 0x4F: return "RIGHT";
        case 0x52: return "UP";
        case 0x51: return "DOWN";
        default: return "";
    }
}
