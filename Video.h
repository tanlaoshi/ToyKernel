#ifndef VIDEO_H
#define VIDEO_H

#include "BootConfig.h"

void SetVideo(VIDEO_CONFIG *VideoConfig);
void DrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
void ClearScreen(UINT32 Color);
void DrawChar(UINT32 X, UINT32 Y, char c, UINT32 Color);
void DrawString(UINT32 X, UINT32 Y, const char *str, UINT32 Color);

#endif