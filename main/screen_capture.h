// main/screen_capture.h —— FAP_SCREENSHOT_V1 串口截屏协议(只读观测):
// 主机发 "FAP_SCREENSHOT_V1\n",设备回传头行 + RGB565LE 整帧。
// 不重启、不刷写、不改设置;失败只影响本次截屏流。
#pragma once

void screen_capture_init(void);
