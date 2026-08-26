#include "Kernel.h"
#include "Video.h"
#include "UI.h"

void KernelEntry(BOOT_CONFIG *BootConfig) {
    SetVideo(&BootConfig->VideoConfig);
    ClearScreen(COLOR_DARK_GRAY);
    
    UINT32 W = BootConfig->VideoConfig.HorizontalResolution;
    UINT32 H = BootConfig->VideoConfig.VerticalResolution;
    
    // ============================================================
    //  第一步：画背景和边框
    // ============================================================
    
    DrawRectangle(10, 10, W - 20, H - 20, COLOR_WHITE);
    DrawRectangle(12, 12, W - 24, H - 24, COLOR_GRAY);
    
    // 标题
    DrawString(W/2 - 60, 30, "ToyOS UI Demo", COLOR_WHITE);
    DrawLine(W/2 - 80, 50, W/2 + 80, 50, COLOR_CYAN);
    
    // ============================================================
    //  第二步：图形区（左半部分）
    // ============================================================
    
    // 图形区标题
    DrawString(20, 70, "--- Graphics ---", COLOR_YELLOW);
    
    // 第1行：直线（左） + 矩形（右）
    DrawLine(30, 100, 140, 160, COLOR_RED);
    DrawString(30, 175, "Line", COLOR_WHITE);
    
    DrawRectangle(200, 100, 100, 70, COLOR_GREEN);
    DrawString(200, 185, "Rectangle", COLOR_WHITE);
    
    // 第2行：填充矩形（左） + 圆（右）
    FillRectangle(30, 210, 100, 70, COLOR_BLUE);
    DrawString(30, 295, "FillRectangle", COLOR_WHITE);
    
    DrawCircle(270, 250, 45, COLOR_YELLOW);
    DrawString(240, 305, "Circle", COLOR_WHITE);
    
    // 第3行：填充圆（左） + 圆角矩形（右）
    FillCircle(80, 350, 40, COLOR_MAGENTA);
    DrawString(50, 405, "FillCircle", COLOR_WHITE);
    
    DrawRoundRectangle(200, 320, 120, 80, 15, COLOR_CYAN);
    DrawString(210, 415, "RoundRectangle", COLOR_WHITE);
    
    // 第4行：填充圆角矩形
    FillRoundRectangle(30, 430, 120, 70, 15, COLOR_GREEN);
    DrawString(30, 515, "FillRoundRect", COLOR_WHITE);
    
    // ============================================================
    //  第三步：UI 元素区（右半部分）- 调整宽度防止出界
    // ============================================================
    
    UINT32 rightX = W / 2 + 30;
    UINT32 rightWidth = W - rightX - 30;  // 动态计算剩余宽度
    
    // UI 区标题
    DrawString(rightX, 70, "--- UI Elements ---", COLOR_YELLOW);
    
    // 按钮（缩小宽度，三个按钮并排）
    UINT32 btnWidth = (rightWidth - 40) / 3;
    DrawButton(rightX, 100, btnWidth, 40, "Click Me", COLOR_WHITE, COLOR_BLUE);
    DrawButton(rightX + btnWidth + 10, 100, btnWidth, 40, "Cancel", COLOR_WHITE, COLOR_RED);
    DrawButton(rightX + 2 * (btnWidth + 10), 100, btnWidth, 40, "OK", COLOR_BLACK, COLOR_GREEN);
    
    // 进度条
    DrawString(rightX, 165, "Progress Bar:", COLOR_WHITE);
    DrawProgressBar(rightX, 185, rightWidth - 60, 25, 75, 100, COLOR_GREEN, COLOR_GRAY);
    DrawString(rightX + rightWidth - 50, 185, "75%", COLOR_WHITE);
    
    DrawString(rightX, 230, "Loading:", COLOR_WHITE);
    DrawProgressBar(rightX, 250, rightWidth - 60, 25, 40, 100, COLOR_CYAN, COLOR_GRAY);
    DrawString(rightX + rightWidth - 50, 250, "40%", COLOR_WHITE);
    
    // 状态面板
    UINT32 panelH = 140;
    DrawRoundRectangle(rightX, 295, rightWidth, panelH, 10, COLOR_GRAY);
    DrawString(rightX + 15, 315, "Status: System Running", COLOR_GREEN);
    DrawString(rightX + 15, 340, "Memory: 512 MB", COLOR_WHITE);
    DrawString(rightX + 15, 365, "CPU: x86_64", COLOR_WHITE);
    DrawString(rightX + 15, 390, "Resolution: 1024x768", COLOR_WHITE);
    DrawString(rightX + 15, 415, "ToyOS v0.1", COLOR_CYAN);
    
    // 底部状态栏
    FillRectangle(0, H - 25, W, 25, COLOR_GRAY);
    DrawString(20, H - 20, "ToyOS v0.1 - UI Demo", COLOR_WHITE);
    DrawString(W - 250, H - 20, "ESC: Exit | F1: Help", COLOR_WHITE);

    while (1);
}