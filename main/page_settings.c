// main/page_settings.c —— 设置:主题实时换肤、音效/音量/亮度、日期设定、重置、关于。
// 日期子模式:设备离线无 RTC,首次需手动设一次日期;之后按单调时钟推进。
#include "page_settings.h"

#include <stdio.h>

#include "bsp_display.h"

#include "page_about.h"
#include "sound_player.h"
#include "store.h"
#include "ui_font.h"
#include "ui_theme.h"

#define SET_ROWS 8

typedef enum {
    SEL_NORMAL = 0,
    SEL_DATE,        // 编辑 年/月/日/时
    SEL_RESET,       // 重置确认
} sel_mode_t;

static int s_sel;
static sel_mode_t s_mode;
static int s_date_field;              // 0=年 1=月 2=日 3=时
static int s_date_val[4];
static int s_reset_sel;
static lv_obj_t *s_rows[SET_ROWS];
static lv_obj_t *s_value[SET_ROWS];
static lv_obj_t *s_overlay;

static const char *row_name(int i) {
    static const char *N[SET_ROWS] = {
        "主题", "音效", "音量", "亮度", "设置日期", "道具库存", "重置进度", "关于",
    };
    return N[i];
}

static void settings_refresh(void) {
    const app_theme_t *t = theme_cur();
    const app_save_t *sv = store_get();
    static char buf[64];

    for (int i = 0; i < SET_ROWS; i++) {
        bool sel = (i == s_sel && s_mode == SEL_NORMAL);
        lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(sel ? t->stroke : t->surface),
                                  LV_PART_MAIN);
        shell_glow(s_rows[i], t->accent, sel);
        lv_obj_set_style_text_color(s_rows[i], lv_color_hex(t->text), 0);
    }
    lv_label_set_text(s_value[0], theme_name(sv->settings.theme));
    lv_label_set_text(s_value[1], sv->settings.sound_on ? "开" : "关");
    lv_label_set_text_fmt(s_value[2], "%d/4", sv->settings.volume + 1);
    lv_label_set_text_fmt(s_value[3], "%d/4", sv->settings.brightness + 1);
    if (time_is_set()) {
        int y, mo, d, h;
        time_get_date(&y, &mo, &d, &h);
        lv_label_set_text_fmt(s_value[4], "%d-%02d-%02d", y, mo, d);
    } else {
        lv_label_set_text(s_value[4], "未设置");
    }
    snprintf(buf, sizeof(buf), "显%u 排%u 保%u",
             (unsigned)sv->settings.tools[0], (unsigned)sv->settings.tools[1],
             (unsigned)sv->settings.tools[2]);
    lv_label_set_text(s_value[5], buf);
    lv_label_set_text(s_value[6], s_mode == SEL_RESET ? "确认清空?" : "");
    lv_label_set_text(s_value[7], "v1.0");
}


static void date_overlay_refresh(void) {
    const app_theme_t *t = theme_cur();
    if (!s_overlay) {
        s_overlay = lv_obj_create(shell_content());
        lv_obj_set_size(s_overlay, 216, 104);
        lv_obj_center(s_overlay);
        lv_obj_set_style_bg_color(s_overlay, lv_color_hex(t->surface), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_overlay, lv_color_hex(t->accent), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_overlay, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(s_overlay, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_overlay, 8, LV_PART_MAIN);
        lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_clean(s_overlay);
    lv_obj_t *title = shell_label(s_overlay, "设置日期", font_ui_20(), t->accent);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);
    static const char *FIELDS[4] = { "年", "月", "日", "时" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *f = lv_obj_create(s_overlay);
        lv_obj_set_size(f, 46, 52);
        lv_obj_set_pos(f, 6 + i * 50, 34);
        lv_obj_set_style_bg_color(f, lv_color_hex(i == s_date_field ? t->stroke : t->bg),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(f, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(f, i == s_date_field ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(f, lv_color_hex(t->accent), LV_PART_MAIN);
        lv_obj_set_style_radius(f, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(f, 2, LV_PART_MAIN);
        lv_obj_remove_flag(f, LV_OBJ_FLAG_SCROLLABLE);
        // 数值与单位分层留白,互不重叠(需求);操作提示只留页面底部一处。
        lv_obj_t *v = shell_label(f, "", font_ui_16(), t->text);
        lv_label_set_text_fmt(v, "%d", s_date_val[i]);
        lv_obj_align(v, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_t *u = shell_label(f, FIELDS[i], font_ui_12(), t->dim);
        lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, -3);
    }
}

static void overlay_close(void) {
    if (s_overlay) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
    }
}

static void apply_brightness(void) {
    static const int PCT[4] = { 20, 50, 80, 100 };
    bsp_display_backlight((uint8_t)PCT[store_get()->settings.brightness]);
}

static void settings_enter(void) {
    const app_theme_t *t = theme_cur();
    lv_obj_t *content = shell_content();

    for (int i = 0; i < SET_ROWS; i++) {
        s_rows[i] = shell_card(content, 12, 4 + i * 32, 216, 30);
        lv_obj_t *nm = shell_label(s_rows[i], row_name(i), font_ui_16(), t->text);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 8, 0);
        s_value[i] = shell_label(s_rows[i], "", font_ui_16(), t->accent);
        lv_obj_align(s_value[i], LV_ALIGN_RIGHT_MID, -8, 0);
    }
    s_overlay = NULL;
    settings_refresh();
    shell_set_hints("↑↓ 移动 · OK 修改 · 双击返回");
}

static void settings_exit(void) {
    overlay_close();
    for (int i = 0; i < SET_ROWS; i++) {
        s_rows[i] = NULL;
        s_value[i] = NULL;
    }
}

static void row_change(int dir) {
    app_save_t *sv = store_get();
    switch (s_sel) {
    case 0: {
        int th = (sv->settings.theme + dir + UI_THEME_COUNT) % UI_THEME_COUNT;
        sv->settings.theme = (uint8_t)th;
        theme_set(th);
        ach_apply_theme(&sv->stats, th);
        store_flush();
        shell_reload();          // 当前页整页重建 = 实时换肤
        return;
    }
    case 1:
        sv->settings.sound_on = !sv->settings.sound_on;
        sound_set_enabled(sv->settings.sound_on);
        break;
    case 2:
        sv->settings.volume = (uint8_t)((sv->settings.volume + dir + 4) % 4);
        sound_set_volume(sv->settings.volume);
        break;
    case 3:
        sv->settings.brightness = (uint8_t)((sv->settings.brightness + dir + 4) % 4);
        apply_brightness();
        break;
    default:
        return;
    }
    store_flush();
    settings_refresh();
}

static bool settings_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    app_save_t *sv = store_get();

    // ---- 日期编辑 ----
    if (s_mode == SEL_DATE) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            s_mode = SEL_NORMAL;
            overlay_close();
            settings_refresh();
            shell_set_hints("↑↓ 移动 · OK 修改 · 双击返回");
            return true;
        }
        if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS) {
            int dir = (btn == BSP_BTN_UP) ? 1 : -1;
            static const int MAXV[4] = { 2099, 12, 31, 23 };
            static const int MINV[4] = { 2024, 1, 1, 0 };
            s_date_val[s_date_field] += dir;
            if (s_date_val[s_date_field] > MAXV[s_date_field]) s_date_val[s_date_field] = MINV[s_date_field];
            if (s_date_val[s_date_field] < MINV[s_date_field]) s_date_val[s_date_field] = MAXV[s_date_field];
            date_overlay_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            sound_play(SND_TICK);
            if (s_date_field < 3) {
                s_date_field++;
            } else {
                if (time_set_date(s_date_val[0], s_date_val[1], s_date_val[2],
                                  s_date_val[3]) == ESP_OK) {
                    sound_play(SND_CONFIRM);
                } else {
                    sound_play(SND_ERROR);
                }
                s_mode = SEL_NORMAL;
                overlay_close();
                settings_refresh();
                shell_set_hints("↑↓ 移动 · OK 修改 · 双击返回");
                return true;
            }
            date_overlay_refresh();
            return true;
        }
        return true;
    }

    // ---- 重置确认 ----
    if (s_mode == SEL_RESET) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            s_mode = SEL_NORMAL;
            settings_refresh();
            return true;
        }
        if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS) {
            s_reset_sel = 1 - s_reset_sel;
            settings_refresh();
            sound_play(SND_TICK);
            return true;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            if (s_reset_sel == 0) {
                store_reset_progress();
                store_flush();
                sound_play(SND_ACHIEVE);
                shell_toast("已重置", "成就与统计已清空");
            } else {
                sound_play(SND_BACK);
            }
            s_mode = SEL_NORMAL;
            settings_refresh();
            return true;
        }
        return true;
    }

    // ---- 常规行 ----
    if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) {
        s_sel = (s_sel + SET_ROWS - 1) % SET_ROWS;
        settings_refresh();
        sound_play(SND_TICK);
        return true;
    }
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS) {
        s_sel = (s_sel + 1) % SET_ROWS;
        settings_refresh();
        sound_play(SND_TICK);
        return true;
    }
    // 注意:↑↓ 只移动选项,不在 CLICK 里改值——一次按键会先后产生
    // PRESS/CLICK,两处都处理就会"滚动后又点了一下"(需求 #8 的 bug)。
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        sound_play(SND_TICK);
        switch (s_sel) {
        case 0: case 1: case 2: case 3:
            row_change(1);
            break;
        case 4: {
            s_mode = SEL_DATE;
            s_date_field = 0;
            if (time_is_set()) {
                time_get_date(&s_date_val[0], &s_date_val[1], &s_date_val[2], &s_date_val[3]);
            } else {
                s_date_val[0] = 2026;
                s_date_val[1] = 1;
                s_date_val[2] = 1;
                s_date_val[3] = 12;
            }
            date_overlay_refresh();
            shell_set_hints("↑↓ 改数值 · OK 下一项 · 双击取消");
            return true;
        }
        case 5:
            break;
        case 6:
            s_mode = SEL_RESET;
            s_reset_sel = 1;   // 默认"否",防误触
            settings_refresh();
            shell_set_hints("↑↓ 是/否 · OK 确认 · 双击取消");
            return true;
        case 7:
            shell_push(about_page());   // 关于 = 独立页,内含可滚动的玩法介绍(需求)
            return true;
        }
        return true;
    }
    return false;
}

static const app_page_t PAGE_SETTINGS = {
    .title = "设置",
    .enter = settings_enter,
    .exit = settings_exit,
    .key = settings_key,
};

const app_page_t *settings_page(void) {
    return &PAGE_SETTINGS;
}
