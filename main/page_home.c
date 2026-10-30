// main/page_home.c —— 主页:大标题 + 选项列表 + 漂浮彩珠背景 + 统计行。
#include "page_home.h"

#include <stdio.h>

#include "bsp_pins.h"

#include "game_rules.h"
#include "page_achieve.h"
#include "page_daily.h"
#include "page_game.h"
#include "page_settings.h"
#include "sound_player.h"
#include "store.h"
#include "ui_font.h"
#include "ui_theme.h"

#define HOME_ROWS 8

static int s_sel;
static lv_obj_t *s_rows[HOME_ROWS];
static lv_obj_t *s_row_sub[HOME_ROWS];
static lv_obj_t *s_stats;

static const char *row_name(int i) {
    static const char *N[HOME_ROWS] = {
        "标准模式", "经典模式", "专家模式", "大师模式", "数字密码",
        "每日密令", "成就档案", "设置",
    };
    return N[i];
}

static const char *row_sub(int i) {
    const app_save_t *sv = store_get();
    static char buf[40];
    if (i < MODE_COUNT) return gr_mode_params((game_mode_t)i);
    if (i == 5) {
        if (!time_is_set()) return "请先设置日期";
        if (sv->settings.daily_done && sv->settings.daily_done_day == time_day_index()) {
            return "今日已完成";
        }
        snprintf(buf, sizeof(buf), "连签 %u 天", (unsigned)sv->stats.daily_streak);
        return buf;
    }
    if (i == 6) {
        int n = 0;
        for (int k = 0; k < ACH_COUNT; k++) {
            if (sv->ach_seen[k]) n++;
        }
        snprintf(buf, sizeof(buf), "%d/%d 已解锁", n, ACH_COUNT);
        return buf;
    }
    return "主题 · 音效 · 亮度";
}

static void rows_refresh(void) {
    const app_theme_t *t = theme_cur();
    for (int i = 0; i < HOME_ROWS; i++) {
        bool sel = (i == s_sel);
        lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(sel ? t->stroke : t->surface),
                                  LV_PART_MAIN);
        shell_glow(s_rows[i], t->accent, sel);
        lv_obj_set_style_text_color(s_rows[i], lv_color_hex(t->text), 0);
        lv_label_set_text(s_row_sub[i], row_sub(i));
        lv_obj_set_style_text_color(s_row_sub[i],
            lv_color_hex(sel ? t->accent : t->dim), 0);
    }
    const app_save_t *sv = store_get();
    lv_label_set_text_fmt(s_stats, "%s · 连胜 %u · 最高分 %u",
                          gr_rank_name(gr_rank(sv->stats.xp)),
                          (unsigned)sv->stats.cur_streak,
                          (unsigned)sv->settings.best_score);
}

static void home_enter(void) {
    const app_theme_t *t = theme_cur();
    lv_obj_t *content = shell_content();
    store_daily_tick();   // 跨天后回主页即触发道具补给

    // 漂浮彩珠背景(静态错落 + 缓速浮动)
    for (int i = 0; i < 5; i++) {
        lv_obj_t *p = lv_obj_create(content);
        int x = 12 + (i * 47) % 210;
        int y = 10 + (i * 61) % 230;
        lv_obj_set_pos(p, x, y);
        lv_obj_set_size(p, 14 + (i % 3) * 6, 14 + (i % 3) * 6);
        shell_peg(p, t->peg[i % UI_PEG_COLORS], 12);
        lv_obj_set_style_opa(p, LV_OPA_30, LV_PART_MAIN);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, p);
        lv_anim_set_values(&a, y, y - 10 - (i % 3) * 4);
        lv_anim_set_duration(&a, 1800 + i * 300);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_playback_duration(&a, 1800 + i * 300);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_start(&a);
    }

    lv_obj_t *logo = shell_label(content, "超级密码机", font_ui_28(), t->accent);
    lv_obj_set_width(logo, BSP_LCD_W);
    lv_obj_set_style_text_align(logo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(logo, 0, 6);
    lv_obj_set_style_text_letter_space(logo, 3, 0);

    for (int i = 0; i < HOME_ROWS; i++) {
        s_rows[i] = shell_card(content, 12, 42 + i * 24, 216, 22);
        lv_obj_t *name = shell_label(s_rows[i], row_name(i), font_ui_16(), t->text);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, 0);
        s_row_sub[i] = shell_label(s_rows[i], "", font_ui_14(), t->dim);
        lv_obj_align(s_row_sub[i], LV_ALIGN_RIGHT_MID, -6, 0);
    }

    s_stats = shell_label(content, "", font_ui_14(), t->dim);
    lv_obj_set_width(s_stats, BSP_LCD_W);
    lv_obj_set_style_text_align(s_stats, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_stats, 0, 242);

    rows_refresh();
    shell_set_hints("↑↓ 选择 · OK 进入");
}

static void home_exit(void) {
}

static bool home_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_PRESS && ev != BSP_BTN_CLICK) return false;
    if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) {
        s_sel = (s_sel + HOME_ROWS - 1) % HOME_ROWS;
        rows_refresh();
        sound_play(SND_TICK);
        return true;
    }
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS) {
        s_sel = (s_sel + 1) % HOME_ROWS;
        rows_refresh();
        sound_play(SND_TICK);
        return true;
    }
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        sound_play(SND_CONFIRM);
        switch (s_sel) {
        case 0: game_start_mode(MODE_STANDARD); shell_push(game_page()); break;
        case 1: game_start_mode(MODE_CLASSIC); shell_push(game_page()); break;
        case 2: game_start_mode(MODE_EXPERT); shell_push(game_page()); break;
        case 3: game_start_mode(MODE_MASTER); shell_push(game_page()); break;
        case 4: game_start_mode(MODE_DIGITS); shell_push(game_page()); break;
        case 5: shell_push(daily_page()); break;
        case 6: shell_push(achieve_page()); break;
        case 7: shell_push(settings_page()); break;
        }
        return true;
    }
    return false;
}

static const app_page_t PAGE_HOME = {
    .title = "主页",
    .enter = home_enter,
    .exit = home_exit,
    .key = home_key,
};

const app_page_t *home_page(void) {
    return &PAGE_HOME;
}
