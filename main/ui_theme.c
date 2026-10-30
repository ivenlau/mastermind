// main/ui_theme.c —— 见 ui_theme.h。色值即设计定版。
// 珠色板排布原则:冷暖交替 + 明度差,相邻序号色差最大;选中环独立于所有珠色。
#include "ui_theme.h"

static int s_theme = 1;   // 默认主题:黄铜机械(与 store 默认值一致)

static const app_theme_t THEMES[UI_THEME_COUNT] = {
    {   // 暗夜霓虹(默认)
        .bg = 0x0B1026, .surface = 0x141B3D, .stroke = 0x2A3466,
        .text = 0xE6ECFF, .dim = 0x8B94B8,
        .accent = 0x22D3EE, .accent2 = 0x7C5CFF,
        .exact = 0x34D399, .partial = 0xFBBF24,
        .ok = 0x34D399, .warn = 0xFBBF24, .bad = 0xF43F5E,
        .sel_ring = 0xFFFFFF, .sel_glow = 0x000000,
        .peg = { 0xFF4136, 0x2ECC40, 0xFF8C1A, 0x4D7CFE,
                 0xFFD400, 0xB26BFF, 0x00E0E0, 0xFFFFFF },
    },
    {   // 黄铜机械
        .bg = 0x2B1D0E, .surface = 0x3E2C16, .stroke = 0x6B4E26,
        .text = 0xF5E7CB, .dim = 0xB99A66,
        .accent = 0xD4A24E, .accent2 = 0xC97B3C,
        .exact = 0x8BC34A, .partial = 0xE6B84C,
        .ok = 0x8BC34A, .warn = 0xE6B84C, .bad = 0xC0392B,
        .sel_ring = 0xFFF8E7, .sel_glow = 0x1A1206,
        .peg = { 0xE04B3A, 0x7FB04A, 0x5599D1, 0xF2CE3E,
                 0xB36BC9, 0x3FC1B0, 0xE8804F, 0xF5EFD9 },
    },
    {   // 糖果乐园(浅色)
        .bg = 0xFFF7ED, .surface = 0xFFFFFF, .stroke = 0xF2D5BA,
        .text = 0x4A3428, .dim = 0xA08468,
        .accent = 0xFB7185, .accent2 = 0x8B5CF6,
        .exact = 0x4ADE80, .partial = 0xFDE047,
        .ok = 0x22C55E, .warn = 0xF59E0B, .bad = 0xEF4444,
        .sel_ring = 0x2D3436, .sel_glow = 0xFFFFFF,
        .peg = { 0xD63031, 0x1E9E5A, 0x2B6CE0, 0xE8A200,
                 0x8E44AD, 0xE17055, 0x0FA3B1, 0x2D3436 },
    },
    {   // 磷光终端
        .bg = 0x020402, .surface = 0x0A1F0A, .stroke = 0x1E4D2B,
        .text = 0xB9FFCB, .dim = 0x4E9B6A,
        .accent = 0x33FF66, .accent2 = 0x00CCFF,
        .exact = 0x33FF66, .partial = 0xFFB000,
        .ok = 0x33FF66, .warn = 0xFFB000, .bad = 0xFF3333,
        .sel_ring = 0xFFFFFF, .sel_glow = 0x000000,
        .peg = { 0xFF4444, 0x44FF44, 0xFFAA00, 0x4488FF,
                 0xFFFF44, 0xCC66FF, 0x44FFFF, 0xFFFFFF },
    },
    {   // 复古木匣
        .bg = 0x3B2F2A, .surface = 0x54413A, .stroke = 0x7A5C48,
        .text = 0xF0E2CE, .dim = 0xB39A82,
        .accent = 0xC9A227, .accent2 = 0xA0522D,
        .exact = 0x7A9B57, .partial = 0xC9A227,
        .ok = 0x7A9B57, .warn = 0xC9A227, .bad = 0xB5443A,
        .sel_ring = 0xF2E8D5, .sel_glow = 0x1E1712,
        .peg = { 0xD35445, 0x7FA650, 0x5B8FC9, 0xE8C24A,
                 0xB07BC4, 0x4FBFA8, 0xE58A5C, 0xF2E8D5 },
    },
};

static const char *NAMES[UI_THEME_COUNT] = {
    "暗夜霓虹", "黄铜机械", "糖果乐园", "磷光终端", "复古木匣",
};

const app_theme_t *theme_get(int theme) {
    if (theme < 0 || theme >= UI_THEME_COUNT) theme = 0;
    return &THEMES[theme];
}

const char *theme_name(int theme) {
    if (theme < 0 || theme >= UI_THEME_COUNT) theme = 0;
    return NAMES[theme];
}

void theme_set(int theme) {
    s_theme = (theme >= 0 && theme < UI_THEME_COUNT) ? theme : 0;
}

int theme_index(void) {
    return s_theme;
}

const app_theme_t *theme_cur(void) {
    return &THEMES[s_theme];
}
