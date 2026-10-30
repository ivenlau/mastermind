// main/ui_font.h —— 应用字体:4 档中文子集(Noto Sans SC 子集,由
// tools/gen_cipher_font.py 从字符清单生成)+ Montserrat 回退(LVGL 符号)。
// 中文渲染是"字体集成"任务:字符清单/生成命令在 assets/fonts/,新增 UI 文案
// 必须重新生成并跑 tests/test_font_inventory.py,禁止留缺字方框。
#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

// 尺寸档:12 极小角标,14 状态栏/提示,16 正文,20 标题/按钮,28 大数字。
const lv_font_t *font_ui_12(void);
const lv_font_t *font_ui_14(void);
const lv_font_t *font_ui_16(void);
const lv_font_t *font_ui_20(void);
const lv_font_t *font_ui_28(void);

// LVGL 初始化后、创建任何控件前调用一次(构造带 fallback 的字体描述符)。
void ui_fonts_init(void);

// 字形覆盖探测:font 是否真的含该码点(非占位)。设备端字体验收用。
bool ui_font_has_glyph(const lv_font_t *font, uint32_t codepoint);
