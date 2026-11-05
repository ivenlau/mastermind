// main/ui_shell.c —— 见 ui_shell.h。
#include "ui_shell.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"

#include "store.h"
#include "ui_font.h"
#include "ui_theme.h"

static const char *TAG = "shell";

#define SHELL_TOP_H     28
#define SHELL_BOTTOM_H  28
#define SHEEL_STACK_MAX 8

static lv_obj_t *s_screen;
static lv_obj_t *s_top;
static lv_obj_t *s_title;
static lv_obj_t *s_time;
static lv_obj_t *s_batt_box;
static lv_obj_t *s_batt_fill;
static lv_obj_t *s_batt_text;
static lv_obj_t *s_content;
static lv_obj_t *s_hint;
static lv_obj_t *s_toast;
static lv_timer_t *s_batt_timer;

static const app_page_t *s_stack[SHEEL_STACK_MAX];
static int s_top_idx = -1;

// 动画执行回调必须是 (void*, int32_t) 签名;lv_obj_set_style_opa 多一个 selector
// 参数,不能强转,统一走包装。
static void anim_set_opa(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

// ---- 电池小组件:外框 + 电量条 + 百分比(-1 时只画图标) ----
static void battery_draw(int soc) {
    const app_theme_t *t = theme_cur();
    lv_obj_set_style_bg_color(s_batt_box, lv_color_hex(t->dim), LV_PART_MAIN);
    if (soc < 0) {
        lv_obj_set_width(s_batt_fill, 2);
        lv_label_set_text(s_batt_text, "");
        return;
    }
    int w = soc > 100 ? 100 : soc;
    lv_obj_set_width(s_batt_fill, 2 + (18 * w / 100));
    lv_obj_set_style_bg_color(s_batt_fill,
        lv_color_hex(soc <= 15 ? t->bad : t->ok), LV_PART_MAIN);
    lv_label_set_text_fmt(s_batt_text, "%d%%", soc);
}

// 电量显示预热:深睡唤醒后电量计刚被唤醒,前一两个读数可能虚低,
// 直接着色会出现红色低电闪跳(需求)。先中性显示,连续有效读数后再上色。
static int s_batt_streak;

static void battery_update(void) {
    int soc = bsp_battery_soc();
    if (soc < 0) {
        s_batt_streak = 0;
        battery_draw(-1);
        return;
    }
    if (s_batt_streak < 1) {
        s_batt_streak++;
        battery_draw(-1);       // 首个有效读数只做预热,不显示
        return;
    }
    battery_draw(soc);
}

static void battery_timer_cb(lv_timer_t *tmr) {
    (void)tmr;
    battery_update();
}

// 状态栏左侧时钟:精确到秒(需求)。日期未设置显示占位符。
static void clock_timer_cb(lv_timer_t *tmr) {
    (void)tmr;
    if (!s_time) return;
    int h, m, s;
    time_hms(&h, &m, &s);
    if (h < 0) {
        lv_label_set_text(s_time, "--:--:--");
    } else {
        lv_label_set_text_fmt(s_time, "%02d:%02d:%02d", h, m, s);
    }
}

// ---- 成就浮层 ----
static void toast_timer_cb(lv_timer_t *tmr) {
    (void)tmr;
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

void shell_toast(const char *title, const char *desc) {
    const app_theme_t *t = theme_cur();
    if (s_toast) lv_obj_delete(s_toast);
    s_toast = lv_obj_create(s_content);
    lv_obj_set_size(s_toast, 216, 96);
    lv_obj_center(s_toast);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(t->surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(t->accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_toast, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_toast, 14, LV_PART_MAIN);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lb = shell_label(s_toast, title, font_ui_20(), t->accent);
    lv_obj_align(lb, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *db = shell_label(s_toast, desc, font_ui_14(), t->text);
    lv_obj_align(db, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_width(db, 196);
    lv_label_set_long_mode(db, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(db, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_style_opa(s_toast, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_toast);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_exec_cb(&a, anim_set_opa);
    lv_anim_start(&a);

    lv_timer_t *tmr = lv_timer_create(toast_timer_cb, 2600, NULL);
    if (tmr) lv_timer_set_repeat_count(tmr, 1);
}

// ---- 页面栈与内容重建 ----
static void content_build(const app_page_t *page, int slide_dir) {
    const app_theme_t *t = theme_cur();
    s_content = lv_obj_create(s_screen);
    lv_obj_set_pos(s_content, 0, SHELL_TOP_H);
    lv_obj_set_size(s_content, BSP_LCD_W, BSP_LCD_H - SHELL_TOP_H - SHELL_BOTTOM_H);
    lv_obj_set_style_bg_color(s_content, lv_color_hex(t->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_content, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    if (page->title) shell_set_title(page->title);
    shell_set_hints("");
    if (page->enter) page->enter();

    // 入场:横向滑入 + 淡入(slide_dir 仅决定起始方向,视觉统一从右入)
    lv_obj_set_style_opa(s_content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_content);
    lv_anim_set_values(&a, 24, 0);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_start(&a);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_exec_cb(&a, anim_set_opa);
    lv_anim_start(&a);
    (void)slide_dir;
}

static void content_teardown(void) {
    if (!s_content) return;
    const app_page_t *cur = (s_top_idx >= 0) ? s_stack[s_top_idx] : NULL;
    if (cur && cur->exit) cur->exit();
    lv_obj_delete(s_content);
    s_content = NULL;
    s_toast = NULL;   // 挂在 content 下,随父删除
}

void shell_push(const app_page_t *page) {
    if (!page || s_top_idx + 1 >= SHEEL_STACK_MAX) return;
    content_teardown();
    s_stack[++s_top_idx] = page;
    content_build(page, 1);
}

void shell_pop(void) {
    if (s_top_idx <= 0) return;
    content_teardown();
    s_top_idx--;
    content_build(s_stack[s_top_idx], -1);
}

void shell_reload(void) {
    const app_theme_t *t = theme_cur();
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(t->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_top, lv_color_hex(t->surface), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_title, lv_color_hex(t->text), 0);
    lv_obj_set_style_bg_color(s_hint, lv_color_hex(t->surface), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(t->dim), 0);
    if (s_top_idx >= 0) {
        content_teardown();
        content_build(s_stack[s_top_idx], 1);
    }
    battery_draw(bsp_battery_soc());
}

void shell_dispatch(bsp_btn_t btn, bsp_btn_ev_t ev) {
    const app_page_t *cur = (s_top_idx >= 0) ? s_stack[s_top_idx] : NULL;
    if (cur && cur->key && cur->key(btn, ev)) return;
    // 全局返回约定:OK 双击(页面未消费时)。根页忽略。
    if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) shell_pop();
}

void shell_set_hints(const char *text) {
    if (!s_hint) return;
    lv_label_set_text(s_hint, text ? text : "");
}

void shell_set_title(const char *text) {
    if (!s_title) return;
    lv_label_set_text(s_title, text ? text : "");
}

bool shell_no_auto_sleep(void) {
    const app_page_t *cur = (s_top_idx >= 0) ? s_stack[s_top_idx] : NULL;
    return cur ? cur->no_auto_sleep : false;
}

lv_obj_t *shell_content(void) {
    return s_content;
}

// ---- 样式助手 ----
lv_obj_t *shell_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
    lv_obj_t *lb = lv_label_create(parent);
    lv_obj_set_style_text_font(lb, font, 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(color), 0);
    lv_label_set_text(lb, text);
    return lb;
}

lv_obj_t *shell_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    const app_theme_t *t = theme_cur();
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(t->surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(c, lv_color_hex(t->stroke), LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 6, LV_PART_MAIN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

void shell_peg(lv_obj_t *obj, uint32_t color, int radius) {
    const app_theme_t *t = theme_cur();
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(t->stroke), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    // 不用盒阴影:LVGL 软件渲染的 box_shadow 路径在本机触发过看门狗死循环
    // (taskLVGL 卡死在 lv_draw_sw_box_shadow),全应用禁用阴影,靠描边表达层次。
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void shell_glow(lv_obj_t *obj, uint32_t color, bool on) {
    if (!on) {
        // 恢复未选中态:边框颜色必须一起还原,否则上次选中的行会留高亮边框。
        lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(obj, lv_color_hex(theme_cur()->stroke), LV_PART_MAIN);
        return;
    }
    lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(color), LV_PART_MAIN);
}

// ---- 外壳初始化 ----
void shell_init(void) {
    const app_theme_t *t = theme_cur();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(t->bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_top = lv_obj_create(s_screen);
    lv_obj_set_pos(s_top, 0, 0);
    lv_obj_set_size(s_top, BSP_LCD_W, SHELL_TOP_H);
    lv_obj_set_style_bg_color(s_top, lv_color_hex(t->surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_top, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_top, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_top, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_top, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_top, LV_OBJ_FLAG_SCROLLABLE);

    s_title = shell_label(s_top, "", font_ui_16(), t->text);
    // 标题居中(需求);时钟在左、电量在右。
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    // 状态栏左侧时钟(到秒)
    s_time = shell_label(s_top, "--:--:--", font_ui_12(), t->dim);
    lv_obj_align(s_time, LV_ALIGN_LEFT_MID, 8, 0);

    // 电池图标:外框 + 电量条 + 百分比文本(数字紧贴图标左侧,避开右圆角)。
    s_batt_box = lv_obj_create(s_top);
    lv_obj_set_size(s_batt_box, 22, 12);
    lv_obj_align(s_batt_box, LV_ALIGN_RIGHT_MID, -52, 0);
    lv_obj_set_style_radius(s_batt_box, 3, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_batt_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_batt_box, lv_color_hex(t->dim), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_batt_box, 1, LV_PART_MAIN);
    lv_obj_remove_flag(s_batt_box, LV_OBJ_FLAG_SCROLLABLE);
    s_batt_fill = lv_obj_create(s_batt_box);
    lv_obj_set_pos(s_batt_fill, 1, 1);
    lv_obj_set_size(s_batt_fill, 12, 8);
    lv_obj_set_style_radius(s_batt_fill, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_batt_fill, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_batt_fill, LV_OBJ_FLAG_SCROLLABLE);
    s_batt_text = shell_label(s_top, "", font_ui_14(), t->dim);
    lv_obj_align(s_batt_text, LV_ALIGN_RIGHT_MID, -16, 0);

    s_hint = shell_label(s_screen, "", font_ui_14(), t->dim);
    lv_obj_set_width(s_hint, BSP_LCD_W);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_screen_load(s_screen);
    s_batt_timer = lv_timer_create(battery_timer_cb, 2000, NULL);
    lv_timer_create(clock_timer_cb, 1000, NULL);
    battery_draw(-1);          // 启动先中性,预热后着色
    s_batt_streak = 0;
    clock_timer_cb(NULL);
    ESP_LOGI(TAG, "外壳就绪");
}
