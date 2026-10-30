// main/ui_theme.h —— 主题系统:5 套语义色 token(含反馈绿/黄与珠色板)。
// 所有页面样式只从 token 取色,切换主题即全局换肤;主题索引持久化在 store。
#pragma once

#include <stdint.h>

#define UI_THEME_COUNT 5
#define UI_PEG_COLORS  8     // 珠色板容量;模式用前 N 个

typedef struct {
    uint32_t bg;             // 页面底色
    uint32_t surface;        // 卡片底
    uint32_t stroke;         // 描边
    uint32_t text;           // 主文字
    uint32_t dim;            // 次要文字
    uint32_t accent;         // 强调(选中/光晕)
    uint32_t accent2;        // 次强调
    uint32_t exact;          // 反馈绿钉(色对位对)
    uint32_t partial;        // 反馈黄钉(色对位错)
    uint32_t ok;
    uint32_t warn;
    uint32_t bad;
    uint32_t sel_ring;             // 选中环描边(必须与任何珠色都区分)
    uint32_t sel_glow;             // 选中环外发光
    uint32_t peg[UI_PEG_COLORS];   // 珠色 0..7(冷暖交替排布,相邻色差最大)
} app_theme_t;

// theme 0..UI_THEME_COUNT-1;越界回退 0。
const app_theme_t *theme_get(int theme);
const char *theme_name(int theme);

// 当前主题(应用启动/切换后有效)。切换只改索引;UI 重建由页面/外壳负责。
void theme_set(int theme);
int theme_index(void);
const app_theme_t *theme_cur(void);
