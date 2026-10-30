// main/ui_font.c —— 见 ui_font.h。生成字体在 assets/fonts/,这里只做声明与回退接线。
#include "ui_font.h"

LV_FONT_DECLARE(cipher_font_12)
LV_FONT_DECLARE(cipher_font_14)
LV_FONT_DECLARE(cipher_font_16)
LV_FONT_DECLARE(cipher_font_20)
LV_FONT_DECLARE(cipher_font_28)

// 可写描述符副本:主字体(中文子集)+ 回退(Montserrat,LVGL 符号/罕见拉丁)。
// 回退是单向的:Montserrat 补不了子集缺失的汉字,缺字必须重新生成子集。
static lv_font_t s_f12, s_f14, s_f16, s_f20, s_f28;
static bool s_ready;

void ui_fonts_init(void) {
    if (s_ready) return;
    s_f12 = cipher_font_12;
    s_f12.fallback = &lv_font_montserrat_14;
    s_f14 = cipher_font_14;
    s_f14.fallback = &lv_font_montserrat_14;
    s_f16 = cipher_font_16;
    s_f16.fallback = &lv_font_montserrat_20;      // 没有 16 号 Montserrat,符号就近回退
    s_f20 = cipher_font_20;
    s_f20.fallback = &lv_font_montserrat_20;
    s_f28 = cipher_font_28;
    s_f28.fallback = &lv_font_montserrat_20;      // 大数字符号回退同理
    s_ready = true;
}

const lv_font_t *font_ui_12(void) { return &s_f12; }
const lv_font_t *font_ui_14(void) { return &s_f14; }
const lv_font_t *font_ui_16(void) { return &s_f16; }
const lv_font_t *font_ui_20(void) { return &s_f20; }
const lv_font_t *font_ui_28(void) { return &s_f28; }

bool ui_font_has_glyph(const lv_font_t *font, uint32_t codepoint) {
    if (font == NULL) return false;
    lv_font_glyph_dsc_t glyph = { 0 };
    return lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) && !glyph.is_placeholder;
}
